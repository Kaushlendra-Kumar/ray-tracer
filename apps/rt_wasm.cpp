// rt_wasm — WebAssembly front end for the rendering core, used by the
// browser editor when it runs without the Go backend (e.g. on GitHub Pages).
//
// The module is loaded inside a dedicated Web Worker
// (web/frontend/src/wasm/worker.ts), which calls the C functions below.
// Results come back through two JavaScript hooks on the Module object:
//   Module.onFrame(job, rgbaPixels, info)   after every progressive pass
//   Module.onDone(job, stats)               render finished or was cancelled
//
// Threaded build (-pthread, needs a cross-origin isolated page): a
// RenderSession renders on its own pthread and spawns the usual tile
// workers, so the Web Worker's event loop stays free to receive cancel
// requests and to service Emscripten's thread proxying. Frames reach the
// worker with MAIN_THREAD_ASYNC_EM_ASM.
// Single-threaded build: rt_start renders synchronously with one thread;
// the page cancels by terminating the Web Worker.
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>

#include "raytracer/core/scene.hpp"
#include "raytracer/io/scene_io.hpp"
#include "raytracer/rendering/renderer.hpp"

using namespace rt;

#ifdef __EMSCRIPTEN_PTHREADS__
#define RT_TO_WORKER MAIN_THREAD_ASYNC_EM_ASM
#else
#define RT_TO_WORKER EM_ASM
#endif

namespace {

std::string g_message;  // backing store for the strings returned to JS

struct Display {
  ToneMap op;
  Real exposure;
};

// Same choice as Renderer::image(): the normals view is shown unmapped.
Display display_of(const RenderSettings& s) {
  if (s.integrator == Integrator::Normals) return {ToneMap::Clamp, 1.0};
  return {s.tone_map, s.exposure};
}

int threads_for(int requested) {
#ifdef __EMSCRIPTEN_PTHREADS__
  // The pthread pool is sized to navigator.hardwareConcurrency.
  const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  return requested > 0 ? std::min(requested, hw) : hw;
#else
  (void)requested;
  return 1;
#endif
}

// Tone-maps the framebuffer into a malloc'd RGBA buffer (ready for an
// ImageData) whose ownership passes to JavaScript, which frees it.
void emit_frame(int job, const Framebuffer& fb, const RenderProgress& p, Display d) {
  const Image8 img = resolve(fb, d.op, d.exposure);
  const std::size_t n = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
  auto* rgba = static_cast<std::uint8_t*>(std::malloc(n * 4));
  if (!rgba) return;
  for (std::size_t i = 0; i < n; ++i) {
    rgba[4 * i + 0] = img.rgb[3 * i + 0];
    rgba[4 * i + 1] = img.rgb[3 * i + 1];
    rgba[4 * i + 2] = img.rgb[3 * i + 2];
    rgba[4 * i + 3] = 255;
  }
  RT_TO_WORKER({
    const pixels = HEAPU8.slice($1, $1 + $2 * $3 * 4);
    _free($1);
    Module["onFrame"]($0, pixels, {
      pass: $4, spp_done: $5, spp_target: $6, elapsed_ms: $7, rays: $8,
      width: $2, height: $3, finished: !!$9, cancelled: !!$10
    });
  }, job, rgba, img.width, img.height, p.pass, p.spp_done, p.spp_target, p.elapsed_ms,
     static_cast<double>(p.rays), p.finished ? 1 : 0, p.cancelled ? 1 : 0);
}

void emit_done(int job, const RenderProgress& p, const Renderer& r) {
  const RenderStatsSnapshot st = r.stats();
  RT_TO_WORKER({
    Module["onDone"]($0, {
      cancelled: !!$1, spp_done: $2, elapsed_ms: $3, rays: $4, shadow_rays: $5,
      mrays_per_sec: $6, invalid_samples: $7, threads: $8
    });
  }, job, p.cancelled ? 1 : 0, p.spp_done, st.elapsed_ms, static_cast<double>(st.rays),
     static_cast<double>(st.shadow_rays), st.mrays_per_sec(), static_cast<double>(st.invalid_samples),
     r.thread_count());
}

SceneDesc load(const char* json) {
  SceneDesc d = parse_scene(json);
  if (const auto errs = validate_scene(d); !errs.empty()) throw SceneError(errs.front());
  d.render.threads = threads_for(d.render.threads);
  return d;
}

#ifdef __EMSCRIPTEN_PTHREADS__
std::unique_ptr<RenderSession> g_session;
#endif

}  // namespace

extern "C" {

// "" if the scene parses and validates, otherwise the first problem found.
EMSCRIPTEN_KEEPALIVE const char* rt_validate(const char* json) {
  g_message.clear();
  try {
    load(json);
  } catch (const std::exception& e) {
    g_message = e.what();
  }
  return g_message.c_str();
}

// Reason for the last failed rt_start.
EMSCRIPTEN_KEEPALIVE const char* rt_message() { return g_message.c_str(); }

// Number of threads a render uses by default.
EMSCRIPTEN_KEEPALIVE int rt_threads() { return threads_for(0); }

EMSCRIPTEN_KEEPALIVE int rt_running() {
#ifdef __EMSCRIPTEN_PTHREADS__
  return g_session && g_session->running() ? 1 : 0;
#else
  return 0;
#endif
}

// Requests a stop; workers notice within one tile row and a final frame and
// onDone(cancelled) follow. Returns immediately.
EMSCRIPTEN_KEEPALIVE void rt_cancel() {
#ifdef __EMSCRIPTEN_PTHREADS__
  if (g_session) g_session->cancel();
#endif
}

// Starts render `job` (an id echoed back in every callback). max_pass caps the
// samples per progressive pass (0 = default). Returns 0 on success, 1 if the
// scene was rejected or a render is still running (reason in rt_message()).
EMSCRIPTEN_KEEPALIVE int rt_start(int job, const char* json, int max_pass) {
  g_message.clear();
  if (rt_running()) {
    g_message = "a render is still running";
    return 1;
  }
  try {
    const SceneDesc desc = load(json);
    Scene scene = Scene::compile(desc);
    const Display disp = display_of(scene.settings);
    RenderOptions opts;
    if (max_pass > 0) opts.max_pass_spp = max_pass;
#ifdef __EMSCRIPTEN_PTHREADS__
    g_session.reset();  // not running, so this joins immediately
    g_session = std::make_unique<RenderSession>(std::move(scene), opts);
    RenderSession* s = g_session.get();
    s->start([job, disp, s](const Framebuffer& fb, const RenderProgress& p) {
      emit_frame(job, fb, p, disp);
      if (p.finished || p.cancelled) emit_done(job, p, s->renderer());
    });
#else
    Renderer r(scene, opts);
    const RenderProgress fin = r.render([&](const Framebuffer& fb, const RenderProgress& p) { emit_frame(job, fb, p, disp); });
    emit_done(job, fin, r);
#endif
  } catch (const std::exception& e) {
    g_message = e.what();
    return 1;
  }
  return 0;
}

}  // extern "C"
