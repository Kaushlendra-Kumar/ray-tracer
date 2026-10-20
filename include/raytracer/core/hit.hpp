#pragma once
#include <cstdint>

#include "raytracer/math/ray.hpp"

namespace rt {

using MaterialId = std::uint32_t;
inline constexpr std::uint32_t kNoPrimitive = 0xFFFFFFFFu;

// Everything the shading code needs to know about a ray-surface hit.
struct HitRecord {
  Real t = kInfinity;
  Point3 p;
  Vec3 normal;            // shading normal, flipped to face the incoming ray
  Vec3 geometric_normal;  // true surface normal, flipped to face the incoming ray
  Vec2 uv;
  bool front_face = true; // ray hit the side the outward normal points to
  MaterialId material = 0;
  std::uint32_t primitive = kNoPrimitive;  // index into Scene::primitives (or plane id)
  bool is_plane = false;

  // `outward` is the outward-facing geometric normal (unit length).
  void set_face_normal(const Ray& r, const Vec3& outward) {
    front_face = dot(r.direction, outward) < 0;
    geometric_normal = front_face ? outward : -outward;
    normal = geometric_normal;
  }
};

}  // namespace rt
