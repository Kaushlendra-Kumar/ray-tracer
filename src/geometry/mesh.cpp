#include "raytracer/geometry/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace rt {

void TriangleMesh::compute_smooth_normals() {
  normals.assign(positions.size(), Vec3{});
  for (const auto& f : faces) {
    const Vec3 a = positions[f[0]], b = positions[f[1]], c = positions[f[2]];
    const Vec3 n = cross(b - a, c - a);  // length = 2 * area => area weighting
    for (auto i : f) normals[i] += n;
  }
  for (auto& n : normals) n = normalize_or(n, Vec3{0, 1, 0});
}

Point3 MeshTransform::apply_point(const Point3& p) const {
  const Vec3 s = p * scale;
  const Real a = rotate_y_deg * kPi / 180, c = std::cos(a), sn = std::sin(a);
  return Vec3{c * s.x + sn * s.z, s.y, -sn * s.x + c * s.z} + translate;
}

Vec3 MeshTransform::apply_normal(const Vec3& n) const {
  // Linear part M = R * S, so (M^-1)^T = R * S^-1 (R is orthonormal).
  const Vec3 s{n.x / scale.x, n.y / scale.y, n.z / scale.z};
  const Real a = rotate_y_deg * kPi / 180, c = std::cos(a), sn = std::sin(a);
  return normalize_or(Vec3{c * s.x + sn * s.z, s.y, -sn * s.x + c * s.z}, n);
}

TriangleMesh make_icosphere(int subdivisions) {
  TriangleMesh m;
  const Real t = (1 + std::sqrt(5.0)) / 2;
  m.positions = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                 {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (auto& p : m.positions) p = normalize(p);
  m.faces = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
             {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
             {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (int s = 0; s < subdivisions; ++s) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoint;
    auto mid = [&](std::uint32_t a, std::uint32_t b) {
      const auto key = std::minmax(a, b);
      if (auto it = midpoint.find(key); it != midpoint.end()) return it->second;
      m.positions.push_back(normalize(m.positions[a] + m.positions[b]));
      const auto idx = static_cast<std::uint32_t>(m.positions.size() - 1);
      midpoint.emplace(key, idx);
      return idx;
    };
    std::vector<std::array<std::uint32_t, 3>> next;
    next.reserve(m.faces.size() * 4);
    for (const auto& f : m.faces) {
      const auto a = mid(f[0], f[1]), b = mid(f[1], f[2]), c = mid(f[2], f[0]);
      next.push_back({f[0], a, c});
      next.push_back({f[1], b, a});
      next.push_back({f[2], c, b});
      next.push_back({a, b, c});
    }
    m.faces = std::move(next);
  }
  m.normals = m.positions;  // exact normals of the unit sphere
  return m;
}

TriangleMesh make_torus(Real major, Real minor, int segments_u, int segments_v) {
  TriangleMesh m;
  const int su = std::max(3, segments_u), sv = std::max(3, segments_v);
  for (int i = 0; i < su; ++i) {
    const Real u = 2 * kPi * i / su;
    for (int j = 0; j < sv; ++j) {
      const Real v = 2 * kPi * j / sv;
      const Vec3 dir{std::cos(u), 0, std::sin(u)};
      const Vec3 n = std::cos(v) * dir + Vec3{0, std::sin(v), 0};
      m.positions.push_back(major * dir + minor * n);
      m.normals.push_back(n);
    }
  }
  auto idx = [&](int i, int j) { return static_cast<std::uint32_t>(((i % su) * sv) + (j % sv)); };
  for (int i = 0; i < su; ++i)
    for (int j = 0; j < sv; ++j) {
      // Winding chosen so that cross(e1, e2) points outward.
      m.faces.push_back({idx(i, j), idx(i, j + 1), idx(i + 1, j)});
      m.faces.push_back({idx(i + 1, j), idx(i, j + 1), idx(i + 1, j + 1)});
    }
  return m;
}

}  // namespace rt
