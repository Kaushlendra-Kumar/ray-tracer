#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

#include "raytracer/math/vec3.hpp"

namespace rt {

inline constexpr Real kInfinity = std::numeric_limits<Real>::infinity();

// A ray P(t) = origin + t * direction. Direction is normalised by the camera and
// all materials; intersection routines nevertheless do not assume unit length
// (the BVH equivalence tests use arbitrary directions).
struct Ray {
  Point3 origin;
  Vec3 direction;
  constexpr Ray() = default;
  constexpr Ray(const Point3& o, const Vec3& d) : origin(o), direction(d) {}
  constexpr Point3 at(Real t) const { return origin + t * direction; }
};

// Closed interval [min, max]. The default interval is *empty* (min > max).
struct Interval {
  Real min = +kInfinity;
  Real max = -kInfinity;

  constexpr Interval() = default;
  constexpr Interval(Real lo, Real hi) : min(lo), max(hi) {}

  constexpr Real size() const { return max - min; }
  constexpr bool empty() const { return min > max; }
  constexpr bool contains(Real x) const { return min <= x && x <= max; }
  constexpr bool surrounds(Real x) const { return min < x && x < max; }
  constexpr Real clamp(Real x) const { return x < min ? min : (x > max ? max : x); }
  constexpr Interval expand(Real delta) const { return {min - delta / 2, max + delta / 2}; }

  static constexpr Interval universe() { return {-kInfinity, +kInfinity}; }
};

// Numerical tolerances used across the tracer (documented in math_foundations.md).
namespace tol {
// Minimum hit distance accepted for secondary rays. Combined with offsetting the
// ray origin along the geometric normal this prevents "shadow acne".
inline constexpr Real kRayTMin = 1e-6;
// Distance used to push spawned ray origins off a surface, scaled by the hit
// point's magnitude so that the offset remains meaningful far from the origin.
inline Real spawn_offset(const Point3& p) {
  const Real m = std::max({std::fabs(p.x), std::fabs(p.y), std::fabs(p.z), Real(1)});
  return 1e-7 * m * 16;
}
}  // namespace tol

// Offset a point along the geometric normal, on the side that `dir` points to.
inline Point3 offset_ray_origin(const Point3& p, const Vec3& geometric_normal, const Vec3& dir) {
  const Real d = tol::spawn_offset(p);
  return dot(dir, geometric_normal) > 0 ? p + d * geometric_normal : p - d * geometric_normal;
}

}  // namespace rt
