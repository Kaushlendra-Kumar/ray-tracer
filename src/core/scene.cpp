#include "raytracer/core/scene.hpp"

#include <filesystem>
#include <stdexcept>

#include "raytracer/geometry/mesh.hpp"

namespace rt {

Color Background::eval(const Vec3& dir) const {
  if (kind == Kind::Solid) return top * intensity;
  const Real t = 0.5 * (normalize(dir).y + 1.0);
  return lerp(bottom, top, t) * intensity;
}

const NamedMaterial* SceneDesc::find_material(const std::string& n) const {
  for (const auto& m : materials)
    if (m.name == n) return &m;
  return nullptr;
}

namespace {
template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

AABB bounds_of(const Primitive& p) {
  return std::visit([](const auto& s) { return s.bounds(); }, p);
}
}  // namespace

Scene Scene::compile(const SceneDesc& desc) {
  Scene s;
  s.settings = desc.render;
  s.background = desc.background;
  s.camera.setup(desc.camera, desc.render.width, desc.render.height);

  // Materials are resolved by name once; primitives store compact indices.
  for (const auto& m : desc.materials) s.materials.push_back(m.material);
  auto material_id = [&](const std::string& name) -> MaterialId {
    for (std::size_t i = 0; i < desc.materials.size(); ++i)
      if (desc.materials[i].name == name) return static_cast<MaterialId>(i);
    throw std::runtime_error("unknown material '" + name + "'");
  };

  auto add_triangle = [&](Triangle t) {
    if (t.degenerate()) { s.info.degenerate_skipped++; return; }
    s.primitives.emplace_back(t);
    s.info.triangles++;
  };

  for (const auto& obj : desc.objects) {
    const MaterialId mat = material_id(obj.material);
    std::visit(overloaded{
        [&](const SphereDesc& d) {
          s.primitives.emplace_back(Sphere{d.center, d.radius, mat});
          s.info.spheres++;
        },
        [&](const PlaneDesc& d) {
          s.planes.push_back(Plane{d.point, normalize_or(d.normal, Vec3{0, 1, 0}), mat});
          s.info.planes++;
        },
        [&](const TriangleDesc& d) { add_triangle(Triangle{d.v0, d.v1, d.v2, {}, {}, {}, false, mat}); },
        [&](const QuadDesc& d) {
          const Point3 c = d.corner;
          add_triangle(Triangle{c, c + d.u, c + d.u + d.v, {}, {}, {}, false, mat});
          add_triangle(Triangle{c, c + d.u + d.v, c + d.v, {}, {}, {}, false, mat});
        },
        [&](const BoxDesc& d) {
          const Point3 c = 0.5 * (d.min + d.max);
          const MeshTransform xf{{1, 1, 1}, d.rotate_y, c};
          const Vec3 h = 0.5 * (d.max - d.min);
          auto P = [&](Real sx, Real sy, Real sz) { return xf.apply_point(Vec3{sx * h.x, sy * h.y, sz * h.z}); };
          // Each face: corner, and two edge directions ordered so u x v points outward.
          const Point3 v[8] = {P(-1, -1, -1), P(1, -1, -1), P(1, 1, -1), P(-1, 1, -1),
                               P(-1, -1, 1),  P(1, -1, 1),  P(1, 1, 1),  P(-1, 1, 1)};
          const int faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
          for (const auto& f : faces) {
            add_triangle(Triangle{v[f[0]], v[f[1]], v[f[2]], {}, {}, {}, false, mat});
            add_triangle(Triangle{v[f[0]], v[f[2]], v[f[3]], {}, {}, {}, false, mat});
          }
        },
        [&](const MeshDesc& d) {
          TriangleMesh mesh;
          switch (d.source) {
            case MeshDesc::Source::Icosphere: mesh = make_icosphere(d.subdivisions); break;
            case MeshDesc::Source::Torus:
              mesh = make_torus(d.major_radius, d.minor_radius, d.segments, std::max(3, d.segments / 2));
              break;
            case MeshDesc::Source::ObjFile: {
              std::filesystem::path p(d.path);
              if (p.is_relative() && !desc.base_dir.empty()) p = std::filesystem::path(desc.base_dir) / p;
              mesh = load_obj(p.string());
              break;
            }
          }
          if (d.smooth && mesh.normals.empty()) mesh.compute_smooth_normals();
          const MeshTransform xf{d.scale, d.rotate_y, d.translate};
          const bool smooth = d.smooth && !mesh.normals.empty();
          for (const auto& f : mesh.faces) {
            Triangle t;
            t.v0 = xf.apply_point(mesh.positions[f[0]]);
            t.v1 = xf.apply_point(mesh.positions[f[1]]);
            t.v2 = xf.apply_point(mesh.positions[f[2]]);
            if (smooth) {
              t.n0 = xf.apply_normal(mesh.normals[f[0]]);
              t.n1 = xf.apply_normal(mesh.normals[f[1]]);
              t.n2 = xf.apply_normal(mesh.normals[f[2]]);
              t.has_vertex_normals = true;
            }
            t.material = mat;
            add_triangle(t);
          }
        },
    }, obj.shape);
  }

  for (const auto& l : desc.lights) {
    s.lights.push_back({LightRef::Kind::Point, static_cast<std::uint32_t>(s.point_lights.size())});
    s.point_lights.push_back({l.position, l.intensity});
  }
  s.is_sampled_light.assign(s.primitives.size(), 0);
  for (std::size_t i = 0; i < s.primitives.size(); ++i) {
    const MaterialId m = std::visit([](const auto& p) { return p.material; }, s.primitives[i]);
    if (!emission(s.materials[m]).is_zero()) {
      s.lights.push_back({LightRef::Kind::Emitter, static_cast<std::uint32_t>(i)});
      s.is_sampled_light[i] = 1;
    }
  }
  s.info.lights = s.lights.size();

  std::vector<AABB> bounds;
  bounds.reserve(s.primitives.size());
  for (const auto& p : s.primitives) bounds.push_back(bounds_of(p));
  s.bvh.build(bounds, 4);
  s.info.bvh = s.bvh.stats();
  return s;
}

namespace {

// Candidate hit: just enough to finalise the winner after traversal.
struct Candidate {
  Real t;
  Vec2 extra;
  std::uint32_t index = kNoPrimitive;
};

inline bool hit_t(const Primitive& p, const Ray& r, Real t_min, Real t_max, Real& t, Vec2& extra) {
  return std::visit([&](const auto& s) { return s.hit_t(r, t_min, t_max, t, extra); }, p);
}

inline bool intersect_planes(const std::vector<Plane>& planes, const Ray& r, Real t_min, Real& t_max,
                             HitRecord& rec) {
  bool hit = false;
  for (std::size_t i = 0; i < planes.size(); ++i) {
    HitRecord tmp;
    if (planes[i].intersect(r, Interval(t_min, t_max), tmp)) {
      tmp.primitive = static_cast<std::uint32_t>(i);
      tmp.is_plane = true;
      rec = tmp;
      t_max = tmp.t;
      hit = true;
    }
  }
  return hit;
}

inline void finalize(const Primitive& p, const Ray& r, const Candidate& c, HitRecord& rec) {
  std::visit([&](const auto& s) { s.finalize(r, c.t, c.extra, rec); }, p);
  rec.primitive = c.index;
  rec.is_plane = false;
}

}  // namespace

bool Scene::intersect(const Ray& r, Interval t, HitRecord& rec, TraversalCounters* ctr) const {
  if (!settings.use_bvh) return intersect_brute(r, t, rec, ctr);
  Real t_max = t.max;
  const bool plane_hit = intersect_planes(planes, r, t.min, t_max, rec);
  Candidate best{t_max, {}, kNoPrimitive};
  bvh.closest(
      r, t.min, t_max,
      [&](std::uint32_t i, Real tm) {
        Real th;
        Vec2 e;
        if (hit_t(primitives[i], r, t.min, tm, th, e)) {
          best = {th, e, i};
          return th;
        }
        return tm;
      },
      ctr);
  if (best.index != kNoPrimitive) {
    finalize(primitives[best.index], r, best, rec);
    return true;
  }
  return plane_hit;
}

bool Scene::intersect_brute(const Ray& r, Interval t, HitRecord& rec, TraversalCounters* ctr) const {
  Real t_max = t.max;
  const bool plane_hit = intersect_planes(planes, r, t.min, t_max, rec);
  Candidate best{t_max, {}, kNoPrimitive};
  for (std::size_t i = 0; i < primitives.size(); ++i) {
    if (ctr) ++ctr->prim_tests;
    Real th;
    Vec2 e;
    if (hit_t(primitives[i], r, t.min, best.t, th, e)) best = {th, e, static_cast<std::uint32_t>(i)};
  }
  if (best.index != kNoPrimitive) {
    finalize(primitives[best.index], r, best, rec);
    return true;
  }
  return plane_hit;
}

bool Scene::occluded(const Ray& r, Real t_max, TraversalCounters* ctr) const {
  const Real t_min = tol::kRayTMin;
  HitRecord tmp;
  for (const auto& pl : planes)
    if (pl.intersect(r, Interval(t_min, t_max), tmp)) return true;
  Real th;
  Vec2 e;
  if (!settings.use_bvh) {
    for (const auto& p : primitives) {
      if (ctr) ++ctr->prim_tests;
      if (hit_t(p, r, t_min, t_max, th, e)) return true;
    }
    return false;
  }
  return bvh.any(
      r, t_min, t_max, [&](std::uint32_t i, Real tm) { return hit_t(primitives[i], r, t_min, tm, th, e) ? th : tm; },
      ctr);
}

}  // namespace rt
