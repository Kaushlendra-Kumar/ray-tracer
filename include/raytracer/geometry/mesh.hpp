#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "raytracer/math/vec3.hpp"

namespace rt {

// Indexed triangle mesh (shared vertices). Converted into independent
// Triangle primitives when the scene is compiled, because the BVH and the
// intersection code operate on self-contained triangles.
struct TriangleMesh {
  std::vector<Point3> positions;
  std::vector<Vec3> normals;  // per-vertex; empty or same size as positions
  std::vector<std::array<std::uint32_t, 3>> faces;

  std::size_t triangle_count() const { return faces.size(); }
  void compute_smooth_normals();  // area-weighted vertex normals
};

struct MeshTransform {
  Vec3 scale{1, 1, 1};
  Real rotate_y_deg = 0;
  Vec3 translate{0, 0, 0};
  Point3 apply_point(const Point3& p) const;
  Vec3 apply_normal(const Vec3& n) const;  // inverse-transpose of the linear part
};

// Procedural meshes (no external assets needed for demos and benchmarks).
TriangleMesh make_icosphere(int subdivisions);              // unit radius, 20*4^s faces
TriangleMesh make_torus(Real major, Real minor, int segments_u, int segments_v);

// Minimal Wavefront OBJ reader: `v`, `vn`, `f` (any of v, v/vt, v//vn,
// v/vt/vn, negative indices); polygons are fan-triangulated. Other
// statements are ignored. Throws std::runtime_error with a line number.
TriangleMesh load_obj(const std::string& path);
TriangleMesh parse_obj(const std::string& text);

}  // namespace rt
