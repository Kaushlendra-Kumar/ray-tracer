#include <catch2/catch_amalgamated.hpp>

#include "raytracer/core/camera.hpp"
#include "raytracer/math/aabb.hpp"
#include "raytracer/math/sampling.hpp"

using namespace rt;
using Catch::Approx;

static bool near(const Vec3& a, const Vec3& b, Real eps = 1e-12) { return (a - b).length() < eps; }

TEST_CASE("Vec3 arithmetic", "[math]") {
  const Vec3 a{1, 2, 3}, b{4, -5, 6};
  CHECK(a + b == Vec3{5, -3, 9});
  CHECK(a - b == Vec3{-3, 7, -3});
  CHECK(a * 2.0 == Vec3{2, 4, 6});
  CHECK(2.0 * a == Vec3{2, 4, 6});
  CHECK(a * b == Vec3{4, -10, 18});
  CHECK(a / 2.0 == Vec3{0.5, 1, 1.5});
  CHECK(-a == Vec3{-1, -2, -3});
  CHECK(a[0] == 1);
  CHECK(a[2] == 3);
  CHECK(a.max_component() == 3);
}

TEST_CASE("dot and cross identities", "[math]") {
  const Vec3 a{1.5, -2, 0.25}, b{-3, 4, 7}, c{0.5, 0.5, -2};
  CHECK(dot(a, b) == Approx(1.5 * -3 + -2 * 4 + 0.25 * 7));
  CHECK(dot(cross(a, b), a) == Approx(0).margin(1e-12));          // a x b is orthogonal to a
  CHECK(dot(cross(a, b), b) == Approx(0).margin(1e-12));
  CHECK(near(cross(a, b), -cross(b, a)));                          // anti-commutative
  CHECK(cross(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 0, 1});     // right-handed
  CHECK(dot(a, cross(b, c)) == Approx(dot(b, cross(c, a))));       // scalar triple product
  // Lagrange: |a x b|^2 = |a|^2 |b|^2 - (a.b)^2
  CHECK(cross(a, b).length_squared() == Approx(a.length_squared() * b.length_squared() - dot(a, b) * dot(a, b)));
}

TEST_CASE("normalisation is guarded", "[math]") {
  CHECK(normalize(Vec3{3, 0, 4}).length() == Approx(1));
  CHECK(normalize(Vec3{}) == Vec3{});
  CHECK(normalize_or(Vec3{1e-300, 0, 0}, Vec3{0, 1, 0}) == Vec3{0, 1, 0});
  CHECK(normalize_or(Vec3{kInfinity, 0, 0}, Vec3{0, 0, 1}) == Vec3{0, 0, 1});
  CHECK(near(normalize(Vec3{1e-5, 0, 0}), Vec3{1, 0, 0}));
}

TEST_CASE("reflection", "[math][optics]") {
  const Vec3 n{0, 1, 0};
  CHECK(reflect(Vec3{1, -1, 0}, n) == Vec3{1, 1, 0});
  CHECK(reflect(Vec3{0, -1, 0}, n) == Vec3{0, 1, 0});  // normal incidence reverses
  const Vec3 v = normalize(Vec3{0.3, -0.8, 0.2});
  CHECK(reflect(v, n).length() == Approx(1));          // preserves length
  CHECK(dot(reflect(v, n), n) == Approx(-dot(v, n)));  // angle in == angle out
}

TEST_CASE("refraction obeys Snell's law", "[math][optics]") {
  const Vec3 n{0, 1, 0};
  Vec3 out;
  SECTION("normal incidence passes straight through") {
    REQUIRE(refract(Vec3{0, -1, 0}, n, 1.0 / 1.5, out));
    CHECK(near(out, Vec3{0, -1, 0}));
  }
  SECTION("oblique incidence") {
    const Real theta_i = 40 * kPi / 180, eta = 1.0 / 1.5;
    const Vec3 in{std::sin(theta_i), -std::cos(theta_i), 0};
    REQUIRE(refract(in, n, eta, out));
    CHECK(out.length() == Approx(1));
    const Real sin_t = out.x;  // tangential component of a unit vector
    CHECK(sin_t == Approx(eta * std::sin(theta_i)));
    CHECK(out.y < 0);  // continues into the second medium
  }
  SECTION("total internal reflection beyond the critical angle") {
    const Real eta = 1.5;  // glass -> air
    const Real crit = std::asin(1 / eta);
    const Real below = crit - 0.01, above = crit + 0.01;
    CHECK(refract(Vec3{std::sin(below), -std::cos(below), 0}, n, eta, out));
    CHECK_FALSE(refract(Vec3{std::sin(above), -std::cos(above), 0}, n, eta, out));
  }
  SECTION("eta = 1 leaves direction unchanged") {
    const Vec3 in = normalize(Vec3{0.4, -0.7, 0.1});
    REQUIRE(refract(in, n, 1.0, out));
    CHECK(near(out, in, 1e-12));
  }
}

TEST_CASE("Ray and Interval", "[math]") {
  const Ray r{{1, 2, 3}, {0, 0, -1}};
  CHECK(r.at(2) == Vec3{1, 2, 1});
  CHECK(Interval{}.empty());
  CHECK_FALSE(Interval(0, 1).empty());
  CHECK(Interval(0, 1).contains(1));
  CHECK_FALSE(Interval(0, 1).surrounds(1));
  CHECK(Interval(0, 1).clamp(2) == 1);
  CHECK(Interval::universe().surrounds(1e300));
}

TEST_CASE("AABB slab test", "[math][aabb]") {
  const AABB box{{-1, -1, -1}, {1, 1, 1}};
  CHECK(box.hit(Ray{{0, 0, 5}, {0, 0, -1}}, {0, kInfinity}));
  CHECK_FALSE(box.hit(Ray{{0, 0, 5}, {0, 0, 1}}, {0, kInfinity}));       // pointing away
  CHECK_FALSE(box.hit(Ray{{2, 0, 5}, {0, 0, -1}}, {0, kInfinity}));      // misses laterally
  CHECK_FALSE(box.hit(Ray{{0, 0, 5}, {0, 0, -1}}, {0, 3.9}));            // t_max too short
  CHECK(box.hit(Ray{{0, 0, 0}, {0.3, 0.2, 0.1}}, {0, kInfinity}));       // origin inside
  CHECK(box.hit(Ray{{1, 0, 5}, {0, 0, -1}}, {0, kInfinity}));            // grazing face, axis-parallel (NaN path)
  CHECK(box.hit(Ray{{1, 0, 5}, {-0.0, 0, -1}}, {0, kInfinity}));         // same with -0.0 (inv = -inf)
  CHECK(box.hit(Ray{{-1, 0, 5}, {0.0, 0, -1}}, {0, kInfinity}));         // on the min plane
  CHECK(box.hit(Ray{{-1, 0, 5}, {-0.0, 0, -1}}, {0, kInfinity}));
  CHECK_FALSE(box.hit(Ray{{1.001, 0, 5}, {-0.0, 0, -1}}, {0, kInfinity}));  // just outside, parallel
  CHECK(box.hit(Ray{{-5, -5, -5}, {1, 1, 1}}, {0, kInfinity}));          // diagonal
  CHECK(box.surface_area() == Approx(24));
  CHECK(box.longest_axis() == 0);
  AABB e;
  CHECK(e.empty());
  CHECK(e.surface_area() == 0);
  e.expand(Vec3{1, 2, 3});
  CHECK_FALSE(e.empty());
  CHECK(e.centroid() == Vec3{1, 2, 3});
}

TEST_CASE("ONB is orthonormal for many normals, including poles", "[math]") {
  Rng rng(7);
  std::vector<Vec3> normals{{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {0, 1, 0}};
  for (int i = 0; i < 200; ++i) normals.push_back(random_unit_vector(rng));
  for (const Vec3& n : normals) {
    const Onb b(n);
    CHECK(b.u.length() == Approx(1));
    CHECK(b.v.length() == Approx(1));
    CHECK(dot(b.u, b.v) == Approx(0).margin(1e-12));
    CHECK(dot(b.u, b.w) == Approx(0).margin(1e-12));
    CHECK(near(cross(b.u, b.v), b.w, 1e-9));  // right-handed
    const Vec3 x{0.2, -0.4, 0.9};
    CHECK(near(b.to_world(b.to_local(x)), x, 1e-12));
  }
}

TEST_CASE("RNG is deterministic and in range", "[math][rng]") {
  Rng a(42), b(42), c(43);
  bool all_same = true;
  for (int i = 0; i < 1000; ++i) {
    const Real x = a.uniform();
    REQUIRE(x >= 0);
    REQUIRE(x < 1);
    if (x != b.uniform()) all_same = false;
  }
  CHECK(all_same);
  CHECK(Rng(42).next_u32() != c.next_u32());
  CHECK(Rng::for_sample(1, 2, 3).next_u32() == Rng::for_sample(1, 2, 3).next_u32());
  CHECK(Rng::for_sample(1, 2, 3).next_u32() != Rng::for_sample(1, 2, 4).next_u32());
}

TEST_CASE("Sampling distributions have the expected moments", "[math][sampling]") {
  Rng rng(1234);
  const int n = 200000;
  Real mean_cos_cosine = 0, mean_cos_uniform = 0;
  Vec3 mean_sphere{};
  for (int i = 0; i < n; ++i) {
    const Vec3 c = cosine_hemisphere(rng);
    REQUIRE(c.z >= 0);
    REQUIRE(c.length() == Approx(1).margin(1e-9));
    mean_cos_cosine += c.z;
    mean_cos_uniform += uniform_hemisphere(rng).z;
    mean_sphere += random_unit_vector(rng);
    REQUIRE(random_in_unit_sphere(rng).length_squared() < 1);
  }
  // E[cos] under cos/pi is 2/3; under the uniform hemisphere it is 1/2.
  CHECK(mean_cos_cosine / n == Approx(2.0 / 3).margin(0.005));
  CHECK(mean_cos_uniform / n == Approx(0.5).margin(0.005));
  CHECK((mean_sphere / n).length() < 0.01);
}

TEST_CASE("Camera basis and centre ray", "[math][camera]") {
  CameraDesc d;
  d.look_from = {0, 0, 0};
  d.look_at = {0, 0, -1};
  d.vfov_deg = 90;
  Camera cam(d, 200, 100);
  CHECK(near(cam.w(), Vec3{0, 0, 1}));
  CHECK(near(cam.u(), Vec3{1, 0, 0}));
  CHECK(near(cam.v(), Vec3{0, 1, 0}));
  Rng rng(1);
  // The centre of the image (pixel 100, 50 at offset 0) looks straight down -Z.
  const Ray centre = cam.generate(100, 50, 0, 0, rng);
  CHECK(near(centre.direction, Vec3{0, 0, -1}, 1e-12));
  // Top-left corner: vfov 90 => half-height = 1 at focus distance 1; aspect 2 => half-width 2.
  const Ray tl = cam.generate(0, 0, 0, 0, rng);
  CHECK(near(tl.direction, normalize(Vec3{-2, 1, -1}), 1e-12));
  // Degenerate vup (parallel to view) must still give an orthonormal basis.
  d.vup = {0, 0, 1};
  Camera cam2(d, 10, 10);
  CHECK(cam2.u().length() == Approx(1));
  CHECK(dot(cam2.u(), cam2.w()) == Approx(0).margin(1e-12));
}
