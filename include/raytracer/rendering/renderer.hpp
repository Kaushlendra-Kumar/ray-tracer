#pragma once
// Multithreaded progressive renderer.
//
// Work is organised in *passes*. Each pass adds `k` samples to every pixel;
// pass sizes grow 1, 1, 2, 4, 8, ... (capped) so a rough image appears almost
// immediately and refines afterwards. Inside a pass the image is cut into
// square tiles that worker threads claim from a shared atomic counter
// (dynamic scheduling balances uneven tile costs, e.g. glass vs sky).
//
// Determinism: the RNG for sample s of pixel p is seeded from (seed, p, s),
// so the final image is bit-identical for any thread count, tile size, or
// pass schedule. This is tested.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "raytracer/core/scene.hpp"
#include "raytracer/rendering/framebuffer.hpp"

namespace rt {

struct RenderProgress {
  int pass = 0;                   // 1-based index of the completed pass
  std::uint32_t spp_done = 0;     // samples every pixel has received
  int spp_target = 0;
  double elapsed_ms = 0;
  std::uint64_t rays = 0;         // closest-hit + shadow rays so far
  bool finished = false;          // all samples done
  bool cancelled = false;
};

struct RenderStatsSnapshot {
  std::uint64_t camera_samples = 0, rays = 0, shadow_rays = 0, rr_terminated = 0, invalid_samples = 0, tiles = 0;
  double elapsed_ms = 0;
  double mrays_per_sec() const { return elapsed_ms > 0 ? (double(rays + shadow_rays) / 1e3) / elapsed_ms : 0; }
};

struct RenderOptions {
  bool progressive = true;
  int max_pass_spp = 32;   // cap on samples per progressive pass
};

// Called on the coordinating thread *between* passes, when no worker is
// running: the framebuffer may be read without synchronisation.
using ProgressFn = std::function<void(const Framebuffer&, const RenderProgress&)>;

class Renderer {
 public:
  Renderer(const Scene& scene, RenderOptions opts = {});

  // Blocking render. Returns the final progress record. `cancel` may be set
  // from any thread; workers notice it within one tile row.
  RenderProgress render(const ProgressFn& on_pass = {}, const std::atomic<bool>* cancel = nullptr);

  const Framebuffer& framebuffer() const { return fb_; }
  Image8 image() const;  // tone-mapped 8-bit result
  RenderStatsSnapshot stats() const;
  int thread_count() const { return threads_; }

 private:
  void render_tile(int tile, std::uint32_t s0, std::uint32_t s1, const std::atomic<bool>* cancel);

  const Scene& scene_;
  RenderOptions opts_;
  Framebuffer fb_;
  int threads_ = 1;
  int tiles_x_ = 0, tiles_y_ = 0;

  // Shared counters. Each sits on its own cache line so that workers flushing
  // their per-tile totals do not false-share with each other.
  struct alignas(64) Counter { std::atomic<std::uint64_t> v{0}; };
  Counter camera_samples_, rays_, shadow_rays_, rr_terminated_, invalid_, tiles_done_;
  std::atomic<double> elapsed_ms_{0};
};

// Asynchronous render job that owns its scene (RAII: the destructor cancels
// and joins). Used by the CLI's streaming mode and the cancellation tests.
class RenderSession {
 public:
  RenderSession(Scene scene, RenderOptions opts = {});
  ~RenderSession();
  RenderSession(const RenderSession&) = delete;
  RenderSession& operator=(const RenderSession&) = delete;

  void start(ProgressFn on_pass = {});
  void cancel();               // request stop; returns immediately
  RenderProgress wait();       // join; returns the last progress record
  void restart(Scene scene, ProgressFn on_pass = {});
  bool running() const { return running_.load(); }
  const Renderer& renderer() const { return *renderer_; }

 private:
  Scene scene_;
  RenderOptions opts_;
  std::unique_ptr<Renderer> renderer_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> running_{false};
  RenderProgress last_;
  std::jthread thread_;
};

}  // namespace rt
