#include "raytracer/rendering/renderer.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#include "raytracer/rendering/integrator.hpp"

namespace rt {

Renderer::Renderer(const Scene& scene, RenderOptions opts) : scene_(scene), opts_(opts) {
  const auto& s = scene_.settings;
  fb_.resize(s.width, s.height);
  const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
  threads_ = s.threads > 0 ? s.threads : static_cast<int>(hw);
  const int ts = std::max(1, s.tile_size);
  tiles_x_ = (s.width + ts - 1) / ts;
  tiles_y_ = (s.height + ts - 1) / ts;
}

void Renderer::render_tile(int tile, std::uint32_t s0, std::uint32_t s1, const std::atomic<bool>* cancel) {
  const auto& cfg = scene_.settings;
  const int ts = std::max(1, cfg.tile_size);
  const int x0 = (tile % tiles_x_) * ts, y0 = (tile / tiles_x_) * ts;
  const int x1 = std::min(x0 + ts, cfg.width), y1 = std::min(y0 + ts, cfg.height);
  PathStats st;
  std::uint64_t samples = 0;
  for (int y = y0; y < y1; ++y) {
    if (cancel && cancel->load(std::memory_order_relaxed)) break;
    for (int x = x0; x < x1; ++x) {
      const auto pixel = static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(cfg.width) + static_cast<std::uint64_t>(x);
      for (std::uint32_t s = s0; s < s1; ++s) {
        Rng rng = Rng::for_sample(cfg.seed, pixel, s);
        // Box-filtered anti-aliasing: jitter uniformly inside the pixel.
        const Real jx = rng.uniform();
        const Real jy = rng.uniform();
        const Ray r = scene_.camera.generate(x, y, jx, jy, rng);
        Color L = radiance(scene_, r, rng, st);
        if (!L.is_finite() || L.x < 0 || L.y < 0 || L.z < 0) {
          // A NaN/Inf would poison the pixel forever. Recording it as black
          // is a (tiny, counted) bias; `invalid_samples` makes it visible.
          st.invalid_samples++;
          L = Color{};
        }
        fb_.add(x, y, L);
        ++samples;
      }
    }
  }
  camera_samples_.v.fetch_add(samples, std::memory_order_relaxed);
  rays_.v.fetch_add(st.rays, std::memory_order_relaxed);
  shadow_rays_.v.fetch_add(st.shadow_rays, std::memory_order_relaxed);
  rr_terminated_.v.fetch_add(st.rr_terminated, std::memory_order_relaxed);
  invalid_.v.fetch_add(st.invalid_samples, std::memory_order_relaxed);
  tiles_done_.v.fetch_add(1, std::memory_order_relaxed);
}

RenderProgress Renderer::render(const ProgressFn& on_pass, const std::atomic<bool>* cancel) {
  const auto t0 = std::chrono::steady_clock::now();
  const auto target = static_cast<std::uint32_t>(scene_.settings.spp);
  const int ntiles = tiles_x_ * tiles_y_;
  RenderProgress prog;
  prog.spp_target = scene_.settings.spp;

  std::uint32_t done = 0;
  while (done < target) {
    std::uint32_t k = target - done;
    if (opts_.progressive) k = std::min({k, std::max<std::uint32_t>(1, done), static_cast<std::uint32_t>(std::max(1, opts_.max_pass_spp))});
    const std::uint32_t s0 = done, s1 = done + k;

    std::atomic<int> next{0};
    auto worker = [&] {
      for (;;) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return;
        const int t = next.fetch_add(1, std::memory_order_relaxed);
        if (t >= ntiles) return;
        render_tile(t, s0, s1, cancel);
      }
    };
    {
      // jthread joins in its destructor: leaving this scope is the pass barrier.
      std::vector<std::jthread> pool;
      const int n = std::min(threads_, ntiles);
      pool.reserve(static_cast<std::size_t>(n));
      for (int i = 1; i < n; ++i) pool.emplace_back(worker);
      worker();  // the calling thread works too
    }

    prog.pass++;
    prog.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    elapsed_ms_.store(prog.elapsed_ms);
    prog.rays = rays_.v.load() + shadow_rays_.v.load();
    if (cancel && cancel->load()) {
      prog.cancelled = true;
      prog.spp_done = fb_.min_samples();
      if (on_pass) on_pass(fb_, prog);
      return prog;
    }
    done = s1;
    prog.spp_done = done;
    prog.finished = done >= target;
    if (on_pass) on_pass(fb_, prog);
  }
  return prog;
}

Image8 Renderer::image() const {
  if (scene_.settings.integrator == Integrator::Normals) return resolve(fb_, ToneMap::Clamp, 1.0);
  return resolve(fb_, scene_.settings.tone_map, scene_.settings.exposure);
}

RenderStatsSnapshot Renderer::stats() const {
  RenderStatsSnapshot s;
  s.camera_samples = camera_samples_.v.load();
  s.rays = rays_.v.load();
  s.shadow_rays = shadow_rays_.v.load();
  s.rr_terminated = rr_terminated_.v.load();
  s.invalid_samples = invalid_.v.load();
  s.tiles = tiles_done_.v.load();
  s.elapsed_ms = elapsed_ms_.load();
  return s;
}

// ---------------------------------------------------------------------------
RenderSession::RenderSession(Scene scene, RenderOptions opts)
    : scene_(std::move(scene)), opts_(opts), renderer_(std::make_unique<Renderer>(scene_, opts_)) {}

RenderSession::~RenderSession() {
  cancel();
  wait();
}

void RenderSession::start(ProgressFn on_pass) {
  wait();  // never run two jobs on one session
  cancel_.store(false);
  running_.store(true);
  renderer_ = std::make_unique<Renderer>(scene_, opts_);
  thread_ = std::jthread([this, cb = std::move(on_pass)] {
    last_ = renderer_->render(cb, &cancel_);
    running_.store(false);
  });
}

void RenderSession::cancel() { cancel_.store(true); }

RenderProgress RenderSession::wait() {
  if (thread_.joinable()) thread_.join();
  return last_;
}

void RenderSession::restart(Scene scene, ProgressFn on_pass) {
  cancel();
  wait();
  scene_ = std::move(scene);
  start(std::move(on_pass));
}

}  // namespace rt
