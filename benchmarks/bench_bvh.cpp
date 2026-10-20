// BVH vs brute force on scenes of increasing size.
// Usage: bench_bvh [max_subdivisions=6] [brute_limit=20000]
//
// Each scene is an icosphere of 20*4^s triangles plus a floor. For each we
// trace the same 256x256 primary rays plus one diffuse bounce per pixel, and
// report build time, trace time, rays/s and primitive tests per ray.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "raytracer/core/scene.hpp"
#include "raytracer/math/sampling.hpp"

using namespace rt;
using Clock = std::chrono::steady_clock;

namespace {

struct Result {
  double ms = 0;
  std::uint64_t rays = 0;
  TraversalCounters ctr;
  std::uint64_t hits = 0;
};

Result trace(const Scene& scene, bool brute) {
  Result res;
  CameraDesc cd;
  cd.look_from = {0, 1.5, 4};
  cd.look_at = {0, 1, 0};
  Camera cam(cd, 256, 256);
  const auto t0 = Clock::now();
  for (int y = 0; y < 256; ++y)
    for (int x = 0; x < 256; ++x) {
      Rng rng = Rng::for_sample(1, static_cast<std::uint64_t>(y * 256 + x), 0);
      Ray r = cam.generate(x, y, 0.5, 0.5, rng);
      for (int bounce = 0; bounce < 2; ++bounce) {
        HitRecord h;
        res.rays++;
        const bool hit = brute ? scene.intersect_brute(r, Interval(1e-6, kInfinity), h, &res.ctr)
                               : scene.intersect(r, Interval(1e-6, kInfinity), h, &res.ctr);
        if (!hit) break;
        res.hits++;
        const Vec3 d = Onb(h.normal).to_world(cosine_hemisphere(rng));
        r = Ray(offset_ray_origin(h.p, h.geometric_normal, d), d);
      }
    }
  res.ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  return res;
}

}  // namespace

int main(int argc, char** argv) {
  const int max_s = argc > 1 ? std::atoi(argv[1]) : 6;
  const std::size_t brute_limit = argc > 2 ? static_cast<std::size_t>(std::atoll(argv[2])) : 20000;
  std::printf("| triangles | BVH nodes | depth | build ms | BVH trace ms | BVH Mrays/s | nodes/ray | tests/ray (BVH) | brute trace ms | brute Mrays/s | tests/ray (brute) | speedup | results equal |\n");
  std::printf("|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|\n");
  for (int s = 0; s <= max_s; ++s) {
    SceneDesc d;
    d.materials.push_back({"m", Lambertian{}});
    MeshDesc m;
    m.subdivisions = s;
    m.translate = {0, 1, 0};
    d.objects.push_back({"ico", "m", m});
    d.objects.push_back({"floor", "m", QuadDesc{{-5, 0, -5}, {10, 0, 0}, {0, 0, 10}}});
    const Scene scene = Scene::compile(d);
    const Result b = trace(scene, false);
    const auto& st = scene.info.bvh;
    const double tests_bvh = double(b.ctr.prim_tests) / double(b.rays);
    const double nodes_bvh = double(b.ctr.node_visits) / double(b.rays);
    if (scene.primitives.size() <= brute_limit) {
      const Result f = trace(scene, true);
      std::printf("| %zu | %zu | %zu | %.2f | %.1f | %.2f | %.1f | %.1f | %.1f | %.3f | %.0f | %.1fx | %s |\n", scene.primitives.size(),
                  st.nodes, st.max_depth, st.build_ms, b.ms, double(b.rays) / b.ms / 1e3, nodes_bvh, tests_bvh, f.ms, double(f.rays) / f.ms / 1e3,
                  double(f.ctr.prim_tests) / double(f.rays), f.ms / b.ms, (f.hits == b.hits ? "yes" : "NO"));
    } else {
      std::printf("| %zu | %zu | %zu | %.2f | %.1f | %.2f | %.1f | %.1f | skipped | - | - | - | - |\n", scene.primitives.size(), st.nodes,
                  st.max_depth, st.build_ms, b.ms, double(b.rays) / b.ms / 1e3, nodes_bvh, tests_bvh);
    }
    std::fflush(stdout);
  }
  return 0;
}
