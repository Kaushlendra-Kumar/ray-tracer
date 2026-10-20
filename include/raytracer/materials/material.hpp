#pragma once
// Materials are plain values held in a std::variant. Compared with a virtual
// class hierarchy this gives value semantics (copyable, comparable,
// serialisable), contiguous storage in the scene, and exhaustive std::visit
// dispatch: forgetting to handle a new material type is a compile error.
#include <variant>

#include "raytracer/core/hit.hpp"
#include "raytracer/math/rng.hpp"

namespace rt {

// Solid colour or a 3D checkerboard (when checker_scale > 0).
struct Texture {
  Color a{0.5};
  Color b{0.5};
  Real checker_scale = 0;
  Color sample(const Point3& p) const;
  bool operator==(const Texture&) const = default;
};

struct Lambertian {
  Texture albedo;
  bool operator==(const Lambertian&) const = default;
};

// Mirror reflection perturbed by `roughness` (0 = perfect mirror). The fuzzy
// lobe has no closed-form BRDF, so it is treated as a sample-only lobe: it is
// never combined with light sampling, which keeps the estimator unbiased for
// the material model it defines (see docs/rendering_pipeline.md).
struct Metal {
  Color albedo{0.9};
  Real roughness = 0;
  bool operator==(const Metal&) const = default;
};

// Smooth glass-like interface with index of refraction `ior` (relative to the
// medium outside, assumed vacuum/air = 1) and an optional tint.
struct Dielectric {
  Real ior = 1.5;
  Color tint{1};
  bool operator==(const Dielectric&) const = default;
};

// Diffuse area emitter. Radiance = color * intensity, emitted from the front
// face only (the side the outward normal points to) unless two_sided.
// One-sided is the physically sensible default: a two-sided ceiling light
// would also blast the ceiling a few millimetres above it.
struct Emissive {
  Color color{1};
  Real intensity = 1;
  bool two_sided = false;
  bool operator==(const Emissive&) const = default;
};

using Material = std::variant<Lambertian, Metal, Dielectric, Emissive>;

struct ScatterResult {
  Vec3 direction;    // unit direction of the scattered ray
  Color weight;      // f * |cos| / pdf  (the throughput multiplier)
  bool specular;     // delta-like or sample-only lobe: skip light sampling
};

// Sample an outgoing direction. Returns false if the path is absorbed.
bool scatter(const Material& m, const Ray& in, const HitRecord& rec, Rng& rng, ScatterResult& out);

// Radiance emitted towards the viewer at a hit (respects one-sidedness).
Color emitted(const Material& m, bool front_face);
// Emitted radiance ignoring orientation (is this material a light at all?).
Color emission(const Material& m);
bool emits_back_face(const Material& m);

// True if the material has a non-delta BRDF we can evaluate for light sampling.
bool is_diffuse(const Material& m);

// BRDF * cos(theta_i) for diffuse materials (light sampling). Zero otherwise.
Color eval_brdf_cos(const Material& m, const HitRecord& rec, const Vec3& wi);

// Fresnel reflectance via Schlick's approximation.
Real schlick(Real cos_theta, Real eta_ratio);

// Albedo-ish colour used by the direct-lighting integrator and normals preview.
Color base_color(const Material& m, const Point3& p);

}  // namespace rt
