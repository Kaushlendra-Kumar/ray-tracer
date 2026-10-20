// Command editor-backend is a small local HTTP server that sits between the
// browser scene editor and the C++ renderer. It never renders anything
// itself: it validates requests, runs `rt --stream`, and relays progressive
// frames to the browser over Server-Sent Events.
//
//	GET  /api/health                   renderer binary status
//	GET  /api/scenes                   names of the example scenes
//	GET  /api/scenes/{name}            one example scene (JSON)
//	POST /api/validate                 {"scene": {...}} -> {"ok": bool, "error": "..."}
//	POST /api/render                   {"scene": {...}, "preview": bool} -> {"job_id": n}
//	GET  /api/render/{id}/events       SSE: frame / done / error events
//	GET  /api/render/{id}/frame        latest PNG frame of the job
//	POST /api/render/{id}/cancel       stop the job
package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"
)

const maxBody = 4 << 20 // scenes are small; refuse anything bigger

type server struct {
	mgr       *Manager
	rtBin     string
	sceneDir  string
	staticDir string
}

func main() {
	root := findRepoRoot()
	addr := flag.String("addr", "127.0.0.1:8080", "listen address (loopback by default: this is a local tool)")
	rtBin := flag.String("rt", filepath.Join(root, "build", "rt"), "path to the rt renderer binary")
	scenes := flag.String("scenes", filepath.Join(root, "assets", "scenes"), "directory of example scenes")
	static := flag.String("static", filepath.Join(root, "web", "frontend", "dist"), "built frontend to serve (optional)")
	flag.Parse()

	s := &server{mgr: NewManager(*rtBin, *scenes), rtBin: *rtBin, sceneDir: *scenes, staticDir: *static}
	srv := &http.Server{Addr: *addr, Handler: s.routes(), ReadHeaderTimeout: 10 * time.Second}

	go func() {
		log.Printf("editor backend on http://%s (renderer: %s)", *addr, *rtBin)
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Fatal(err)
		}
	}()
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	<-stop
	log.Print("shutting down: cancelling renders")
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_ = srv.Shutdown(ctx)
	s.mgr.Shutdown()
}

func findRepoRoot() string {
	dir, _ := os.Getwd()
	for d := dir; ; d = filepath.Dir(d) {
		if _, err := os.Stat(filepath.Join(d, "CMakeLists.txt")); err == nil {
			if _, err := os.Stat(filepath.Join(d, "assets", "scenes")); err == nil {
				return d
			}
		}
		if filepath.Dir(d) == d {
			return dir
		}
	}
}

func (s *server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/health", s.health)
	mux.HandleFunc("GET /api/scenes", s.listScenes)
	mux.HandleFunc("GET /api/scenes/{name}", s.getScene)
	mux.HandleFunc("POST /api/validate", s.validate)
	mux.HandleFunc("POST /api/render", s.startRender)
	mux.HandleFunc("GET /api/render/{id}/events", s.events)
	mux.HandleFunc("GET /api/render/{id}/frame", s.frame)
	mux.HandleFunc("POST /api/render/{id}/cancel", s.cancel)
	if st, err := os.Stat(s.staticDir); err == nil && st.IsDir() {
		mux.Handle("/", http.FileServer(http.Dir(s.staticDir)))
	}
	return logRequests(mux)
}

func logRequests(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasSuffix(r.URL.Path, "/frame") {
			log.Printf("%s %s", r.Method, r.URL.Path)
		}
		h.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, map[string]string{"error": msg})
}

func (s *server) health(w http.ResponseWriter, _ *http.Request) {
	_, err := os.Stat(s.rtBin)
	writeJSON(w, http.StatusOK, map[string]any{"renderer": s.rtBin, "renderer_found": err == nil})
}

func (s *server) listScenes(w http.ResponseWriter, _ *http.Request) {
	entries, err := os.ReadDir(s.sceneDir)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	names := []string{}
	for _, e := range entries {
		if strings.HasSuffix(e.Name(), ".json") {
			names = append(names, strings.TrimSuffix(e.Name(), ".json"))
		}
	}
	sort.Strings(names)
	writeJSON(w, http.StatusOK, names)
}

func validSceneName(n string) bool {
	if n == "" || len(n) > 64 {
		return false
	}
	for _, c := range n {
		if !(c == '_' || c == '-' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
			return false
		}
	}
	return true
}

func (s *server) getScene(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	if !validSceneName(name) {
		writeErr(w, http.StatusBadRequest, "invalid scene name")
		return
	}
	b, err := os.ReadFile(filepath.Join(s.sceneDir, name+".json"))
	if err != nil {
		writeErr(w, http.StatusNotFound, "no such scene")
		return
	}
	w.Header().Set("Content-Type", "application/json")
	_, _ = w.Write(b)
}

type renderRequest struct {
	Scene   json.RawMessage `json:"scene"`
	Preview bool            `json:"preview"`
}

// readScene decodes the request and applies server-side safety checks that
// the C++ validator does not know about (it trusts its local user).
func readScene(r *http.Request) (renderRequest, error) {
	var req renderRequest
	body, err := io.ReadAll(io.LimitReader(r.Body, maxBody+1))
	if err != nil {
		return req, err
	}
	if len(body) > maxBody {
		return req, errors.New("scene too large")
	}
	if err := json.Unmarshal(body, &req); err != nil {
		return req, fmt.Errorf("invalid request JSON: %w", err)
	}
	var scene map[string]any
	if err := json.Unmarshal(req.Scene, &scene); err != nil || scene == nil {
		return req, errors.New("scene must be a JSON object")
	}
	if objs, ok := scene["objects"].([]any); ok {
		for i, o := range objs {
			obj, _ := o.(map[string]any)
			if p, ok := obj["path"].(string); ok && p != "" {
				// OBJ meshes may only reference files inside the scenes folder.
				clean := filepath.Clean(p)
				if filepath.IsAbs(clean) || strings.HasPrefix(clean, "..") {
					return req, fmt.Errorf("objects[%d].path: must be a relative path inside the scenes folder", i)
				}
			}
		}
	}
	return req, nil
}

func (s *server) validate(w http.ResponseWriter, r *http.Request) {
	req, err := readScene(r)
	if err != nil {
		writeJSON(w, http.StatusOK, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, s.rtBin, "-", "--validate")
	cmd.Dir = s.sceneDir
	cmd.Stdin = bytes.NewReader(req.Scene)
	out, err := cmd.CombinedOutput()
	if err != nil {
		msg := strings.TrimSpace(string(out))
		msg = strings.TrimPrefix(msg, "error: ")
		writeJSON(w, http.StatusOK, map[string]any{"ok": false, "error": msg})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}

func (s *server) startRender(w http.ResponseWriter, r *http.Request) {
	req, err := readScene(r)
	if err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	var extra []string
	if req.Preview {
		// Fast feedback while the user drags the camera: few samples,
		// shallow paths, one progressive pass per sample.
		extra = append(extra, "--max-pass", "1")
	}
	job, err := s.mgr.Start(req.Scene, req.Preview, extra)
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{"job_id": job.ID})
}

func (s *server) jobFromPath(w http.ResponseWriter, r *http.Request) *Job {
	id, err := strconv.ParseInt(r.PathValue("id"), 10, 64)
	if err != nil {
		writeErr(w, http.StatusBadRequest, "invalid job id")
		return nil
	}
	job := s.mgr.Get(id)
	if job == nil {
		writeErr(w, http.StatusNotFound, "no such job")
	}
	return job
}

func (s *server) events(w http.ResponseWriter, r *http.Request) {
	job := s.jobFromPath(w, r)
	if job == nil {
		return
	}
	flusher, ok := w.(http.Flusher)
	if !ok {
		writeErr(w, http.StatusInternalServerError, "streaming unsupported")
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Connection", "keep-alive")
	ch, unsubscribe := job.Subscribe()
	defer unsubscribe()
	keepalive := time.NewTicker(15 * time.Second)
	defer keepalive.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-keepalive.C:
			_, _ = io.WriteString(w, ": keepalive\n\n")
			flusher.Flush()
		case ev, ok := <-ch:
			if !ok {
				return
			}
			data, _ := json.Marshal(ev.Data)
			fmt.Fprintf(w, "event: %s\ndata: %s\n\n", ev.Type, data)
			flusher.Flush()
		}
	}
}

func (s *server) frame(w http.ResponseWriter, r *http.Request) {
	job := s.jobFromPath(w, r)
	if job == nil {
		return
	}
	_, png, seq := job.snapshot()
	if png == nil {
		writeErr(w, http.StatusNotFound, "no frame yet")
		return
	}
	w.Header().Set("Content-Type", "image/png")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Frame-Seq", strconv.Itoa(seq))
	_, _ = w.Write(png)
}

func (s *server) cancel(w http.ResponseWriter, r *http.Request) {
	job := s.jobFromPath(w, r)
	if job == nil {
		return
	}
	job.Cancel()
	writeJSON(w, http.StatusOK, map[string]any{"job_id": job.ID, "cancelling": true})
}
