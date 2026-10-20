#include "raytracer/materials/material.hpp"

#include <cmath>

#include "raytracer/math/sampling.hpp"

namespace rt {

namespace {
template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
}  // namespace

Color Texture::sample(const Point3& p) const {
  if (checker_scale <= 0) return a;
  // The small offset matters: surfaces lying exactly on a cell boundary (a
  // ground plane at y = 0) have hit points with y = +-1e-17 from rounding,
  // and floor() of that flips between 0 and -1, producing a speckled seam.
  constexpr Real kOffset = 1e-4;
  const auto parity = static_cast<long long>(std::floor(p.x * checker_scale + kOffset)) +
                      static_cast<long long>(std::floor(p.y * checker_scale + kOffset)) +
                      static_cast<long long>(std::floor(p.z * checker_scale + kOffset));
  return (parity & 1) ? b : a;
}

Real schlick(Real cos_theta, Real eta_ratio) {
  Real r0 = (1 - eta_ratio) / (1 + eta_ratio);
  r0 *= r0;
  const Real m = 1 - cos_theta;
  return r0 + (1 - r0) * m * m * m * m * m;
}

bool scatter(const Material& mat, const Ray& in, const HitRecord& rec, Rng& rng, ScatterResult& out) {
  return std::visit(overloaded{
      [&](const Lambertian& m) {
        // Cosine-weighted sampling: f*cos/pdf = (albedo/pi) * cos / (cos/pi) = albedo.
        const Onb onb(rec.normal);
        out.direction = onb.to_world(cosine_hemisphere(rng));
        // With smooth shading normals a sample can dip below the true surface.
        if (dot(out.direction, rec.geometric_normal) <= 0) return false;
        out.weight = m.albedo.sample(rec.p);
        out.specular = false;
        return true;
      },
      [&](const Metal& m) {
        const Vec3 unit_in = normalize(in.direction);
        Vec3 d = reflect(unit_in, rec.normal);
        if (m.roughness > 0) d = normalize(normalize(d) + m.roughness * random_unit_vector(rng));
        if (dot(d, rec.geometric_normal) <= 0) return false;  // absorbed below the surface
        out.direction = d;
        out.weight = m.albedo;
        out.specular = true;
        return true;
      },
      [&](const Dielectric& m) {
        const Real eta = rec.front_face ? (1.0 / m.ior) : m.ior;  // n_i / n_t
        const Vec3 unit_in = normalize(in.direction);
        const Real cos_i = std::fmin(dot(-unit_in, rec.normal), 1.0);
        Vec3 refracted;
        const bool can_refract = refract(unit_in, rec.normal, eta, refracted);
        // Choosing reflection with probability F (instead of splitting the
        // path) keeps the weight equal to the tint: F * (1/F) = 1.
        if (!can_refract || rng.uniform() < schlick(cos_i, eta)) {
          out.direction = reflect(unit_in, rec.normal);
        } else {
          out.direction = normalize(refracted);
        }
        out.weight = m.tint;
        out.specular = true;
        return true;
      },
      [&](const Emissive&) { return false; },
  }, mat);
}

Color emission(const Material& m) {
  if (const auto* e = std::get_if<Emissive>(&m)) return e->color * e->intensity;
  return {};
}

bool emits_back_face(const Material& m) {
  const auto* e = std::get_if<Emissive>(&m);
  return e && e->two_sided;
}

Color emitted(const Material& m, bool front_face) {
  if (const auto* e = std::get_if<Emissive>(&m))
    if (front_face || e->two_sided) return e->color * e->intensity;
  return {};
}

bool is_diffuse(const Material& m) { return std::holds_alternative<Lambertian>(m); }

Color eval_brdf_cos(const Material& m, const HitRecord& rec, const Vec3& wi) {
  const auto* l = std::get_if<Lambertian>(&m);
  if (!l) return {};
  const Real cos_t = dot(rec.normal, wi);
  if (cos_t <= 0 || dot(rec.geometric_normal, wi) <= 0) return {};
  return l->albedo.sample(rec.p) * (cos_t * kInvPi);
}

Color base_color(const Material& mat, const Point3& p) {
  return std::visit(overloaded{
      [&](const Lambertian& m) { return m.albedo.sample(p); },
      [](const Metal& m) { return m.albedo; },
      [](const Dielectric& m) { return m.tint; },
      [](const Emissive& m) { return m.color; },
  }, mat);
}

}  // namespace rt
