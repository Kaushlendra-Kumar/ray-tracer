#pragma once
#include <variant>
#include <vector>

#include "raytracer/core/scene_desc.hpp"
#include "raytracer/geometry/bvh.hpp"
#include "raytracer/geometry/shapes.hpp"

namespace rt {

using Primitive = std::variant<Sphere, Triangle>;

struct PointLight {
  Point3 position;
  Color intensity;
};

// A light the integrator can sample directly: a point light or an emissive
// bounded primitive (sphere / triangle).
struct LightRef {
  enum class Kind { Point, Emitter } kind;
  std::uint32_t index;  // into point_lights or primitives
};

struct SceneBuildInfo {
  std::size_t triangles = 0, spheres = 0, planes = 0, degenerate_skipped = 0, lights = 0;
  BvhBuildStats bvh;
};

// Immutable, render-ready scene. All members are values: copying a Scene is a
// deep copy, and the renderer only ever holds `const Scene&`, so any number of
// worker threads can read it concurrently without synchronisation.
class Scene {
 public:
  static Scene compile(const SceneDesc& desc);

  bool intersect(const Ray& r, Interval t, HitRecord& rec, TraversalCounters* ctr = nullptr) const;
  bool occluded(const Ray& r, Real t_max, TraversalCounters* ctr = nullptr) const;

  // Brute-force reference used by tests/benchmarks (ignores the BVH).
  bool intersect_brute(const Ray& r, Interval t, HitRecord& rec, TraversalCounters* ctr = nullptr) const;

  const Material& material(MaterialId id) const { return materials[id]; }

  Camera camera;
  RenderSettings settings;
  Background background;
  std::vector<Material> materials;
  std::vector<Primitive> primitives;  // bounded primitives (inside the BVH)
  std::vector<Plane> planes;          // unbounded, tested linearly
  std::vector<PointLight> point_lights;
  std::vector<LightRef> lights;
  std::vector<std::uint8_t> is_sampled_light;  // per primitive: part of `lights`
  Bvh bvh;
  SceneBuildInfo info;
};

}  // namespace rt
