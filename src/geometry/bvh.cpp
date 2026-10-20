#include "raytracer/geometry/bvh.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <numeric>
#include <stdexcept>

namespace rt {

namespace {

constexpr int kBins = 16;
constexpr Real kTraversalCost = 1.0;  // relative to one primitive intersection
constexpr std::size_t kMaxDepth = 60; // traversal stack is 64 entries

struct BuildPrim {
  AABB bounds;
  Vec3 centroid;
  std::uint32_t index;
};

class Builder {
 public:
  Builder(std::vector<BuildPrim>& prims, std::vector<BvhNode>& nodes, BvhBuildStats& st, int max_leaf)
      : prims_(prims), nodes_(nodes), st_(st), max_leaf_(static_cast<std::size_t>(std::max(1, max_leaf))) {}

  void build(std::size_t begin, std::size_t end, std::size_t depth) {
    const auto node_idx = nodes_.size();
    nodes_.emplace_back();
    AABB bounds, cbounds;
    for (std::size_t i = begin; i < end; ++i) {
      bounds.expand(prims_[i].bounds);
      cbounds.expand(prims_[i].centroid);
    }
    nodes_[node_idx].bounds = bounds;
    st_.max_depth = std::max(st_.max_depth, depth);
    const std::size_t n = end - begin;

    if (n <= max_leaf_ || depth >= kMaxDepth) return make_leaf(node_idx, begin, n);

    // --- Binned SAH (Wald 2007): evaluate kBins-1 candidate planes per axis.
    int best_axis = -1, best_split = -1;
    Real best_cost = kInfinity;
    const Real parent_area = bounds.surface_area();
    for (int axis = 0; axis < 3; ++axis) {
      const auto ax = static_cast<std::size_t>(axis);
      const Real lo = cbounds.min[ax], hi = cbounds.max[ax];
      if (!(hi > lo)) continue;  // all centroids coincide on this axis
      std::array<AABB, kBins> bin_bounds{};
      std::array<std::size_t, kBins> bin_count{};
      const Real scale = kBins / (hi - lo);
      for (std::size_t i = begin; i < end; ++i) {
        const int b = bin_of(prims_[i].centroid[ax], lo, scale);
        bin_count[static_cast<std::size_t>(b)]++;
        bin_bounds[static_cast<std::size_t>(b)].expand(prims_[i].bounds);
      }
      // Sweep from the right to get suffix areas/counts, then from the left.
      std::array<Real, kBins> right_area{};
      std::array<std::size_t, kBins> right_count{};
      AABB acc;
      std::size_t cnt = 0;
      for (int b = kBins - 1; b > 0; --b) {
        const auto ub = static_cast<std::size_t>(b);
        acc.expand(bin_bounds[ub]);
        cnt += bin_count[ub];
        right_area[ub] = acc.surface_area();
        right_count[ub] = cnt;
      }
      acc = AABB{};
      cnt = 0;
      for (int b = 0; b < kBins - 1; ++b) {
        const auto ub = static_cast<std::size_t>(b);
        acc.expand(bin_bounds[ub]);
        cnt += bin_count[ub];
        const std::size_t rc = right_count[ub + 1];
        if (cnt == 0 || rc == 0) continue;
        const Real cost = kTraversalCost + (acc.surface_area() * Real(cnt) + right_area[ub + 1] * Real(rc)) /
                                               std::max(parent_area, Real(1e-30));
        if (cost < best_cost) { best_cost = cost; best_axis = axis; best_split = b; }
      }
    }

    std::size_t mid;
    if (best_axis < 0) {
      // Degenerate: every centroid is identical. SAH cannot separate them, so
      // split by count to keep leaves bounded in size.
      mid = begin + n / 2;
      best_axis = cbounds.longest_axis();
    } else {
      // Making a leaf is cheaper than splitting? Only allowed if it fits.
      if (best_cost >= Real(n) && n <= 16) return make_leaf(node_idx, begin, n);
      const auto ax = static_cast<std::size_t>(best_axis);
      const Real lo = cbounds.min[ax];
      const Real scale = kBins / (cbounds.max[ax] - lo);
      auto it = std::partition(prims_.begin() + static_cast<std::ptrdiff_t>(begin),
                               prims_.begin() + static_cast<std::ptrdiff_t>(end),
                               [&](const BuildPrim& p) { return bin_of(p.centroid[ax], lo, scale) <= best_split; });
      mid = static_cast<std::size_t>(it - prims_.begin());
      if (mid == begin || mid == end) mid = begin + n / 2;  // floating-point safety net
    }

    nodes_[node_idx].axis = static_cast<std::uint8_t>(best_axis);
    build(begin, mid, depth + 1);
    const auto right = nodes_.size();
    build(mid, end, depth + 1);
    nodes_[node_idx].offset = static_cast<std::uint32_t>(right);
    nodes_[node_idx].count = 0;
  }

 private:
  static int bin_of(Real c, Real lo, Real scale) {
    const int b = static_cast<int>((c - lo) * scale);
    return std::clamp(b, 0, kBins - 1);
  }

  void make_leaf(std::size_t node_idx, std::size_t begin, std::size_t n) {
    if (n > 0xFFFF) throw std::runtime_error("BVH leaf too large");
    nodes_[node_idx].offset = static_cast<std::uint32_t>(begin);
    nodes_[node_idx].count = static_cast<std::uint16_t>(n);
    st_.leaves++;
    st_.max_leaf_size = std::max(st_.max_leaf_size, n);
  }

  std::vector<BuildPrim>& prims_;
  std::vector<BvhNode>& nodes_;
  BvhBuildStats& st_;
  std::size_t max_leaf_;
};

}  // namespace

void Bvh::build(const std::vector<AABB>& prim_bounds, int max_leaf_size) {
  const auto t0 = std::chrono::steady_clock::now();
  nodes_.clear();
  prim_indices_.clear();
  stats_ = {};

  std::vector<BuildPrim> prims;
  prims.reserve(prim_bounds.size());
  for (std::size_t i = 0; i < prim_bounds.size(); ++i) {
    const AABB& b = prim_bounds[i];
    if (b.empty() || !b.min.is_finite() || !b.max.is_finite()) continue;
    prims.push_back({b, b.centroid(), static_cast<std::uint32_t>(i)});
  }
  if (prims.empty()) return;

  nodes_.reserve(2 * prims.size() / static_cast<std::size_t>(std::max(1, max_leaf_size)) + 1);
  Builder(prims, nodes_, stats_, max_leaf_size).build(0, prims.size(), 0);

  prim_indices_.resize(prims.size());
  for (std::size_t i = 0; i < prims.size(); ++i) prim_indices_[i] = prims[i].index;

  // SAH cost of the final tree (for reporting): sum over nodes of
  // area(node)/area(root) * (traversal cost or primitive count).
  const Real root_area = std::max(nodes_[0].bounds.surface_area(), Real(1e-30));
  for (const BvhNode& n : nodes_) {
    const Real rel = n.bounds.surface_area() / root_area;
    stats_.sah_cost += rel * (n.is_leaf() ? Real(n.count) : kTraversalCost);
  }
  stats_.nodes = nodes_.size();
  stats_.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool Bvh::validate(const std::vector<AABB>& prim_bounds) const {
  auto contains = [](const AABB& outer, const AABB& inner) {
    const Real e = 1e-9;
    return inner.min.x >= outer.min.x - e && inner.min.y >= outer.min.y - e && inner.min.z >= outer.min.z - e &&
           inner.max.x <= outer.max.x + e && inner.max.y <= outer.max.y + e && inner.max.z <= outer.max.z + e;
  };
  std::vector<int> seen(prim_bounds.size(), 0);
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    const BvhNode& n = nodes_[i];
    if (n.is_leaf()) {
      for (std::uint32_t k = 0; k < n.count; ++k) {
        const auto p = prim_indices_[n.offset + k];
        if (!contains(n.bounds, prim_bounds[p])) return false;
        seen[p]++;
      }
    } else {
      if (n.offset >= nodes_.size() || n.offset <= i) return false;
      if (!contains(n.bounds, nodes_[i + 1].bounds) || !contains(n.bounds, nodes_[n.offset].bounds)) return false;
    }
  }
  for (std::size_t p = 0; p < prim_bounds.size(); ++p) {
    const bool valid = !prim_bounds[p].empty();
    if (seen[p] != (valid ? 1 : 0)) return false;
  }
  return true;
}

}  // namespace rt
