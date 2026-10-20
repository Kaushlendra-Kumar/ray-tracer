#pragma once
#include <cmath>

#include "raytracer/math/ray.hpp"
#include "raytracer/math/sampling.hpp"

namespace rt {

struct CameraDesc {
  Point3 look_from{0, 0, 0};
  Point3 look_at{0, 0, -1};
  Vec3 vup{0, 1, 0};
  Real vfov_deg = 40;     // vertical field of view
  Real aperture = 0;      // lens diameter; 0 = pinhole
  Real focus_dist = 0;    // 0 = |look_at - look_from|
  bool operator==(const CameraDesc&) const = default;
};

// Thin-lens perspective camera.
//
// Basis: w points *backwards* (from look_at to look_from), u to the right,
// v up. The viewport sits at distance focus_dist along -w. Pixel (0,0) is the
// top-left corner of the image; rows increase downwards.
class Camera {
 public:
  Camera() = default;
  Camera(const CameraDesc& d, int width, int height) { setup(d, width, height); }

  void setup(const CameraDesc& d, int width, int height) {
    const Real aspect = Real(width) / Real(height);
    origin_ = d.look_from;
    const Vec3 view = d.look_at - d.look_from;
    w_ = normalize_or(-view, Vec3{0, 0, 1});
    // If vup is parallel to the view direction, pick another up vector.
    Vec3 up = normalize_or(d.vup, Vec3{0, 1, 0});
    if (std::fabs(dot(up, w_)) > 0.999999) up = std::fabs(w_.y) < 0.9 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    u_ = normalize(cross(up, w_));
    v_ = cross(w_, u_);

    const Real focus = d.focus_dist > 0 ? d.focus_dist : std::fmax(view.length(), 1e-3);
    const Real theta = std::clamp(d.vfov_deg, Real(0.1), Real(179)) * kPi / 180;
    const Real vp_h = 2 * std::tan(theta / 2) * focus;
    const Real vp_w = vp_h * aspect;

    const Vec3 vp_u = vp_w * u_;
    const Vec3 vp_v = -vp_h * v_;  // image rows go down
    du_ = vp_u / width;
    dv_ = vp_v / height;
    corner_ = origin_ - focus * w_ - vp_u / 2 - vp_v / 2;
    lens_radius_ = d.aperture / 2;
  }

  // Ray through pixel (i, j) at sub-pixel offset (sx, sy) in [0,1)^2.
  Ray generate(int i, int j, Real sx, Real sy, Rng& rng) const {
    const Point3 target = corner_ + (i + sx) * du_ + (j + sy) * dv_;
    Point3 o = origin_;
    if (lens_radius_ > 0) {
      const Vec2 d = random_in_unit_disk(rng);
      o = origin_ + lens_radius_ * (d.x * u_ + d.y * v_);
    }
    return Ray{o, normalize(target - o)};
  }

  const Vec3& u() const { return u_; }
  const Vec3& v() const { return v_; }
  const Vec3& w() const { return w_; }
  const Point3& origin() const { return origin_; }

 private:
  Point3 origin_, corner_;
  Vec3 du_, dv_, u_{1, 0, 0}, v_{0, 1, 0}, w_{0, 0, 1};
  Real lens_radius_ = 0;
};

}  // namespace rt
