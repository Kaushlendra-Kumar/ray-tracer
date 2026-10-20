#include <catch2/catch_amalgamated.hpp>

#include "raytracer/materials/material.hpp"
#include "raytracer/math/sampling.hpp"

using namespace rt;
using Catch::Approx;

namespace {
HitRecord hit_with_normal(const Vec3& n, const Ray& r) {
  HitRecord rec;
  rec.p = {0, 0, 0};
  rec.set_face_normal(r, n);
  return rec;
}
}  // namespace

TEST_CASE("Lambertian scattering invariants", "[materials]") {
  const Material m = Lambertian{{Color{0.3, 0.5, 0.7}, Color{}, 0}};
  const Ray in{{0, 1, 0}, normalize(Vec3{0.3, -1, 0.2})};
  const HitRecord rec = hit_with_normal({0, 1, 0}, in);
  Rng rng(11);
  Real mean_cos = 0;
  const int n = 50000;
  for (int i = 0; i < n; ++i) {
    ScatterResult sr;
    REQUIRE(scatter(m, in, rec, rng, sr));
    REQUIRE(sr.direction.length() == Approx(1));
    REQUIRE(dot(sr.direction, rec.normal) > 0);   // stays in the upper hemisphere
    REQUIRE(sr.weight == Color{0.3, 0.5, 0.7});   // f cos / pdf == albedo exactly
    REQUIRE_FALSE(sr.specular);
    mean_cos += dot(sr.direction, rec.normal);
  }
  CHECK(mean_cos / n == Approx(2.0 / 3).margin(0.01));  // cosine-distributed
  // Energy conservation of the evaluated BRDF: integral of f cos over the
  // hemisphere = albedo. Estimate with uniform hemisphere sampling.
  Color integral{};
  const Onb onb(rec.normal);
  for (int i = 0; i < n; ++i) integral += eval_brdf_cos(m, rec, onb.to_world(uniform_hemisphere(rng))) * (2 * kPi);
  integral /= n;
  CHECK(integral.x == Approx(0.3).epsilon(0.02));
  CHECK(integral.z == Approx(0.7).epsilon(0.02));
  CHECK(eval_brdf_cos(m, rec, Vec3{0, -1, 0}).is_zero());  // below the surface
}

TEST_CASE("Metal reflects about the normal", "[materials]") {
  const Ray in{{-1, 1, 0}, normalize(Vec3{1, -1, 0})};
  const HitRecord rec = hit_with_normal({0, 1, 0}, in);
  Rng rng(3);
  ScatterResult sr;
  REQUIRE(scatter(Metal{Color{0.9}, 0}, in, rec, rng, sr));
  CHECK((sr.direction - normalize(Vec3{1, 1, 0})).length() < 1e-12);
  CHECK(sr.specular);
  // Rough metal: reflections spread out but never go below the surface.
  for (int i = 0; i < 1000; ++i) {
    if (scatter(Metal{Color{0.9}, 0.8}, in, rec, rng, sr)) {
      REQUIRE(dot(sr.direction, rec.geometric_normal) > 0);
      REQUIRE(sr.direction.length() == Approx(1));
    }
  }
}

TEST_CASE("Schlick Fresnel", "[materials][optics]") {
  // Normal incidence glass: ((1-1.5)/(1+1.5))^2 = 0.04
  CHECK(schlick(1.0, 1.0 / 1.5) == Approx(0.04));
  CHECK(schlick(0.0, 1.0 / 1.5) == Approx(1.0));  // grazing => total reflection
  CHECK(schlick(1.0, 1.0) == Approx(0.0));       // index-matched => no reflection
  for (Real c = 0; c <= 1; c += 0.05) {
    const Real f = schlick(c, 1 / 1.5);
    CHECK(f >= 0);
    CHECK(f <= 1);
  }
}

TEST_CASE("Dielectric entering and exiting", "[materials][optics]") {
  const Material glass = Dielectric{1.5, Color{1}};
  Rng rng(17);
  SECTION("normal incidence: ~4% reflected, rest transmitted straight through") {
    const Ray in{{0, 1, 0}, {0, -1, 0}};
    const HitRecord rec = hit_with_normal({0, 1, 0}, in);
    int reflected = 0;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
      ScatterResult sr;
      REQUIRE(scatter(glass, in, rec, rng, sr));
      REQUIRE(sr.specular);
      if (sr.direction.y > 0) ++reflected;
      else REQUIRE((sr.direction - Vec3{0, -1, 0}).length() < 1e-12);
    }
    CHECK(Real(reflected) / n == Approx(0.04).margin(0.003));
  }
  SECTION("oblique entry bends towards the normal (Snell)") {
    const Real theta = 50 * kPi / 180;
    const Ray in{{0, 1, 0}, {std::sin(theta), -std::cos(theta), 0}};
    const HitRecord rec = hit_with_normal({0, 1, 0}, in);
    REQUIRE(rec.front_face);
    for (int i = 0; i < 200; ++i) {
      ScatterResult sr;
      REQUIRE(scatter(glass, in, rec, rng, sr));
      if (sr.direction.y < 0) REQUIRE(sr.direction.x == Approx(std::sin(theta) / 1.5));
    }
  }
  SECTION("exiting beyond the critical angle always reflects (TIR)") {
    // Ray inside the glass travelling up, hitting the top surface from below.
    const Real theta = 60 * kPi / 180;  // critical angle for 1.5 is ~41.8 deg
    const Ray in{{0, -1, 0}, {std::sin(theta), std::cos(theta), 0}};
    const HitRecord rec = hit_with_normal({0, 1, 0}, in);  // outward normal +Y
    REQUIRE_FALSE(rec.front_face);
    for (int i = 0; i < 500; ++i) {
      ScatterResult sr;
      REQUIRE(scatter(glass, in, rec, rng, sr));
      REQUIRE(sr.direction.y < 0);  // stays inside
    }
  }
  SECTION("exiting below the critical angle refracts away from the normal") {
    const Real theta = 20 * kPi / 180;
    const Ray in{{0, -1, 0}, {std::sin(theta), std::cos(theta), 0}};
    const HitRecord rec = hit_with_normal({0, 1, 0}, in);
    bool saw_transmission = false;
    for (int i = 0; i < 200; ++i) {
      ScatterResult sr;
      REQUIRE(scatter(glass, in, rec, rng, sr));
      if (sr.direction.y > 0) {
        saw_transmission = true;
        REQUIRE(sr.direction.x == Approx(std::sin(theta) * 1.5));
      }
    }
    CHECK(saw_transmission);
  }
}

TEST_CASE("Emissive materials emit and absorb", "[materials]") {
  const Material e = Emissive{Color{1, 0.5, 0.25}, 4};
  CHECK(emitted(e, true) == Color{4, 2, 1});
  CHECK(emitted(e, false).is_zero());  // one-sided by default
  CHECK(emitted(Emissive{Color{1}, 2, true}, false) == Color{2});
  CHECK(emitted(Lambertian{}, true).is_zero());
  Rng rng(1);
  ScatterResult sr;
  const Ray in{{0, 1, 0}, {0, -1, 0}};
  CHECK_FALSE(scatter(e, in, hit_with_normal({0, 1, 0}, in), rng, sr));
}

TEST_CASE("Checker texture", "[materials]") {
  const Texture t{Color{1}, Color{0}, 1};
  CHECK(t.sample({0.5, 0.5, 0.5}) == Color{1});
  CHECK(t.sample({1.5, 0.5, 0.5}) == Color{0});
  CHECK(t.sample({-0.5, 0.5, 0.5}) == Color{0});
  // Regression: a ground plane at y = 0 must not flip parity on rounding noise.
  CHECK(t.sample({0.5, 1e-17, 0.5}) == t.sample({0.5, -1e-17, 0.5}));
}
