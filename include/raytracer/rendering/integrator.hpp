#pragma once
#include <cstdint>

#include "raytracer/core/scene.hpp"
#include "raytracer/math/rng.hpp"

namespace rt {

// Per-worker counters; merged into the shared statistics once per tile so the
// hot loop never touches shared (contended) cache lines.
struct PathStats {
  std::uint64_t rays = 0;          // closest-hit rays (camera + bounces)
  std::uint64_t shadow_rays = 0;   // any-hit visibility rays
  std::uint64_t rr_terminated = 0; // paths ended by Russian roulette
  std::uint64_t invalid_samples = 0;
};

// Radiance arriving along `r` according to the scene's selected integrator:
//   Path    - unbiased Monte Carlo path tracing with next-event estimation
//   Direct  - Whitted-style: direct light + recursive perfect specular
//   Normals - debug visualisation of shading normals
Color radiance(const Scene& scene, const Ray& r, Rng& rng, PathStats& st);

// Exposed for tests: one-sample estimate of direct light at a diffuse hit.
Color sample_direct_light(const Scene& scene, const HitRecord& rec, const Material& mat, Rng& rng, PathStats& st);

}  // namespace rt
