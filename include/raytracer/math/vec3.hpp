#pragma once
// Small value types for 2D/3D vectors.
//
// Conventions (see docs/math_foundations.md):
//   * Right-handed coordinate system, +Y is "up", the default camera looks down -Z.
//   * `Real` is double everywhere. Scalar double math costs about the same as float
//     on x86-64 and buys a lot of headroom against self-intersection and
//     cancellation errors, which is the dominant correctness risk in a tracer.
//   * Vec3 doubles as an RGB colour type (component-wise ops are defined).

#include <cmath>
#include <cstddef>
#include <ostream>

namespace rt {

using Real = double;

inline constexpr Real kPi = 3.14159265358979323846;
inline constexpr Real kInvPi = 1.0 / kPi;
inline constexpr Real kEpsilon = 1e-9;  // "is this length effectively zero?"

struct Vec2 {
  Real x = 0, y = 0;
  constexpr Vec2() = default;
  constexpr Vec2(Real x_, Real y_) : x(x_), y(y_) {}
  constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2 operator*(Real s) const { return {x * s, y * s}; }
  constexpr bool operator==(const Vec2&) const = default;
};

struct Vec3 {
  Real x = 0, y = 0, z = 0;

  constexpr Vec3() = default;
  constexpr Vec3(Real x_, Real y_, Real z_) : x(x_), y(y_), z(z_) {}
  explicit constexpr Vec3(Real s) : x(s), y(s), z(s) {}

  constexpr Real operator[](std::size_t i) const { return i == 0 ? x : (i == 1 ? y : z); }
  constexpr Real& operator[](std::size_t i) { return i == 0 ? x : (i == 1 ? y : z); }

  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  constexpr Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  constexpr Vec3& operator*=(const Vec3& o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
  constexpr Vec3& operator*=(Real s) { x *= s; y *= s; z *= s; return *this; }
  constexpr Vec3& operator/=(Real s) { return *this *= (1.0 / s); }

  constexpr bool operator==(const Vec3&) const = default;

  constexpr Real length_squared() const { return x * x + y * y + z * z; }
  Real length() const { return std::sqrt(length_squared()); }
  constexpr Real max_component() const { return x > y ? (x > z ? x : z) : (y > z ? y : z); }
  constexpr bool is_zero() const { return x == 0 && y == 0 && z == 0; }
  bool is_finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
  bool near_zero(Real eps = 1e-8) const {
    return std::fabs(x) < eps && std::fabs(y) < eps && std::fabs(z) < eps;
  }
};

using Color = Vec3;
using Point3 = Vec3;

constexpr Vec3 operator+(Vec3 a, const Vec3& b) { return a += b; }
constexpr Vec3 operator-(Vec3 a, const Vec3& b) { return a -= b; }
constexpr Vec3 operator*(Vec3 a, const Vec3& b) { return a *= b; }  // component-wise
constexpr Vec3 operator*(Vec3 a, Real s) { return a *= s; }
constexpr Vec3 operator*(Real s, Vec3 a) { return a *= s; }
constexpr Vec3 operator/(Vec3 a, Real s) { return a /= s; }

constexpr Real dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 vmin(const Vec3& a, const Vec3& b) {
  return {std::fmin(a.x, b.x), std::fmin(a.y, b.y), std::fmin(a.z, b.z)};
}
inline Vec3 vmax(const Vec3& a, const Vec3& b) {
  return {std::fmax(a.x, b.x), std::fmax(a.y, b.y), std::fmax(a.z, b.z)};
}
constexpr Vec3 lerp(const Vec3& a, const Vec3& b, Real t) { return a * (1 - t) + b * t; }

// Normalisation that never divides by ~zero: degenerate input yields `fallback`.
inline Vec3 normalize_or(const Vec3& v, const Vec3& fallback) {
  const Real len2 = v.length_squared();
  if (!(len2 > kEpsilon * kEpsilon) || !std::isfinite(len2)) return fallback;
  return v / std::sqrt(len2);
}
// Callers that *know* the input is non-degenerate. Still safe: returns zero vector.
inline Vec3 normalize(const Vec3& v) { return normalize_or(v, Vec3{}); }

// Mirror reflection of incident direction `v` about unit normal `n`.
constexpr Vec3 reflect(const Vec3& v, const Vec3& n) { return v - 2 * dot(v, n) * n; }

// Snell refraction of unit vector `uv` through a surface with unit normal `n`
// (pointing against `uv`), with eta = n_incident / n_transmitted.
// Returns false on total internal reflection.
inline bool refract(const Vec3& uv, const Vec3& n, Real eta, Vec3& out) {
  const Real cos_i = std::fmin(dot(-uv, n), 1.0);
  const Real sin2_t = eta * eta * (1.0 - cos_i * cos_i);
  if (sin2_t > 1.0) return false;  // total internal reflection
  const Real cos_t = std::sqrt(1.0 - sin2_t);
  out = eta * uv + (eta * cos_i - cos_t) * n;
  return true;
}

inline std::ostream& operator<<(std::ostream& os, const Vec3& v) {
  return os << '(' << v.x << ", " << v.y << ", " << v.z << ')';
}

}  // namespace rt
