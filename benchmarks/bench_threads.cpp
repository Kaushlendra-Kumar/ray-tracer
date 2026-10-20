// Thread-scaling and tile-size benchmark.
// Usage: bench_threads <scene.json> [spp=8] [max_threads=hardware]
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "raytracer/core/scene.hpp"
#include "raytracer/io/scene_io.hpp"
#include "raytracer/rendering/renderer.hpp"

using namespace rt;

namespace {
double run(SceneDesc d, int w, int h, int threads, int tile, int spp, double* mrays) {
  d.render.width = w;
  d.render.height = h;
  d.render.threads = threads;
  d.render.tile_size = tile;
  d.render.spp = spp;
  const Scene s = Scene::compile(d);
  Renderer r(s, RenderOptions{false, 32});
  r.render();
  *mrays = r.stats().mrays_per_sec();
  return r.stats().elapsed_ms;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: bench_threads <scene.json> [spp] [max_threads]\n");
    return 1;
  }
  const SceneDesc d = load_scene(argv[1]);
  const int spp = argc > 2 ? std::atoi(argv[2]) : 8;
  const int hw = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  const int max_t = argc > 3 ? std::atoi(argv[3]) : hw;
  std::printf("hardware threads: %d, spp: %d, scene: %s\n\n", hw, spp, d.name.c_str());

  std::printf("### Thread scaling (tile 16)\n\n| resolution | threads | ms | speedup | Mrays/s |\n|---|---:|---:|---:|---:|\n");
  for (auto [w, h] : std::vector<std::pair<int, int>>{{320, 180}, {640, 360}}) {
    double base = 0;
    for (int t = 1; t <= max_t; t *= 2) {
      double mr = 0;
      const double ms = run(d, w, h, t, 16, spp, &mr);
      if (t == 1) base = ms;
      std::printf("| %dx%d | %d | %.0f | %.2fx | %.2f |\n", w, h, t, ms, base / ms, mr);
      std::fflush(stdout);
      if (t * 2 > max_t && t != max_t) { t = max_t / 2; }  // also measure max_t itself
    }
  }

  std::printf("\n### Tile size (640x360, %d threads)\n\n| tile | ms | Mrays/s |\n|---:|---:|---:|\n", max_t);
  for (int tile : {1, 4, 8, 16, 32, 64, 128}) {
    double mr = 0;
    const double ms = run(d, 640, 360, max_t, tile, spp, &mr);
    std::printf("| %d | %.0f | %.2f |\n", tile, ms, mr);
    std::fflush(stdout);
  }
  return 0;
}
