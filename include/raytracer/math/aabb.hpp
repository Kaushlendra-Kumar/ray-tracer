#pragma once
#include <cmath>
#include <utility>

#include "raytracer/math/ray.hpp"

namespace rt {

// Axis-aligned bounding box. A default-constructed box is empty (min=+inf, max=-inf),
// so `expand` from an empty box works without special cases.
struct AABB {
  Vec3 min{+kInfinity};
  Vec3 max{-kInfinity};

  constexpr AABB() = default;
  constexpr AABB(const Vec3& lo, const Vec3& hi) : min(lo), max(hi) {}

  bool empty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }

  void expand(const Vec3& p) { min = vmin(min, p); max = vmax(max, p); }
  void expand(const AABB& b) { min = vmin(min, b.min); max = vmax(max, b.max); }

  Vec3 extent() const { return empty() ? Vec3{} : max - min; }
  Vec3 centroid() const { return 0.5 * (min + max); }

  Real surface_area() const {
    if (empty()) return 0;
    const Vec3 e = max - min;
    return 2 * (e.x * e.y + e.y * e.z + e.z * e.x);
  }

  int longest_axis() const {
    const Vec3 e = extent();
    return (e.x >= e.y && e.x >= e.z) ? 0 : (e.y >= e.z ? 1 : 2);
  }

  // Slab test. `inv_dir` = 1/direction (may contain +-inf for axis-parallel rays).
  // An axis-parallel ray whose origin lies exactly on a slab plane produces
  // 0 * inf = NaN; we treat that slab as unbounded, i.e. the closed box counts
  // as hit (conservative, which is what a BVH needs).
  bool hit(const Point3& origin, const Vec3& inv_dir, Real t_min, Real t_max) const {
    return slab(min.x, max.x, origin.x, inv_dir.x, t_min, t_max) &&
           slab(min.y, max.y, origin.y, inv_dir.y, t_min, t_max) &&
           slab(min.z, max.z, origin.z, inv_dir.z, t_min, t_max);
  }
  bool hit(const Ray& r, Interval t) const {
    const Vec3 inv{1.0 / r.direction.x, 1.0 / r.direction.y, 1.0 / r.direction.z};
    return hit(r.origin, inv, t.min, t.max);
  }

  static AABB merge(AABB a, const AABB& b) { a.expand(b); return a; }

 private:
  // One axis of the slab test; narrows [t_min, t_max] and reports overlap.
  static bool slab(Real lo, Real hi, Real o, Real inv, Real& t_min, Real& t_max) {
    Real t0 = (lo - o) * inv;
    Real t1 = (hi - o) * inv;
    // NaN only arises for a ray parallel to this slab whose origin lies on
    // one of its planes: it is inside the (closed) slab for all t.
    if (t0 != t0 || t1 != t1) return true;
    // Plain comparisons compile to minsd/maxsd; std::fmin/fmax are
    // NaN-aware library calls (measured: ~18% of render time).
    if (t0 > t1) std::swap(t0, t1);
    t_min = t0 > t_min ? t0 : t_min;
    t_max = t1 < t_max ? t1 : t_max;
    return t_min <= t_max;
  }
};

}  // namespace rt
