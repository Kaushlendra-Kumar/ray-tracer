package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"os/exec"
	"sync"
	"syscall"
	"time"
)

// JobState is the lifecycle of one render job.
type JobState string

const (
	StateRunning   JobState = "running"
	StateDone      JobState = "done"
	StateCancelled JobState = "cancelled"
	StateFailed    JobState = "failed"
)

// Event is pushed to SSE subscribers.
type Event struct {
	Type string `json:"type"` // "frame" | "done" | "error"
	Data any    `json:"data"`
}

// FrameEvent is the payload of a "frame" event. The PNG itself is fetched
// from /api/render/{id}/frame so that events stay small.
type FrameEvent struct {
	JobID int64 `json:"job_id"`
	Seq   int   `json:"seq"`
	FrameHeader
}

// Job is one invocation of the rt binary.
type Job struct {
	ID      int64
	Preview bool
	Created time.Time

	mu       sync.Mutex
	state    JobState
	frame    []byte
	seq      int
	header   FrameHeader
	stats    json.RawMessage
	errMsg   string
	subs     map[chan Event]struct{}
	history  []Event // last frame + terminal event, replayed to late subscribers
	cancelFn context.CancelFunc
	done     chan struct{}
}

func (j *Job) snapshot() (state JobState, frame []byte, seq int) {
	j.mu.Lock()
	defer j.mu.Unlock()
	return j.state, j.frame, j.seq
}

// Subscribe returns a channel of events; previously emitted state is replayed
// so a subscriber that connects late still sees the latest frame and the end.
func (j *Job) Subscribe() (<-chan Event, func()) {
	ch := make(chan Event, 16)
	j.mu.Lock()
	for _, e := range j.history {
		ch <- e
	}
	terminal := j.state != StateRunning
	if !terminal {
		j.subs[ch] = struct{}{}
	}
	j.mu.Unlock()
	if terminal {
		close(ch)
		return ch, func() {}
	}
	return ch, func() {
		j.mu.Lock()
		if _, ok := j.subs[ch]; ok {
			delete(j.subs, ch)
			close(ch)
		}
		j.mu.Unlock()
	}
}

func (j *Job) publish(e Event, terminal bool) {
	j.mu.Lock()
	defer j.mu.Unlock()
	if e.Type == "frame" {
		j.history = []Event{e} // only the latest frame matters
	} else {
		j.history = append(j.history, e)
	}
	for ch := range j.subs {
		select {
		case ch <- e:
		default:
			// Slow consumer: drop intermediate frames, never block the renderer.
			if terminal {
				// make room for the terminal event
				select {
				case <-ch:
				default:
				}
				ch <- e
			}
		}
	}
	if terminal {
		for ch := range j.subs {
			close(ch)
		}
		j.subs = map[chan Event]struct{}{}
	}
}

// Cancel asks the renderer to stop (SIGTERM => clean cancel in rt).
func (j *Job) Cancel() { j.cancelFn() }

// Manager runs at most one job at a time: starting a new job cancels the
// previous one, which is what an interactive editor wants. Job IDs increase
// monotonically so clients can discard events from stale jobs.
type Manager struct {
	rtBin   string
	workDir string
	maxJobs int
	timeout time.Duration
	mu      sync.Mutex
	nextID  int64
	jobs    map[int64]*Job
	order   []int64
	current *Job
}

func NewManager(rtBin, workDir string) *Manager {
	return &Manager{rtBin: rtBin, workDir: workDir, maxJobs: 16, timeout: 30 * time.Minute, jobs: map[int64]*Job{}}
}

func (m *Manager) Get(id int64) *Job {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.jobs[id]
}

// Start launches a render of sceneJSON. extraArgs are additional CLI flags.
func (m *Manager) Start(sceneJSON []byte, preview bool, extraArgs []string) (*Job, error) {
	m.mu.Lock()
	prev := m.current
	m.nextID++
	id := m.nextID
	ctx, cancel := context.WithTimeout(context.Background(), m.timeout)
	job := &Job{ID: id, Preview: preview, Created: time.Now(), state: StateRunning,
		subs: map[chan Event]struct{}{}, cancelFn: cancel, done: make(chan struct{})}
	m.jobs[id] = job
	m.order = append(m.order, id)
	for len(m.order) > m.maxJobs { // forget old finished jobs
		old := m.order[0]
		m.order = m.order[1:]
		if j := m.jobs[old]; j != nil {
			j.Cancel()
		}
		delete(m.jobs, old)
	}
	m.current = job
	m.mu.Unlock()

	if prev != nil {
		prev.Cancel()
	}

	args := append([]string{"-", "--stream", "--quiet"}, extraArgs...)
	cmd := exec.CommandContext(ctx, m.rtBin, args...)
	cmd.Dir = m.workDir
	cmd.Stdin = bytes.NewReader(sceneJSON)
	cmd.Cancel = func() error { return cmd.Process.Signal(syscall.SIGTERM) } // let rt finish cleanly
	cmd.WaitDelay = 5 * time.Second                                          // then SIGKILL
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		cancel()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		cancel()
		return nil, fmt.Errorf("cannot start renderer %q: %w", m.rtBin, err)
	}

	go func() {
		defer close(job.done)
		defer cancel()
		gotTerminal := false
		perr := ReadMessages(stdout, func(msg Message) {
			switch msg.Kind {
			case "frame":
				job.mu.Lock()
				job.seq++
				job.frame = msg.PNG
				job.header = msg.Frame
				seq := job.seq
				job.mu.Unlock()
				job.publish(Event{Type: "frame", Data: FrameEvent{JobID: id, Seq: seq, FrameHeader: msg.Frame}}, false)
			case "done":
				var st struct {
					Cancelled bool `json:"cancelled"`
				}
				_ = json.Unmarshal(msg.Raw, &st)
				job.mu.Lock()
				job.stats = msg.Raw
				if st.Cancelled {
					job.state = StateCancelled
				} else {
					job.state = StateDone
				}
				state := job.state
				job.mu.Unlock()
				gotTerminal = true
				job.publish(Event{Type: "done", Data: map[string]any{"job_id": id, "state": state, "stats": msg.Raw}}, true)
			case "error":
				job.mu.Lock()
				job.state = StateFailed
				job.errMsg = msg.ErrMsg
				job.mu.Unlock()
				gotTerminal = true
				job.publish(Event{Type: "error", Data: map[string]any{"job_id": id, "message": msg.ErrMsg}}, true)
			}
		})
		if perr != nil {
			_, _ = io.Copy(io.Discard, stdout)
		}
		werr := cmd.Wait()
		if !gotTerminal {
			msg := "renderer exited unexpectedly"
			var ee *exec.ExitError
			switch {
			case perr != nil:
				msg = perr.Error()
			case errors.As(werr, &ee):
				msg = fmt.Sprintf("renderer exited with %v: %s", werr, lastLine(stderr.String()))
			case werr != nil:
				msg = werr.Error()
			}
			job.mu.Lock()
			job.state = StateFailed
			job.errMsg = msg
			job.mu.Unlock()
			job.publish(Event{Type: "error", Data: map[string]any{"job_id": id, "message": msg}}, true)
		}
		log.Printf("job %d finished: %s", id, job.state)
	}()
	return job, nil
}

// Shutdown cancels everything and waits for the renderer processes to exit.
func (m *Manager) Shutdown() {
	m.mu.Lock()
	jobs := make([]*Job, 0, len(m.jobs))
	for _, j := range m.jobs {
		jobs = append(jobs, j)
	}
	m.mu.Unlock()
	for _, j := range jobs {
		j.Cancel()
		<-j.done
	}
}

func lastLine(s string) string {
	s = string(bytes.TrimSpace([]byte(s)))
	if i := bytes.LastIndexByte([]byte(s), '\n'); i >= 0 {
		return s[i+1:]
	}
	return s
}
