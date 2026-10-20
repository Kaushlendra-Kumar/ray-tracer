#pragma once
// Bounding Volume Hierarchy over an arbitrary set of bounded primitives.
//
// The BVH knows nothing about primitive types: it is built from a list of
// AABBs and traversed with a callback that tests one primitive index. This
// keeps it reusable (and unit-testable) and lets the compiler inline the
// primitive test into the traversal loop.
//
// Memory layout: nodes are stored in a flat std::vector in depth-first order.
// An interior node's left child is always the next node (index + 1), so only
// the right child index needs to be stored. Each node is 56 bytes
// (two Vec3 + two 32-bit fields), so roughly one node per cache line.
#include <cstdint>
#include <vector>

#include "raytracer/math/aabb.hpp"

namespace rt {

struct BvhNode {
  AABB bounds;
  std::uint32_t offset = 0;  // leaf: first index into prim_indices; interior: right child
  std::uint16_t count = 0;   // number of primitives (0 => interior node)
  std::uint8_t axis = 0;     // split axis (interior), for front-to-back ordering
  bool is_leaf() const { return count > 0; }
};

struct BvhBuildStats {
  std::size_t nodes = 0, leaves = 0, max_depth = 0, max_leaf_size = 0;
  double build_ms = 0;
  double sah_cost = 0;
};

struct TraversalCounters {
  std::uint64_t node_visits = 0;
  std::uint64_t prim_tests = 0;
};

class Bvh {
 public:
  // Build from per-primitive bounds. Primitives with empty bounds are skipped.
  void build(const std::vector<AABB>& prim_bounds, int max_leaf_size = 4);

  bool empty() const { return nodes_.empty(); }
  const std::vector<BvhNode>& nodes() const { return nodes_; }
  const std::vector<std::uint32_t>& prim_indices() const { return prim_indices_; }
  const BvhBuildStats& stats() const { return stats_; }
  AABB bounds() const { return nodes_.empty() ? AABB{} : nodes_[0].bounds; }

  // Closest-hit traversal. `test(prim_index, t_max)` must return the hit
  // distance when the primitive is hit in (t_min, t_max), or a value >= t_max
  // otherwise. `t_max` shrinks as hits are found so later boxes are culled.
  // Returns the closest hit distance (or the original t_max if none).
  template <class TestFn>
  Real closest(const Ray& r, Real t_min, Real t_max, TestFn&& test, TraversalCounters* ctr = nullptr) const;

  // Any-hit traversal for shadow rays: returns as soon as `test` reports a hit.
  template <class TestFn>
  bool any(const Ray& r, Real t_min, Real t_max, TestFn&& test, TraversalCounters* ctr = nullptr) const;

  // Self-check used by tests: every node encloses its children and primitives.
  bool validate(const std::vector<AABB>& prim_bounds) const;

 private:
  std::vector<BvhNode> nodes_;
  std::vector<std::uint32_t> prim_indices_;
  BvhBuildStats stats_;
};

// ---------------------------------------------------------------------------
namespace detail {
inline Vec3 safe_inverse(const Vec3& d) { return {1.0 / d.x, 1.0 / d.y, 1.0 / d.z}; }
}  // namespace detail

template <class TestFn>
Real Bvh::closest(const Ray& r, Real t_min, Real t_max, TestFn&& test, TraversalCounters* ctr) const {
  if (nodes_.empty()) return t_max;
  const Vec3 inv = detail::safe_inverse(r.direction);
  const bool neg[3] = {inv.x < 0, inv.y < 0, inv.z < 0};
  std::uint32_t stack[64];
  int sp = 0;
  std::uint32_t idx = 0;
  for (;;) {
    const BvhNode& n = nodes_[idx];
    if (ctr) ++ctr->node_visits;
    if (n.bounds.hit(r.origin, inv, t_min, t_max)) {
      if (n.is_leaf()) {
        for (std::uint32_t i = 0; i < n.count; ++i) {
          if (ctr) ++ctr->prim_tests;
          const Real t = test(prim_indices_[n.offset + i], t_max);
          if (t < t_max) t_max = t;
        }
      } else {
        // Visit the child on the near side of the split plane first.
        if (neg[n.axis]) { stack[sp++] = idx + 1; idx = n.offset; }
        else             { stack[sp++] = n.offset; idx = idx + 1; }
        continue;
      }
    }
    if (sp == 0) break;
    idx = stack[--sp];
  }
  return t_max;
}

template <class TestFn>
bool Bvh::any(const Ray& r, Real t_min, Real t_max, TestFn&& test, TraversalCounters* ctr) const {
  if (nodes_.empty()) return false;
  const Vec3 inv = detail::safe_inverse(r.direction);
  std::uint32_t stack[64];
  int sp = 0;
  std::uint32_t idx = 0;
  for (;;) {
    const BvhNode& n = nodes_[idx];
    if (ctr) ++ctr->node_visits;
    if (n.bounds.hit(r.origin, inv, t_min, t_max)) {
      if (n.is_leaf()) {
        for (std::uint32_t i = 0; i < n.count; ++i) {
          if (ctr) ++ctr->prim_tests;
          if (test(prim_indices_[n.offset + i], t_max) < t_max) return true;
        }
      } else {
        stack[sp++] = n.offset;
        idx = idx + 1;
        continue;
      }
    }
    if (sp == 0) break;
    idx = stack[--sp];
  }
  return false;
}

}  // namespace rt
