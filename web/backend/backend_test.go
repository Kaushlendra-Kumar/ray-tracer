package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestReadMessages(t *testing.T) {
	var buf bytes.Buffer
	buf.WriteString(`FRAME {"pass":1,"spp_done":1,"spp_target":4,"width":2,"height":2,"bytes":5}` + "\n")
	buf.WriteString("\x89PNG\n")
	buf.WriteString(`FRAME {"pass":2,"spp_done":2,"spp_target":4,"width":2,"height":2,"bytes":3}` + "\n")
	buf.WriteString("abc")
	buf.WriteString(`DONE {"cancelled":false}` + "\n")
	var got []Message
	if err := ReadMessages(&buf, func(m Message) { got = append(got, m) }); err != nil {
		t.Fatal(err)
	}
	if len(got) != 3 || got[0].Kind != "frame" || string(got[0].PNG) != "\x89PNG\n" || got[1].Frame.SppDone != 2 ||
		string(got[1].PNG) != "abc" || got[2].Kind != "done" {
		t.Fatalf("unexpected messages: %+v", got)
	}
}

func TestReadMessagesErrors(t *testing.T) {
	for _, in := range []string{
		"FRAME {not json}\n",
		`FRAME {"bytes":10}` + "\nshort",
		`FRAME {"bytes":-1}` + "\n",
		"GARBAGE line\n",
	} {
		if err := ReadMessages(strings.NewReader(in), func(Message) {}); err == nil {
			t.Errorf("expected error for %q", in)
		}
	}
	var msg Message
	if err := ReadMessages(strings.NewReader(`ERROR {"message":"bad scene"}`+"\n"), func(m Message) { msg = m }); err != nil {
		t.Fatal(err)
	}
	if msg.Kind != "error" || msg.ErrMsg != "bad scene" {
		t.Fatalf("got %+v", msg)
	}
}

func TestReadSceneRejectsEscapingPaths(t *testing.T) {
	for _, p := range []string{"/etc/passwd", "../secret.obj", "a/../../x.obj"} {
		body := `{"scene":{"objects":[{"type":"mesh","source":"obj","path":"` + p + `"}]}}`
		r := httptest.NewRequest("POST", "/api/render", strings.NewReader(body))
		if _, err := readScene(r); err == nil {
			t.Errorf("path %q accepted", p)
		}
	}
	r := httptest.NewRequest("POST", "/api/render", strings.NewReader(`{"scene":"nope"}`))
	if _, err := readScene(r); err == nil {
		t.Error("non-object scene accepted")
	}
}

// --- Integration tests against the real renderer (skipped if not built). ---

func rtBinary(t *testing.T) (string, string) {
	root, _ := filepath.Abs("../..")
	bin := filepath.Join(root, "build", "rt")
	if b := os.Getenv("RT_BIN"); b != "" {
		bin = b
	}
	if _, err := os.Stat(bin); err != nil {
		t.Skip("rt binary not built; run cmake --build build first")
	}
	return bin, filepath.Join(root, "assets", "scenes")
}

type sse struct {
	event string
	data  string
}

func readSSE(t *testing.T, url string, until string, timeout time.Duration) []sse {
	t.Helper()
	client := &http.Client{Timeout: timeout}
	resp, err := client.Get(url)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var out []sse
	sc := bufio.NewScanner(resp.Body)
	var cur sse
	for sc.Scan() {
		line := sc.Text()
		switch {
		case strings.HasPrefix(line, "event: "):
			cur.event = strings.TrimPrefix(line, "event: ")
		case strings.HasPrefix(line, "data: "):
			cur.data = strings.TrimPrefix(line, "data: ")
		case line == "" && cur.event != "":
			out = append(out, cur)
			if cur.event == until || cur.event == "error" {
				return out
			}
			cur = sse{}
		}
	}
	return out
}

func startJob(t *testing.T, ts *httptest.Server, scene string) int64 {
	t.Helper()
	resp, err := http.Post(ts.URL+"/api/render", "application/json", strings.NewReader(`{"scene":`+scene+`}`))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var r struct {
		JobID int64  `json:"job_id"`
		Error string `json:"error"`
	}
	_ = json.NewDecoder(resp.Body).Decode(&r)
	if resp.StatusCode != http.StatusAccepted {
		t.Fatalf("start failed: %d %s", resp.StatusCode, r.Error)
	}
	return r.JobID
}

func TestRenderEndToEnd(t *testing.T) {
	bin, scenes := rtBinary(t)
	s := &server{mgr: NewManager(bin, scenes), rtBin: bin, sceneDir: scenes}
	ts := httptest.NewServer(s.routes())
	defer ts.Close()
	defer s.mgr.Shutdown()

	scene := `{"render":{"width":32,"height":24,"spp":8},"materials":{"m":{"type":"lambertian"}},` +
		`"objects":[{"type":"sphere","center":[0,0,-3],"radius":1,"material":"m"}]}`
	id := startJob(t, ts, scene)
	events := readSSE(t, ts.URL+"/api/render/"+itoa(id)+"/events", "done", 30*time.Second)
	frames := 0
	for _, e := range events {
		if e.event == "frame" {
			frames++
		}
	}
	if frames == 0 || events[len(events)-1].event != "done" {
		t.Fatalf("events: %+v", events)
	}
	resp, err := http.Get(ts.URL + "/api/render/" + itoa(id) + "/frame")
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	var png bytes.Buffer
	_, _ = png.ReadFrom(resp.Body)
	if resp.Header.Get("Content-Type") != "image/png" || !bytes.HasPrefix(png.Bytes(), []byte("\x89PNG")) {
		t.Fatalf("frame is not a PNG (%d bytes)", png.Len())
	}
}

func TestInvalidSceneReportsError(t *testing.T) {
	bin, scenes := rtBinary(t)
	s := &server{mgr: NewManager(bin, scenes), rtBin: bin, sceneDir: scenes}
	ts := httptest.NewServer(s.routes())
	defer ts.Close()
	defer s.mgr.Shutdown()
	id := startJob(t, ts, `{"render":{"width":-5}}`)
	events := readSSE(t, ts.URL+"/api/render/"+itoa(id)+"/events", "done", 10*time.Second)
	if len(events) == 0 || events[len(events)-1].event != "error" || !strings.Contains(events[len(events)-1].data, "render.width") {
		t.Fatalf("events: %+v", events)
	}
	resp, _ := http.Post(ts.URL+"/api/validate", "application/json", strings.NewReader(`{"scene":{"camera":{"vfov":500}}}`))
	var v struct {
		OK    bool   `json:"ok"`
		Error string `json:"error"`
	}
	_ = json.NewDecoder(resp.Body).Decode(&v)
	resp.Body.Close()
	if v.OK || !strings.Contains(v.Error, "camera.vfov") {
		t.Fatalf("validate: %+v", v)
	}
}

func TestNewJobCancelsPrevious(t *testing.T) {
	bin, scenes := rtBinary(t)
	s := &server{mgr: NewManager(bin, scenes), rtBin: bin, sceneDir: scenes}
	ts := httptest.NewServer(s.routes())
	defer ts.Close()
	defer s.mgr.Shutdown()
	long := `{"render":{"width":64,"height":64,"spp":1000000},"materials":{"m":{"type":"lambertian"}},` +
		`"objects":[{"type":"sphere","center":[0,0,-3],"radius":1,"material":"m"}]}`
	first := startJob(t, ts, long)
	time.Sleep(300 * time.Millisecond)
	second := startJob(t, ts, long)
	if second <= first {
		t.Fatalf("job ids must increase: %d then %d", first, second)
	}
	events := readSSE(t, ts.URL+"/api/render/"+itoa(first)+"/events", "done", 10*time.Second)
	last := events[len(events)-1]
	if last.event != "done" || !strings.Contains(last.data, `"cancelled"`) {
		t.Fatalf("first job not cancelled: %+v", last)
	}
	// Explicit cancel of the second.
	resp, _ := http.Post(ts.URL+"/api/render/"+itoa(second)+"/cancel", "application/json", nil)
	resp.Body.Close()
	events = readSSE(t, ts.URL+"/api/render/"+itoa(second)+"/events", "done", 10*time.Second)
	if events[len(events)-1].event != "done" {
		t.Fatalf("second job did not finish: %+v", events)
	}
}

func itoa(i int64) string { return strconv.FormatInt(i, 10) }
