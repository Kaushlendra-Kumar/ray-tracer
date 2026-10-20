#include "raytracer/rendering/integrator.hpp"

#include <algorithm>
#include <cmath>

#include "raytracer/math/sampling.hpp"

namespace rt {

namespace {

// Contribution of one specific light, sampled once. Returns f * Le * G / pdf
// (without the light-selection probability).
Color sample_one_light(const Scene& scene, const LightRef& light, const HitRecord& rec, const Material& mat,
                       Rng& rng, PathStats& st) {
  const Point3 p = rec.p;
  Vec3 wi;
  Real dist = 0;
  Color radiance_over_pdf;  // Li / pdf(wi) (or I / r^2 for delta point lights)

  if (light.kind == LightRef::Kind::Point) {
    const PointLight& L = scene.point_lights[light.index];
    const Vec3 d = L.position - p;
    const Real d2 = d.length_squared();
    if (d2 < 1e-16) return {};
    dist = std::sqrt(d2);
    wi = d / dist;
    radiance_over_pdf = L.intensity / d2;  // delta light: no pdf, inverse-square falloff
  } else {
    const Primitive& prim = scene.primitives[light.index];
    if (const auto* sph = std::get_if<Sphere>(&prim)) {
      // Sample the cone of directions subtended by the sphere: every sample
      // hits the light, which gives far lower variance than area sampling.
      const Vec3 oc = sph->center - p;
      const Real d2 = oc.length_squared();
      const Real r2 = sph->radius * sph->radius;
      if (d2 <= r2 * (1 + 1e-9)) {
        // Shading point inside the sphere: the cone covers everything, so
        // sample the surface uniformly instead (only a two-sided emitter
        // lights its interior).
        const Material& lm = scene.material(sph->material);
        if (!emits_back_face(lm)) return {};
        const Vec3 n_l = random_unit_vector(rng);
        const Vec3 d = sph->center + sph->radius * n_l - p;
        const Real dd2 = d.length_squared();
        if (dd2 < 1e-16) return {};
        dist = std::sqrt(dd2);
        wi = d / dist;
        const Real cos_l = std::fabs(dot(n_l, wi));
        if (cos_l < 1e-9) return {};
        radiance_over_pdf = emission(lm) * (cos_l * sph->area() / dd2);
      } else {
      const Real x = r2 / d2;
      const Real one_minus_cos = x / (1 + std::sqrt(1 - x));  // 1 - sqrt(1-x), cancellation-free
      const Real cos_max = 1 - one_minus_cos;
      wi = Onb(oc / std::sqrt(d2)).to_world(uniform_cone(rng, cos_max));
      HitRecord lh;
      if (!sph->intersect(Ray(p, wi), Interval(0, kInfinity), lh)) return {};  // grazing numerical miss
      dist = lh.t;
      const Real pdf = 1 / (2 * kPi * one_minus_cos);
      radiance_over_pdf = emission(scene.material(sph->material)) / pdf;  // seen from outside: front face
      }
    } else {
      const Triangle& tri = std::get<Triangle>(prim);
      const Vec2 b = uniform_triangle(rng);
      const Point3 q = (1 - b.x - b.y) * tri.v0 + b.x * tri.v1 + b.y * tri.v2;
      const Vec3 d = q - p;
      const Real d2 = d.length_squared();
      if (d2 < 1e-16) return {};
      dist = std::sqrt(d2);
      wi = d / dist;
      const Vec3 nl = normalize(cross(tri.v1 - tri.v0, tri.v2 - tri.v0));
      const Material& lm = scene.material(tri.material);
      const Real cos_signed = -dot(nl, wi);  // > 0 when we see the light's front face
      if (cos_signed <= 0 && !emits_back_face(lm)) return {};
      const Real cos_l = std::fabs(cos_signed);
      if (cos_l < 1e-9) return {};
      // Convert the uniform-area pdf (1/A) to solid angle: pdf = d^2 / (cos_l A).
      const Real pdf = d2 / (cos_l * tri.area());
      radiance_over_pdf = emission(lm) / pdf;
    }
  }

  const Color f = eval_brdf_cos(mat, rec, wi);
  if (f.is_zero() || radiance_over_pdf.is_zero()) return {};
  const Point3 origin = offset_ray_origin(p, rec.geometric_normal, wi);
  const Real shadow_tmax = std::max((p + dist * wi - origin).length() - 4 * tol::spawn_offset(p + dist * wi), Real(0));
  st.shadow_rays++;
  if (scene.occluded(Ray(origin, wi), shadow_tmax)) return {};
  return f * radiance_over_pdf;
}

bool is_sampled_light(const Scene& scene, const HitRecord& rec) {
  return !rec.is_plane && rec.primitive != kNoPrimitive && scene.is_sampled_light[rec.primitive];
}

Color path_trace(const Scene& scene, Ray ray, Rng& rng, PathStats& st) {
  const RenderSettings& cfg = scene.settings;
  Color L{}, beta{1};
  bool specular_bounce = true;  // camera rays behave like a specular bounce
  for (int depth = 0; depth < cfg.max_depth; ++depth) {
    HitRecord rec;
    st.rays++;
    if (!scene.intersect(ray, Interval(tol::kRayTMin, kInfinity), rec)) {
      L += beta * scene.background.eval(ray.direction);
      break;
    }
    const Material& mat = scene.material(rec.material);

    // Emission. If the previous vertex already sampled this light explicitly
    // (NEE), counting it again here would double-count its contribution.
    const Color Le = emitted(mat, rec.front_face);
    if (!Le.is_zero() && (specular_bounce || !cfg.light_sampling || !is_sampled_light(scene, rec))) L += beta * Le;

    ScatterResult sr;
    if (!scatter(mat, ray, rec, rng, sr)) break;

    if (cfg.light_sampling && !sr.specular && !scene.lights.empty()) {
      // Uniform light selection: estimator = contribution / (1/N).
      const auto n = static_cast<std::uint32_t>(scene.lights.size());
      const LightRef& light = scene.lights[rng.below(n)];
      L += beta * sample_one_light(scene, light, rec, mat, rng, st) * Real(n);
    }

    beta *= sr.weight;
    specular_bounce = sr.specular;
    if (beta.is_zero()) break;

    // Russian roulette: terminate low-throughput paths with probability 1-q
    // and boost survivors by 1/q, which keeps the estimator unbiased.
    if (depth + 1 >= cfg.rr_min_depth) {
      const Real q = std::clamp(beta.max_component(), Real(0.05), Real(0.95));
      if (rng.uniform() >= q) { st.rr_terminated++; break; }
      beta /= q;
    }
    ray = Ray(offset_ray_origin(rec.p, rec.geometric_normal, sr.direction), sr.direction);
  }
  return L;
}

Color direct_trace(const Scene& scene, const Ray& ray, Rng& rng, PathStats& st, int depth) {
  st.rays++;
  HitRecord rec;
  if (!scene.intersect(ray, Interval(tol::kRayTMin, kInfinity), rec)) return scene.background.eval(ray.direction);
  const Material& mat = scene.material(rec.material);
  Color L = emitted(mat, rec.front_face);
  if (depth >= scene.settings.max_depth) return L;

  if (is_diffuse(mat)) {
    // Every light contributes (one sample each): hard shadows from point
    // lights, slightly noisy soft shadows from area lights.
    for (const LightRef& light : scene.lights) L += sample_one_light(scene, light, rec, mat, rng, st);
    return L;
  }
  auto spawn = [&](const Vec3& d) { return Ray(offset_ray_origin(rec.p, rec.geometric_normal, d), d); };
  if (const auto* m = std::get_if<Metal>(&mat)) {
    ScatterResult sr;
    if (scatter(mat, ray, rec, rng, sr)) L += m->albedo * direct_trace(scene, spawn(sr.direction), rng, st, depth + 1);
    return L;
  }
  if (const auto* d = std::get_if<Dielectric>(&mat)) {
    // Deterministic split into reflected and refracted rays weighted by Fresnel.
    const Vec3 in = normalize(ray.direction);
    const Real eta = rec.front_face ? 1.0 / d->ior : d->ior;
    const Real cos_i = std::fmin(dot(-in, rec.normal), 1.0);
    Vec3 refracted;
    const bool can = refract(in, rec.normal, eta, refracted);
    const Real F = can ? schlick(cos_i, eta) : 1.0;
    L += d->tint * F * direct_trace(scene, spawn(reflect(in, rec.normal)), rng, st, depth + 1);
    if (can) L += d->tint * (1 - F) * direct_trace(scene, spawn(normalize(refracted)), rng, st, depth + 1);
    return L;
  }
  return L;
}

}  // namespace

Color sample_direct_light(const Scene& scene, const HitRecord& rec, const Material& mat, Rng& rng, PathStats& st) {
  Color sum{};
  for (const LightRef& l : scene.lights) sum += sample_one_light(scene, l, rec, mat, rng, st);
  return sum;
}

Color radiance(const Scene& scene, const Ray& r, Rng& rng, PathStats& st) {
  switch (scene.settings.integrator) {
    case Integrator::Path: return path_trace(scene, r, rng, st);
    case Integrator::Direct: return direct_trace(scene, r, rng, st, 0);
    case Integrator::Normals: {
      st.rays++;
      HitRecord rec;
      if (!scene.intersect(r, Interval(tol::kRayTMin, kInfinity), rec)) return Color{};
      // Outward-facing normal mapped from [-1,1] to [0,1].
      const Vec3 n = rec.front_face ? rec.normal : -rec.normal;
      return 0.5 * (n + Vec3{1});
    }
  }
  return {};
}

}  // namespace rt
