#pragma once
// SceneDesc is the *editable description* of a scene: what the JSON file and
// the browser editor talk about. It is compiled into a `Scene` (flattened
// primitives + BVH + light list) before rendering. Keeping the two separate
// means parsing/validation never touches acceleration structures, and the
// renderer never sees names, strings, or unresolved references.
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "raytracer/core/camera.hpp"
#include "raytracer/materials/material.hpp"

namespace rt {

enum class Integrator { Path, Direct, Normals };
enum class ToneMap { Clamp, Reinhard, Aces };

struct RenderSettings {
  int width = 640;
  int height = 360;
  int spp = 64;            // samples per pixel
  int max_depth = 8;       // maximum path length (bounces)
  int rr_min_depth = 3;    // Russian roulette starts after this many bounces
  Integrator integrator = Integrator::Path;
  bool light_sampling = true;  // next-event estimation (direct light sampling)
  std::uint64_t seed = 1;
  ToneMap tone_map = ToneMap::Aces;
  Real exposure = 1.0;
  int threads = 0;         // 0 = hardware concurrency
  int tile_size = 16;
  bool use_bvh = true;     // false = brute force (reference/benchmark)
  bool operator==(const RenderSettings&) const = default;
};

struct Background {
  enum class Kind { Gradient, Solid } kind = Kind::Gradient;
  Color top{0.5, 0.7, 1.0};
  Color bottom{1, 1, 1};
  Real intensity = 1;
  Color eval(const Vec3& dir) const;
  bool operator==(const Background&) const = default;
};

struct SphereDesc { Point3 center; Real radius = 1; bool operator==(const SphereDesc&) const = default; };
struct PlaneDesc { Point3 point; Vec3 normal{0, 1, 0}; bool operator==(const PlaneDesc&) const = default; };
struct TriangleDesc { Point3 v0, v1{1, 0, 0}, v2{0, 1, 0}; bool operator==(const TriangleDesc&) const = default; };
// Parallelogram corner + u + v, made of two triangles (normal = u x v).
struct QuadDesc { Point3 corner; Vec3 u{1, 0, 0}, v{0, 0, 1}; bool operator==(const QuadDesc&) const = default; };
// Axis-aligned box (min/max corners) optionally rotated about its vertical
// centre axis, made of 12 triangles.
struct BoxDesc { Point3 min{-0.5}, max{0.5}; Real rotate_y = 0; bool operator==(const BoxDesc&) const = default; };
struct MeshDesc {
  enum class Source { Icosphere, Torus, ObjFile } source = Source::Icosphere;
  int subdivisions = 3;          // icosphere
  Real major_radius = 1, minor_radius = 0.35;  // torus
  int segments = 48;             // torus (u segments; v = segments/2)
  std::string path;              // OBJ file
  bool smooth = true;            // use per-vertex normals
  Vec3 scale{1, 1, 1};
  Real rotate_y = 0;             // degrees
  Vec3 translate{0, 0, 0};
  bool operator==(const MeshDesc&) const = default;
};

struct ObjectDesc {
  std::string name;
  std::string material;
  std::variant<SphereDesc, PlaneDesc, TriangleDesc, QuadDesc, BoxDesc, MeshDesc> shape;
  bool operator==(const ObjectDesc&) const = default;
};

struct PointLightDesc {
  std::string name;
  Point3 position;
  Color intensity{10};  // radiant intensity (W/sr per channel)
  bool operator==(const PointLightDesc&) const = default;
};

struct NamedMaterial {
  std::string name;
  Material material;
  bool operator==(const NamedMaterial&) const = default;
};

struct SceneDesc {
  std::string name = "untitled";
  CameraDesc camera;
  RenderSettings render;
  Background background;
  std::vector<NamedMaterial> materials;
  std::vector<ObjectDesc> objects;
  std::vector<PointLightDesc> lights;
  std::string base_dir;  // for resolving relative OBJ paths (not serialised)

  const NamedMaterial* find_material(const std::string& name) const;
  bool operator==(const SceneDesc& o) const {
    return name == o.name && camera == o.camera && render == o.render && background == o.background &&
           materials == o.materials && objects == o.objects && lights == o.lights;
  }
};

}  // namespace rt
