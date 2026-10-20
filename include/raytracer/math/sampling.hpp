#pragma once
// Sampling routines. Each returns a direction (or point) together with an
// implied PDF documented next to it; docs/rendering_pipeline.md derives them.
#include <cmath>

#include "raytracer/math/rng.hpp"
#include "raytracer/math/vec3.hpp"

namespace rt {

// Orthonormal basis around a unit vector `w` (Duff et al. 2017, branchless,
// free of the singularity of the classic "pick a non-parallel axis" trick).
struct Onb {
  Vec3 u, v, w;
  explicit Onb(const Vec3& n) : w(n) {
    const Real sign = std::copysign(Real(1), n.z);
    const Real a = -1.0 / (sign + n.z);
    const Real b = n.x * n.y * a;
    u = Vec3{1.0 + sign * n.x * n.x * a, sign * b, -sign * n.x};
    v = Vec3{b, sign + n.y * n.y * a, -n.y};
  }
  Vec3 to_world(const Vec3& a) const { return a.x * u + a.y * v + a.z * w; }
  Vec3 to_local(const Vec3& a) const { return {dot(a, u), dot(a, v), dot(a, w)}; }
};

// Rejection sampling inside the unit ball (expected ~1.91 iterations).
inline Vec3 random_in_unit_sphere(Rng& rng) {
  for (;;) {
    const Vec3 p{rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)};
    if (p.length_squared() < 1) return p;
  }
}

// Uniform direction on the unit sphere. pdf = 1 / (4 pi).
inline Vec3 random_unit_vector(Rng& rng) {
  const Real z = 1 - 2 * rng.uniform();
  const Real r = std::sqrt(std::fmax(0.0, 1 - z * z));
  const Real phi = 2 * kPi * rng.uniform();
  return {r * std::cos(phi), r * std::sin(phi), z};
}

// Uniform point in the unit disk (polar mapping with sqrt for uniform area).
inline Vec2 random_in_unit_disk(Rng& rng) {
  const Real r = std::sqrt(rng.uniform());
  const Real phi = 2 * kPi * rng.uniform();
  return {r * std::cos(phi), r * std::sin(phi)};
}

// Uniform hemisphere around +Z (local frame). pdf = 1 / (2 pi).
inline Vec3 uniform_hemisphere(Rng& rng) {
  const Real z = rng.uniform();
  const Real r = std::sqrt(std::fmax(0.0, 1 - z * z));
  const Real phi = 2 * kPi * rng.uniform();
  return {r * std::cos(phi), r * std::sin(phi), z};
}

// Cosine-weighted hemisphere around +Z (Malley's method). pdf = cos(theta) / pi.
inline Vec3 cosine_hemisphere(Rng& rng) {
  const Vec2 d = random_in_unit_disk(rng);
  const Real z = std::sqrt(std::fmax(0.0, 1 - d.x * d.x - d.y * d.y));
  return {d.x, d.y, z};
}
inline Real cosine_hemisphere_pdf(Real cos_theta) { return cos_theta > 0 ? cos_theta * kInvPi : 0; }

// Uniform direction inside a cone of half-angle acos(cos_max) around +Z.
// pdf = 1 / (2 pi (1 - cos_max)).
inline Vec3 uniform_cone(Rng& rng, Real cos_max) {
  const Real cos_t = 1 - rng.uniform() * (1 - cos_max);
  const Real sin_t = std::sqrt(std::fmax(0.0, 1 - cos_t * cos_t));
  const Real phi = 2 * kPi * rng.uniform();
  return {sin_t * std::cos(phi), sin_t * std::sin(phi), cos_t};
}

// Uniform barycentrics on a triangle (Osada et al.). Returns (b1, b2); b0 = 1-b1-b2.
inline Vec2 uniform_triangle(Rng& rng) {
  const Real su = std::sqrt(rng.uniform());
  return {1 - su, rng.uniform() * su};
}

}  // namespace rt
