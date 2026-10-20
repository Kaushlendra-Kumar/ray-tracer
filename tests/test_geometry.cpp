#include <catch2/catch_amalgamated.hpp>

#include "raytracer/geometry/mesh.hpp"
#include "raytracer/geometry/shapes.hpp"
#include "raytracer/math/sampling.hpp"

using namespace rt;
using Catch::Approx;

static const Interval kAll{1e-9, kInfinity};

TEST_CASE("Ray-sphere intersection", "[geometry][sphere]") {
  const Sphere s{{0, 0, -5}, 1, 3};
  HitRecord rec;
  SECTION("head-on hit returns the near root and outward normal") {
    REQUIRE(s.intersect(Ray{{0, 0, 0}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.t == Approx(4));
    CHECK(rec.p.z == Approx(-4));
    CHECK(rec.normal.z == Approx(1));
    CHECK(rec.front_face);
    CHECK(rec.material == 3);
  }
  SECTION("miss") { CHECK_FALSE(s.intersect(Ray{{0, 2, 0}, {0, 0, -1}}, kAll, rec)); }
  SECTION("behind the ray") { CHECK_FALSE(s.intersect(Ray{{0, 0, 0}, {0, 0, 1}}, kAll, rec)); }
  SECTION("origin inside: far root, normal flipped to face the ray") {
    REQUIRE(s.intersect(Ray{{0, 0, -5}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.t == Approx(1));
    CHECK_FALSE(rec.front_face);
    CHECK(rec.normal.z == Approx(1));  // outward is -Z here; flipped towards the ray origin
    CHECK(rec.geometric_normal.z == Approx(1));
  }
  SECTION("tangent ray") {
    REQUIRE(s.intersect(Ray{{1, 0, 0}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.t == Approx(5).margin(1e-6));
  }
  SECTION("interval excludes hit") { CHECK_FALSE(s.intersect(Ray{{0, 0, 0}, {0, 0, -1}}, Interval(0, 3.9), rec)); }
  SECTION("unnormalised direction still yields correct point") {
    REQUIRE(s.intersect(Ray{{0, 0, 0}, {0, 0, -2}}, kAll, rec));
    CHECK(rec.t == Approx(2));
    CHECK(rec.p.z == Approx(-4));
  }
  SECTION("tiny far sphere (stable quadratic)") {
    const Sphere tiny{{0, 0, -1e4}, 1e-3, 0};
    REQUIRE(tiny.intersect(Ray{{0, 0, 0}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.t == Approx(1e4 - 1e-3).epsilon(1e-12));
  }
  SECTION("normals are unit length on random hits") {
    Rng rng(5);
    for (int i = 0; i < 1000; ++i) {
      const Vec3 dir = normalize(Vec3{0, 0, -5} + 0.9 * random_in_unit_sphere(rng));
      if (s.intersect(Ray{{0, 0, 0}, dir}, kAll, rec)) {
        REQUIRE(rec.normal.length() == Approx(1));
        REQUIRE((rec.p - s.center).length() == Approx(1));
        REQUIRE(dot(rec.normal, dir) < 0);
      }
    }
  }
}

TEST_CASE("Ray-plane intersection", "[geometry][plane]") {
  const Plane p{{0, -1, 0}, {0, 1, 0}, 0};
  HitRecord rec;
  REQUIRE(p.intersect(Ray{{0, 1, 0}, normalize(Vec3{1, -1, 0})}, kAll, rec));
  CHECK(rec.p.y == Approx(-1));
  CHECK(rec.p.x == Approx(2));
  CHECK(rec.normal == Vec3{0, 1, 0});
  CHECK(rec.front_face);
  CHECK_FALSE(p.intersect(Ray{{0, 1, 0}, {1, 0, 0}}, kAll, rec));   // parallel
  CHECK_FALSE(p.intersect(Ray{{0, 1, 0}, {0, 1, 0}}, kAll, rec));   // pointing away
  REQUIRE(p.intersect(Ray{{0, -3, 0}, {0, 1, 0}}, kAll, rec));      // from below: back face
  CHECK_FALSE(rec.front_face);
  CHECK(rec.normal == Vec3{0, -1, 0});
}

TEST_CASE("Ray-triangle intersection", "[geometry][triangle]") {
  const Triangle t{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {}, {}, {}, false, 0};
  HitRecord rec;
  SECTION("interior hit with barycentrics") {
    REQUIRE(t.intersect(Ray{{0.25, 0.25, 1}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.t == Approx(1));
    CHECK(rec.uv.x == Approx(0.25));
    CHECK(rec.uv.y == Approx(0.25));
    CHECK(rec.normal == Vec3{0, 0, 1});  // CCW winding => +Z
    CHECK(rec.front_face);
  }
  SECTION("outside the edges") {
    CHECK_FALSE(t.intersect(Ray{{0.6, 0.6, 1}, {0, 0, -1}}, kAll, rec));
    CHECK_FALSE(t.intersect(Ray{{-0.1, 0.5, 1}, {0, 0, -1}}, kAll, rec));
  }
  SECTION("two-sided") {
    REQUIRE(t.intersect(Ray{{0.2, 0.2, -1}, {0, 0, 1}}, kAll, rec));
    CHECK_FALSE(rec.front_face);
    CHECK(rec.normal == Vec3{0, 0, -1});
  }
  SECTION("parallel and degenerate") {
    CHECK_FALSE(t.intersect(Ray{{0.2, 0.2, 1}, {1, 0, 0}}, kAll, rec));
    const Triangle deg{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {}, {}, {}, false, 0};
    CHECK(deg.degenerate());
    CHECK_FALSE(deg.intersect(Ray{{1, 1, 5}, {0, 0, -1}}, kAll, rec));
  }
  SECTION("vertex normals are interpolated and kept on the visible side") {
    Triangle s = t;
    s.has_vertex_normals = true;
    s.n0 = normalize(Vec3{-1, 0, 1});
    s.n1 = normalize(Vec3{1, 0, 1});
    s.n2 = Vec3{0, 0, 1};
    REQUIRE(s.intersect(Ray{{0.5, 0.0001, 1}, {0, 0, -1}}, kAll, rec));
    CHECK(rec.normal.length() == Approx(1));
    CHECK(rec.normal.x == Approx(0).margin(0.01));  // midway between n0 and n1
    CHECK(rec.geometric_normal == Vec3{0, 0, 1});
  }
  SECTION("bounds of an axis-aligned triangle are padded") {
    const AABB b = t.bounds();
    CHECK(b.max.z > b.min.z);
    CHECK(b.hit(Ray{{0.2, 0.2, 1}, {0, 0, -1}}, kAll));
  }
}

TEST_CASE("Procedural meshes are closed and outward-facing", "[geometry][mesh]") {
  SECTION("icosphere") {
    const auto m = make_icosphere(2);
    CHECK(m.faces.size() == 20 * 16);
    for (const auto& f : m.faces) {
      const Vec3 a = m.positions[f[0]], b = m.positions[f[1]], c = m.positions[f[2]];
      REQUIRE(dot(cross(b - a, c - a), (a + b + c) / 3.0) > 0);
    }
    for (const auto& p : m.positions) REQUIRE(p.length() == Approx(1));
  }
  SECTION("torus") {
    const auto m = make_torus(1, 0.3, 24, 12);
    CHECK(m.faces.size() == 24 * 12 * 2);
    for (const auto& f : m.faces) {
      const Vec3 a = m.positions[f[0]], b = m.positions[f[1]], c = m.positions[f[2]];
      const Vec3 nsum = m.normals[f[0]] + m.normals[f[1]] + m.normals[f[2]];
      REQUIRE(dot(cross(b - a, c - a), nsum) > 0);
    }
  }
  SECTION("transform keeps normals perpendicular to surfaces under non-uniform scale") {
    const MeshTransform xf{{2, 1, 0.5}, 30, {1, 2, 3}};
    const Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 1};
    const Vec3 n = normalize(cross(b - a, c - a));
    const Vec3 ta = xf.apply_point(a), tb = xf.apply_point(b), tc = xf.apply_point(c);
    const Vec3 tn = xf.apply_normal(n);
    CHECK(dot(tn, tb - ta) == Approx(0).margin(1e-12));
    CHECK(dot(tn, tc - ta) == Approx(0).margin(1e-12));
    CHECK(xf.apply_point(a) == Vec3{1, 2, 3});
  }
}

TEST_CASE("OBJ parsing", "[geometry][obj]") {
  const auto m = parse_obj(
      "# square\n"
      "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
      "vn 0 0 1\n"
      "f 1//1 2//1 3//1 4//1\n");
  CHECK(m.positions.size() == 4);
  CHECK(m.faces.size() == 2);  // fan-triangulated quad
  CHECK(m.normals.size() == 4);
  const auto neg = parse_obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
  CHECK(neg.faces.size() == 1);
  CHECK(neg.faces[0] == std::array<std::uint32_t, 3>{0, 1, 2});
  CHECK(neg.normals.empty());
  CHECK_THROWS_WITH(parse_obj("v 0 0 0\nf 1 2 3\n"), Catch::Matchers::ContainsSubstring("line 2"));
  CHECK_THROWS(parse_obj("v 0 0\n"));
  CHECK_THROWS(parse_obj("v 0 0 0\nv 1 0 0\nf 1 2\n"));
}
