#include <catch2/catch_amalgamated.hpp>

#include "raytracer/core/scene.hpp"
#include "raytracer/math/sampling.hpp"

using namespace rt;
using Catch::Approx;

namespace {

SceneDesc random_scene(int spheres, int triangles, std::uint64_t seed, bool with_mesh) {
  SceneDesc d;
  d.materials.push_back({"m", Lambertian{}});
  Rng rng(seed);
  auto rp = [&](Real s) { return Vec3{rng.uniform(-s, s), rng.uniform(-s, s), rng.uniform(-s, s)}; };
  for (int i = 0; i < spheres; ++i) d.objects.push_back({"s", "m", SphereDesc{rp(10), rng.uniform(0.05, 1.0)}});
  for (int i = 0; i < triangles; ++i) {
    const Vec3 c = rp(10);
    d.objects.push_back({"t", "m", TriangleDesc{c + rp(1), c + rp(1), c + rp(1)}});
  }
  if (with_mesh) {
    MeshDesc m;
    m.source = MeshDesc::Source::Icosphere;
    m.subdivisions = 3;
    m.scale = {3, 3, 3};
    d.objects.push_back({"mesh", "m", m});
  }
  d.objects.push_back({"floor", "m", PlaneDesc{{0, -12, 0}, {0, 1, 0}}});
  return d;
}

}  // namespace

TEST_CASE("BVH equals brute force on random scenes", "[bvh]") {
  for (std::uint64_t seed : {1u, 2u, 3u}) {
    const Scene scene = Scene::compile(random_scene(300, 600, seed, seed == 2));
    REQUIRE(scene.bvh.validate([&] {
      std::vector<AABB> b;
      for (const auto& p : scene.primitives) b.push_back(std::visit([](const auto& s) { return s.bounds(); }, p));
      return b;
    }()));
    Rng rng(seed * 100);
    int hits = 0;
    for (int i = 0; i < 5000; ++i) {
      Vec3 o{rng.uniform(-15, 15), rng.uniform(-15, 15), rng.uniform(-15, 15)};
      Vec3 dir = random_unit_vector(rng);
      if (i % 50 == 0) dir = Vec3{0, 0, i % 100 ? 1.0 : -1.0};  // axis-parallel rays (inf in inv_dir)
      const Ray r{o, dir};
      HitRecord a, b;
      const bool ha = scene.intersect(r, Interval(1e-6, kInfinity), a);
      const bool hb = scene.intersect_brute(r, Interval(1e-6, kInfinity), b);
      REQUIRE(ha == hb);
      if (ha) {
        ++hits;
        REQUIRE(a.t == Approx(b.t).epsilon(1e-12));
        REQUIRE((a.p - b.p).length() < 1e-9);
      }
      // Any-hit must agree with closest-hit for a few distances.
      for (Real tmax : {0.5, 5.0, 50.0}) REQUIRE(scene.occluded(r, tmax) == (ha && a.t < tmax));
    }
    CHECK(hits > 1000);  // the test actually exercises hits
  }
}

TEST_CASE("BVH reduces intersection work", "[bvh]") {
  const Scene scene = Scene::compile(random_scene(2000, 2000, 9, false));
  Rng rng(4);
  TraversalCounters bvh_ctr, brute_ctr;
  for (int i = 0; i < 2000; ++i) {
    const Ray r{{0, 0, -30}, normalize(Vec3{rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3), 1})};
    HitRecord h;
    scene.intersect(r, Interval(1e-6, kInfinity), h, &bvh_ctr);
    scene.intersect_brute(r, Interval(1e-6, kInfinity), h, &brute_ctr);
  }
  INFO("bvh prim tests " << bvh_ctr.prim_tests << " vs brute " << brute_ctr.prim_tests);
  CHECK(bvh_ctr.prim_tests * 20 < brute_ctr.prim_tests);
}

TEST_CASE("BVH edge cases", "[bvh]") {
  SECTION("empty") {
    Bvh b;
    b.build({});
    CHECK(b.empty());
    CHECK(b.closest(Ray{{0, 0, 0}, {0, 0, 1}}, 0, 10, [](std::uint32_t, Real t) { return t; }) == 10);
    CHECK_FALSE(b.any(Ray{{0, 0, 0}, {0, 0, 1}}, 0, 10, [](std::uint32_t, Real) { return 0.0; }));
  }
  SECTION("single primitive") {
    Bvh b;
    b.build({AABB{{-1, -1, -1}, {1, 1, 1}}});
    CHECK(b.nodes().size() == 1);
    CHECK(b.nodes()[0].is_leaf());
  }
  SECTION("many identical primitives (no SAH split possible)") {
    std::vector<AABB> boxes(1000, AABB{{0, 0, 0}, {1, 1, 1}});
    Bvh b;
    b.build(boxes, 4);
    CHECK(b.validate(boxes));
    CHECK(b.stats().max_leaf_size <= 4);
    int count = 0;
    b.closest(Ray{{0.5, 0.5, -5}, {0, 0, 1}}, 0, kInfinity, [&](std::uint32_t, Real tm) { ++count; return tm; });
    CHECK(count == 1000);  // every primitive is reachable
  }
  SECTION("empty bounds are skipped") {
    std::vector<AABB> boxes{AABB{{0, 0, 0}, {1, 1, 1}}, AABB{}, AABB{{2, 0, 0}, {3, 1, 1}}};
    Bvh b;
    b.build(boxes);
    CHECK(b.prim_indices().size() == 2);
    CHECK(b.validate(boxes));
  }
  SECTION("degenerate triangles are dropped at scene compile time") {
    SceneDesc d;
    d.materials.push_back({"m", Lambertian{}});
    d.objects.push_back({"ok", "m", TriangleDesc{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}});
    MeshDesc m;  // an icosphere squashed to ~zero thickness: faces seen edge-on collapse
    m.scale = {1, 1e-30, 1};
    d.objects.push_back({"flat", "m", m});
    const Scene s = Scene::compile(d);
    CHECK(s.info.degenerate_skipped > 0);
    for (const auto& p : s.primitives)
      if (const auto* t = std::get_if<Triangle>(&p)) CHECK_FALSE(t->degenerate());
  }
}
