#include "raytracer/io/scene_io.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rt {

const char* to_string(Integrator i) {
  switch (i) {
    case Integrator::Path: return "path";
    case Integrator::Direct: return "direct";
    case Integrator::Normals: return "normals";
  }
  return "path";
}
const char* to_string(ToneMap t) {
  switch (t) {
    case ToneMap::Clamp: return "clamp";
    case ToneMap::Reinhard: return "reinhard";
    case ToneMap::Aces: return "aces";
  }
  return "aces";
}

namespace {

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

// Typed accessors that produce field-qualified error messages.
class Reader {
 public:
  Reader(const Json& j, std::string path) : j_(j), path_(std::move(path)) {
    if (!j_.is_object()) fail("expected an object");
    for (const auto& kv : j_.as_object()) (void)kv;
  }
  [[noreturn]] void fail(const std::string& msg) const { throw SceneError((path_.empty() ? "scene" : path_) + ": " + msg); }
  std::string at(const std::string& key) const { return path_.empty() ? key : path_ + "." + key; }

  const Json* get(const std::string& key) const { return j_.find(key); }

  Real number(const std::string& key, Real def) const {
    const Json* v = get(key);
    if (!v) return def;
    if (!v->is_number()) throw SceneError(at(key) + ": expected a number");
    return v->as_number();
  }
  int integer(const std::string& key, int def) const {
    const Json* v = get(key);
    if (!v) return def;
    if (!v->is_number()) throw SceneError(at(key) + ": expected an integer");
    const double d = v->as_number();
    if (d != std::floor(d) || std::fabs(d) > 2e9) throw SceneError(at(key) + ": expected an integer");
    return static_cast<int>(d);
  }
  bool boolean(const std::string& key, bool def) const {
    const Json* v = get(key);
    if (!v) return def;
    if (!v->is_bool()) throw SceneError(at(key) + ": expected true/false");
    return v->as_bool();
  }
  std::string string(const std::string& key, const std::string& def) const {
    const Json* v = get(key);
    if (!v) return def;
    if (!v->is_string()) throw SceneError(at(key) + ": expected a string");
    return v->as_string();
  }
  std::string required_string(const std::string& key) const {
    if (!get(key)) throw SceneError(at(key) + ": required");
    return string(key, "");
  }
  Vec3 vec3(const std::string& key, Vec3 def) const {
    const Json* v = get(key);
    if (!v) return def;
    if (v->is_number()) return Vec3{v->as_number()};  // scalar shorthand: 0.5 => [0.5,0.5,0.5]
    if (!v->is_array() || v->as_array().size() != 3) throw SceneError(at(key) + ": expected [x, y, z]");
    const auto& a = v->as_array();
    Vec3 out;
    for (std::size_t i = 0; i < 3; ++i) {
      if (!a[i].is_number()) throw SceneError(at(key) + ": expected [x, y, z] of numbers");
      out[i] = a[i].as_number();
    }
    return out;
  }
  Vec3 required_vec3(const std::string& key) const {
    if (!get(key)) throw SceneError(at(key) + ": required");
    return vec3(key, {});
  }
  const std::string& path() const { return path_; }
  const Json& json() const { return j_; }

 private:
  const Json& j_;
  std::string path_;
};

Json vec_json(const Vec3& v) { return JsonArray{v.x, v.y, v.z}; }

Material read_material(const Reader& r) {
  const std::string type = r.required_string("type");
  if (type == "lambertian" || type == "diffuse") {
    Lambertian m;
    m.albedo.a = r.vec3("albedo", Vec3{0.5});
    m.albedo.b = r.vec3("albedo2", m.albedo.a);
    m.albedo.checker_scale = r.number("checker_scale", 0);
    return m;
  }
  if (type == "metal") return Metal{r.vec3("albedo", Vec3{0.9}), r.number("roughness", 0)};
  if (type == "dielectric" || type == "glass") return Dielectric{r.number("ior", 1.5), r.vec3("tint", Vec3{1})};
  if (type == "emissive" || type == "light") return Emissive{r.vec3("color", Vec3{1}), r.number("intensity", 1), r.boolean("two_sided", false)};
  throw SceneError(r.at("type") + ": unknown material type '" + type + "'");
}

Json write_material(const Material& mat) {
  return std::visit(overloaded{
      [](const Lambertian& m) {
        Json j;
        j["type"] = "lambertian";
        j["albedo"] = vec_json(m.albedo.a);
        if (m.albedo.checker_scale > 0) {
          j["albedo2"] = vec_json(m.albedo.b);
          j["checker_scale"] = m.albedo.checker_scale;
        }
        return j;
      },
      [](const Metal& m) {
        Json j;
        j["type"] = "metal";
        j["albedo"] = vec_json(m.albedo);
        j["roughness"] = m.roughness;
        return j;
      },
      [](const Dielectric& m) {
        Json j;
        j["type"] = "dielectric";
        j["ior"] = m.ior;
        j["tint"] = vec_json(m.tint);
        return j;
      },
      [](const Emissive& m) {
        Json j;
        j["type"] = "emissive";
        j["color"] = vec_json(m.color);
        j["intensity"] = m.intensity;
        if (m.two_sided) j["two_sided"] = true;
        return j;
      },
  }, mat);
}

ObjectDesc read_object(const Reader& r) {
  ObjectDesc o;
  const std::string type = r.required_string("type");
  o.name = r.string("name", type);
  o.material = r.required_string("material");
  if (type == "sphere") {
    o.shape = SphereDesc{r.required_vec3("center"), r.number("radius", 1)};
  } else if (type == "plane") {
    o.shape = PlaneDesc{r.vec3("point", {}), r.vec3("normal", {0, 1, 0})};
  } else if (type == "triangle") {
    o.shape = TriangleDesc{r.required_vec3("v0"), r.required_vec3("v1"), r.required_vec3("v2")};
  } else if (type == "quad") {
    o.shape = QuadDesc{r.required_vec3("corner"), r.required_vec3("u"), r.required_vec3("v")};
  } else if (type == "box") {
    o.shape = BoxDesc{r.required_vec3("min"), r.required_vec3("max"), r.number("rotate_y", 0)};
  } else if (type == "mesh") {
    MeshDesc m;
    const std::string src = r.string("source", "icosphere");
    if (src == "icosphere") m.source = MeshDesc::Source::Icosphere;
    else if (src == "torus") m.source = MeshDesc::Source::Torus;
    else if (src == "obj") m.source = MeshDesc::Source::ObjFile;
    else throw SceneError(r.at("source") + ": expected icosphere, torus or obj");
    m.subdivisions = r.integer("subdivisions", m.subdivisions);
    m.major_radius = r.number("major_radius", m.major_radius);
    m.minor_radius = r.number("minor_radius", m.minor_radius);
    m.segments = r.integer("segments", m.segments);
    m.path = r.string("path", "");
    m.smooth = r.boolean("smooth", true);
    m.scale = r.vec3("scale", {1, 1, 1});
    m.rotate_y = r.number("rotate_y", 0);
    m.translate = r.vec3("translate", {0, 0, 0});
    o.shape = m;
  } else {
    throw SceneError(r.at("type") + ": unknown object type '" + type + "'");
  }
  return o;
}

Json write_object(const ObjectDesc& o) {
  Json j;
  std::visit(overloaded{
      [&](const SphereDesc& d) { j["type"] = "sphere"; j["name"] = o.name; j["center"] = vec_json(d.center); j["radius"] = d.radius; },
      [&](const PlaneDesc& d) { j["type"] = "plane"; j["name"] = o.name; j["point"] = vec_json(d.point); j["normal"] = vec_json(d.normal); },
      [&](const TriangleDesc& d) {
        j["type"] = "triangle"; j["name"] = o.name;
        j["v0"] = vec_json(d.v0); j["v1"] = vec_json(d.v1); j["v2"] = vec_json(d.v2);
      },
      [&](const QuadDesc& d) {
        j["type"] = "quad"; j["name"] = o.name;
        j["corner"] = vec_json(d.corner); j["u"] = vec_json(d.u); j["v"] = vec_json(d.v);
      },
      [&](const BoxDesc& d) {
        j["type"] = "box"; j["name"] = o.name;
        j["min"] = vec_json(d.min); j["max"] = vec_json(d.max); j["rotate_y"] = d.rotate_y;
      },
      [&](const MeshDesc& m) {
        j["type"] = "mesh"; j["name"] = o.name;
        switch (m.source) {
          case MeshDesc::Source::Icosphere: j["source"] = "icosphere"; j["subdivisions"] = m.subdivisions; break;
          case MeshDesc::Source::Torus:
            j["source"] = "torus"; j["major_radius"] = m.major_radius; j["minor_radius"] = m.minor_radius;
            j["segments"] = m.segments;
            break;
          case MeshDesc::Source::ObjFile: j["source"] = "obj"; j["path"] = m.path; break;
        }
        j["smooth"] = m.smooth;
        j["scale"] = vec_json(m.scale);
        j["rotate_y"] = m.rotate_y;
        j["translate"] = vec_json(m.translate);
      },
  }, o.shape);
  j["material"] = o.material;
  return j;
}

}  // namespace

SceneDesc scene_from_json(const Json& root) {
  SceneDesc s;
  const Reader r(root, "");
  for (const auto& [key, _] : root.as_object()) {
    static const char* known[] = {"name", "camera", "render", "background", "materials", "objects", "lights", "$schema", "description"};
    bool ok = false;
    for (const char* k : known) ok = ok || key == k;
    if (!ok) throw SceneError("scene: unknown top-level key '" + key + "'");
  }
  s.name = r.string("name", s.name);

  if (const Json* cj = r.get("camera")) {
    const Reader c(*cj, "camera");
    s.camera.look_from = c.vec3("look_from", s.camera.look_from);
    s.camera.look_at = c.vec3("look_at", s.camera.look_at);
    s.camera.vup = c.vec3("vup", s.camera.vup);
    s.camera.vfov_deg = c.number("vfov", s.camera.vfov_deg);
    s.camera.aperture = c.number("aperture", 0);
    s.camera.focus_dist = c.number("focus_dist", 0);
  }

  if (const Json* rj = r.get("render")) {
    const Reader c(*rj, "render");
    auto& R = s.render;
    R.width = c.integer("width", R.width);
    R.height = c.integer("height", R.height);
    R.spp = c.integer("spp", R.spp);
    R.max_depth = c.integer("max_depth", R.max_depth);
    R.rr_min_depth = c.integer("rr_min_depth", R.rr_min_depth);
    R.light_sampling = c.boolean("light_sampling", R.light_sampling);
    R.seed = static_cast<std::uint64_t>(c.integer("seed", static_cast<int>(R.seed)));
    R.exposure = c.number("exposure", R.exposure);
    R.threads = c.integer("threads", R.threads);
    R.tile_size = c.integer("tile_size", R.tile_size);
    R.use_bvh = c.boolean("use_bvh", R.use_bvh);
    const std::string integ = c.string("integrator", to_string(R.integrator));
    if (integ == "path") R.integrator = Integrator::Path;
    else if (integ == "direct") R.integrator = Integrator::Direct;
    else if (integ == "normals") R.integrator = Integrator::Normals;
    else throw SceneError("render.integrator: expected path, direct or normals");
    const std::string tm = c.string("tone_map", to_string(R.tone_map));
    if (tm == "clamp") R.tone_map = ToneMap::Clamp;
    else if (tm == "reinhard") R.tone_map = ToneMap::Reinhard;
    else if (tm == "aces") R.tone_map = ToneMap::Aces;
    else throw SceneError("render.tone_map: expected clamp, reinhard or aces");
  }

  if (const Json* bj = r.get("background")) {
    const Reader b(*bj, "background");
    const std::string kind = b.string("type", "gradient");
    if (kind == "gradient") s.background.kind = Background::Kind::Gradient;
    else if (kind == "solid") s.background.kind = Background::Kind::Solid;
    else throw SceneError("background.type: expected gradient or solid");
    s.background.top = b.vec3(kind == "solid" ? "color" : "top", s.background.top);
    s.background.bottom = b.vec3("bottom", s.background.bottom);
    s.background.intensity = b.number("intensity", 1);
  }

  if (const Json* mj = r.get("materials")) {
    if (!mj->is_object()) throw SceneError("materials: expected an object of name -> material");
    for (const auto& [name, mat] : mj->as_object())
      s.materials.push_back({name, read_material(Reader(mat, "materials." + name))});
  }

  if (const Json* oj = r.get("objects")) {
    if (!oj->is_array()) throw SceneError("objects: expected an array");
    const auto& arr = oj->as_array();
    for (std::size_t i = 0; i < arr.size(); ++i)
      s.objects.push_back(read_object(Reader(arr[i], "objects[" + std::to_string(i) + "]")));
  }

  if (const Json* lj = r.get("lights")) {
    if (!lj->is_array()) throw SceneError("lights: expected an array");
    const auto& arr = lj->as_array();
    for (std::size_t i = 0; i < arr.size(); ++i) {
      const Reader l(arr[i], "lights[" + std::to_string(i) + "]");
      const std::string type = l.string("type", "point");
      if (type != "point") throw SceneError(l.at("type") + ": only 'point' lights are supported (use emissive materials for area lights)");
      s.lights.push_back({l.string("name", "light" + std::to_string(i)), l.required_vec3("position"), l.vec3("intensity", Vec3{10})});
    }
  }

  const auto errors = validate_scene(s);
  if (!errors.empty()) {
    std::string msg = errors.front();
    if (errors.size() > 1) msg += " (and " + std::to_string(errors.size() - 1) + " more problem(s))";
    throw SceneError(msg);
  }
  return s;
}

Json scene_to_json(const SceneDesc& s) {
  Json j;
  j["name"] = s.name;
  Json& c = j["camera"];
  c["look_from"] = vec_json(s.camera.look_from);
  c["look_at"] = vec_json(s.camera.look_at);
  c["vup"] = vec_json(s.camera.vup);
  c["vfov"] = s.camera.vfov_deg;
  c["aperture"] = s.camera.aperture;
  c["focus_dist"] = s.camera.focus_dist;

  Json& r = j["render"];
  const auto& R = s.render;
  r["width"] = R.width;
  r["height"] = R.height;
  r["spp"] = R.spp;
  r["max_depth"] = R.max_depth;
  r["rr_min_depth"] = R.rr_min_depth;
  r["integrator"] = to_string(R.integrator);
  r["light_sampling"] = R.light_sampling;
  r["seed"] = R.seed;
  r["tone_map"] = to_string(R.tone_map);
  r["exposure"] = R.exposure;
  r["threads"] = R.threads;
  r["tile_size"] = R.tile_size;
  r["use_bvh"] = R.use_bvh;

  Json& b = j["background"];
  if (s.background.kind == Background::Kind::Solid) {
    b["type"] = "solid";
    b["color"] = vec_json(s.background.top);
  } else {
    b["type"] = "gradient";
    b["top"] = vec_json(s.background.top);
    b["bottom"] = vec_json(s.background.bottom);
  }
  b["intensity"] = s.background.intensity;

  Json mats = JsonObject{};
  for (const auto& m : s.materials) mats[m.name] = write_material(m.material);
  j["materials"] = std::move(mats);

  JsonArray objs;
  for (const auto& o : s.objects) objs.push_back(write_object(o));
  j["objects"] = std::move(objs);

  JsonArray lights;
  for (const auto& l : s.lights) {
    Json lj;
    lj["type"] = "point";
    lj["name"] = l.name;
    lj["position"] = vec_json(l.position);
    lj["intensity"] = vec_json(l.intensity);
    lights.push_back(std::move(lj));
  }
  j["lights"] = std::move(lights);
  return j;
}

std::vector<std::string> validate_scene(const SceneDesc& s) {
  std::vector<std::string> e;
  auto finite = [&](const Vec3& v, const std::string& where) {
    if (!v.is_finite()) e.push_back(where + ": must be finite");
  };
  auto color = [&](const Vec3& v, const std::string& where) {
    finite(v, where);
    if (v.x < 0 || v.y < 0 || v.z < 0) e.push_back(where + ": colour components must be >= 0");
  };
  const auto& R = s.render;
  if (R.width < 1 || R.width > 8192) e.push_back("render.width: must be in [1, 8192]");
  if (R.height < 1 || R.height > 8192) e.push_back("render.height: must be in [1, 8192]");
  if (R.spp < 1 || R.spp > 1'000'000) e.push_back("render.spp: must be in [1, 1000000]");
  if (R.max_depth < 1 || R.max_depth > 1000) e.push_back("render.max_depth: must be in [1, 1000]");
  if (R.rr_min_depth < 0) e.push_back("render.rr_min_depth: must be >= 0");
  if (R.threads < 0 || R.threads > 1024) e.push_back("render.threads: must be in [0, 1024]");
  if (R.tile_size < 1 || R.tile_size > 1024) e.push_back("render.tile_size: must be in [1, 1024]");
  if (!(R.exposure > 0) || !std::isfinite(R.exposure)) e.push_back("render.exposure: must be > 0");

  finite(s.camera.look_from, "camera.look_from");
  finite(s.camera.look_at, "camera.look_at");
  if ((s.camera.look_at - s.camera.look_from).length() < 1e-9) e.push_back("camera: look_from and look_at must differ");
  if (!(s.camera.vfov_deg > 0 && s.camera.vfov_deg < 180)) e.push_back("camera.vfov: must be in (0, 180)");
  if (s.camera.aperture < 0) e.push_back("camera.aperture: must be >= 0");
  if (s.camera.focus_dist < 0) e.push_back("camera.focus_dist: must be >= 0");
  color(s.background.top, "background.top");
  color(s.background.bottom, "background.bottom");

  for (std::size_t i = 0; i < s.materials.size(); ++i) {
    const auto& nm = s.materials[i];
    const std::string at = "materials." + nm.name;
    if (nm.name.empty()) e.push_back("materials: material names must be non-empty");
    for (std::size_t k = 0; k < i; ++k)
      if (s.materials[k].name == nm.name) e.push_back(at + ": duplicate material name");
    std::visit(overloaded{
        [&](const Lambertian& m) {
          color(m.albedo.a, at + ".albedo");
          color(m.albedo.b, at + ".albedo2");
          if (m.albedo.checker_scale < 0) e.push_back(at + ".checker_scale: must be >= 0");
        },
        [&](const Metal& m) {
          color(m.albedo, at + ".albedo");
          if (m.roughness < 0 || m.roughness > 1) e.push_back(at + ".roughness: must be in [0, 1]");
        },
        [&](const Dielectric& m) {
          if (!(m.ior >= 1.0 && m.ior <= 4.0)) e.push_back(at + ".ior: must be in [1, 4]");
          color(m.tint, at + ".tint");
        },
        [&](const Emissive& m) {
          color(m.color, at + ".color");
          if (!(m.intensity >= 0) || !std::isfinite(m.intensity)) e.push_back(at + ".intensity: must be >= 0");
        },
    }, nm.material);
  }

  for (std::size_t i = 0; i < s.objects.size(); ++i) {
    const auto& o = s.objects[i];
    const std::string at = "objects[" + std::to_string(i) + "]";
    if (!s.find_material(o.material)) e.push_back(at + ".material: unknown material '" + o.material + "'");
    std::visit(overloaded{
        [&](const SphereDesc& d) {
          finite(d.center, at + ".center");
          if (!(d.radius > 0) || !std::isfinite(d.radius)) e.push_back(at + ".radius: must be > 0");
        },
        [&](const PlaneDesc& d) {
          finite(d.point, at + ".point");
          finite(d.normal, at + ".normal");
          if (d.normal.length() < 1e-9) e.push_back(at + ".normal: must be non-zero");
        },
        [&](const TriangleDesc& d) {
          finite(d.v0, at + ".v0"); finite(d.v1, at + ".v1"); finite(d.v2, at + ".v2");
          if (cross(d.v1 - d.v0, d.v2 - d.v0).length() < 1e-12) e.push_back(at + ": triangle is degenerate");
        },
        [&](const QuadDesc& d) {
          finite(d.corner, at + ".corner"); finite(d.u, at + ".u"); finite(d.v, at + ".v");
          if (cross(d.u, d.v).length() < 1e-12) e.push_back(at + ": quad is degenerate (u parallel to v)");
        },
        [&](const BoxDesc& d) {
          finite(d.min, at + ".min"); finite(d.max, at + ".max");
          if (!(d.max.x > d.min.x && d.max.y > d.min.y && d.max.z > d.min.z)) e.push_back(at + ": box max must exceed min on every axis");
        },
        [&](const MeshDesc& m) {
          if (m.subdivisions < 0 || m.subdivisions > 7) e.push_back(at + ".subdivisions: must be in [0, 7]");
          if (m.segments < 3 || m.segments > 2048) e.push_back(at + ".segments: must be in [3, 2048]");
          if (!(m.major_radius > 0) || !(m.minor_radius > 0)) e.push_back(at + ": torus radii must be > 0");
          if (m.source == MeshDesc::Source::ObjFile && m.path.empty()) e.push_back(at + ".path: required for obj meshes");
          finite(m.scale, at + ".scale");
          finite(m.translate, at + ".translate");
          if (std::fabs(m.scale.x * m.scale.y * m.scale.z) < 1e-12) e.push_back(at + ".scale: components must be non-zero");
        },
    }, o.shape);
  }

  for (std::size_t i = 0; i < s.lights.size(); ++i) {
    finite(s.lights[i].position, "lights[" + std::to_string(i) + "].position");
    color(s.lights[i].intensity, "lights[" + std::to_string(i) + "].intensity");
  }
  return e;
}

SceneDesc parse_scene(const std::string& text) {
  Json j;
  try {
    j = Json::parse(text);
  } catch (const JsonError& err) {
    throw SceneError(err.what());
  }
  try {
    return scene_from_json(j);
  } catch (const JsonError& err) {
    throw SceneError(err.what());
  }
}

SceneDesc load_scene(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw SceneError("cannot open scene file: " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  SceneDesc s = parse_scene(ss.str());
  s.base_dir = std::filesystem::path(path).parent_path().string();
  return s;
}

}  // namespace rt
