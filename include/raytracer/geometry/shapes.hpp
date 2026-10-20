#pragma once
// Analytic primitives. Each is a plain value type with an `intersect` member
// that fills a HitRecord when a hit inside (t_min, t_max) exists.
#include <cmath>

#include "raytracer/core/hit.hpp"
#include "raytracer/math/aabb.hpp"

namespace rt {

struct Sphere {
  Point3 center;
  Real radius = 1;
  MaterialId material = 0;

  // Solve |o + t d - c|^2 = r^2, i.e. a t^2 - 2 h t + c = 0, with two
  // precision fixes (Haines et al., "Precision Improvements for Ray/Sphere
  // Intersection", Ray Tracing Gems ch. 7):
  //  * the discriminant h^2 - a c is computed as a (r^2 - |v|^2), where v is the
  //    perpendicular from the centre to the ray. The naive form subtracts two
  //    huge, nearly equal numbers for small or distant spheres.
  //  * the roots are paired as q/a and c/q with q = h + sign(h) sqrt(disc), so
  //    neither root suffers cancellation.
  // Cheap test: distance only. `extra` is unused (shared signature with Triangle).
  bool hit_t(const Ray& r, Real t_min, Real t_max, Real& t_out, Vec2& /*extra*/) const {
    const Vec3 oc = center - r.origin;
    const Real a = r.direction.length_squared();
    if (a == 0) return false;
    const Real h = dot(r.direction, oc);
    const Real c = oc.length_squared() - radius * radius;
    const Vec3 v = oc - (h / a) * r.direction;
    const Real disc = a * (radius * radius - v.length_squared());
    if (disc < 0) return false;
    const Real sq = std::sqrt(disc);
    const Real q = h + std::copysign(sq, h);  // same-sign sum: no cancellation
    Real t0 = c / q, t1 = q / a;              // roots of a t^2 - 2 h t + c
    if (q == 0) { t0 = t1 = h / a; }
    if (t0 > t1) std::swap(t0, t1);
    Real t = t0;
    if (!(t_min < t && t < t_max)) {
      t = t1;
      if (!(t_min < t && t < t_max)) return false;
    }
    t_out = t;
    return true;
  }

  // Shading data for an accepted hit (computed once per ray, not per candidate).
  void finalize(const Ray& r, Real t, Vec2 /*extra*/, HitRecord& rec) const {
    rec.t = t;
    rec.p = r.at(t);
    const Vec3 outward = (rec.p - center) / radius;
    rec.set_face_normal(r, outward);
    const Real theta = std::acos(std::clamp(-outward.y, Real(-1), Real(1)));
    const Real phi = std::atan2(-outward.z, outward.x) + kPi;
    rec.uv = {phi / (2 * kPi), theta / kPi};
    rec.material = material;
  }

  bool intersect(const Ray& r, Interval t_range, HitRecord& rec) const {
    Real t;
    Vec2 e;
    if (!hit_t(r, t_range.min, t_range.max, t, e)) return false;
    finalize(r, t, e, rec);
    return true;
  }

  AABB bounds() const {
    const Vec3 r{std::fabs(radius)};
    return {center - r, center + r};
  }
  Real area() const { return 4 * kPi * radius * radius; }
};

// Infinite plane through `point` with unit `normal`. Unbounded, so it lives
// outside the BVH and is tested separately.
struct Plane {
  Point3 point;
  Vec3 normal{0, 1, 0};
  MaterialId material = 0;

  bool intersect(const Ray& r, Interval t_range, HitRecord& rec) const {
    const Real denom = dot(normal, r.direction);
    if (std::fabs(denom) < 1e-12) return false;  // parallel (or lying in the plane)
    const Real t = dot(point - r.origin, normal) / denom;
    if (!t_range.surrounds(t)) return false;
    rec.t = t;
    rec.p = r.at(t);
    rec.set_face_normal(r, normal);
    // Planar UVs from an arbitrary tangent frame (used for checker textures).
    const Vec3 tangent = normalize_or(cross(std::fabs(normal.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, normal), {1, 0, 0});
    const Vec3 bitangent = cross(normal, tangent);
    const Vec3 d = rec.p - point;
    rec.uv = {dot(d, tangent), dot(d, bitangent)};
    rec.material = material;
    return true;
  }
};

// Triangle with optional per-vertex normals (smooth shading for meshes).
struct Triangle {
  Point3 v0, v1, v2;
  Vec3 n0, n1, n2;  // only used when has_vertex_normals
  bool has_vertex_normals = false;
  MaterialId material = 0;

  // Möller–Trumbore. Two-sided: hits from either side are reported, with the
  // outward normal defined by counter-clockwise winding (v0, v1, v2).
  // `bary` receives the barycentrics (b1, b2) of the hit.
  bool hit_t(const Ray& r, Real t_min, Real t_max, Real& t_out, Vec2& bary) const {
    const Vec3 e1 = v1 - v0, e2 = v2 - v0;
    const Vec3 pvec = cross(r.direction, e2);
    const Real det = dot(e1, pvec);
    if (std::fabs(det) < 1e-14) return false;  // ray parallel or degenerate triangle
    const Real inv_det = 1.0 / det;
    const Vec3 tvec = r.origin - v0;
    const Real b1 = dot(tvec, pvec) * inv_det;
    if (b1 < 0 || b1 > 1) return false;
    const Vec3 qvec = cross(tvec, e1);
    const Real b2 = dot(r.direction, qvec) * inv_det;
    if (b2 < 0 || b1 + b2 > 1) return false;
    const Real t = dot(e2, qvec) * inv_det;
    if (!(t_min < t && t < t_max)) return false;
    t_out = t;
    bary = {b1, b2};
    return true;
  }

  void finalize(const Ray& r, Real t, Vec2 bary, HitRecord& rec) const {
    rec.t = t;
    rec.p = r.at(t);
    const Vec3 ng = normalize(cross(v1 - v0, v2 - v0));
    rec.set_face_normal(r, ng);
    if (has_vertex_normals) {
      const Vec3 ns = normalize_or((1 - bary.x - bary.y) * n0 + bary.x * n1 + bary.y * n2, ng);
      rec.normal = rec.front_face ? ns : -ns;
      // Keep the shading normal in the same hemisphere as the geometric one.
      if (dot(rec.normal, rec.geometric_normal) <= 0) rec.normal = rec.geometric_normal;
    }
    rec.uv = bary;
    rec.material = material;
  }

  bool intersect(const Ray& r, Interval t_range, HitRecord& rec) const {
    Real t;
    Vec2 b;
    if (!hit_t(r, t_range.min, t_range.max, t, b)) return false;
    finalize(r, t, b, rec);
    return true;
  }

  AABB bounds() const {
    AABB b;
    b.expand(v0); b.expand(v1); b.expand(v2);
    // Pad flat boxes so axis-aligned triangles never get zero-thickness slabs.
    const Vec3 pad{1e-9};
    b.min -= pad; b.max += pad;
    return b;
  }
  Real area() const { return 0.5 * cross(v1 - v0, v2 - v0).length(); }
  bool degenerate() const { return area() < 1e-14; }
};

}  // namespace rt
