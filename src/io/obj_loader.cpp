#include <fstream>
#include <sstream>
#include <stdexcept>

#include "raytracer/geometry/mesh.hpp"

namespace rt {

namespace {

// Resolve a 1-based (or negative, relative) OBJ index into a 0-based index.
std::uint32_t resolve(long long idx, std::size_t count, int line) {
  long long r = idx > 0 ? idx - 1 : static_cast<long long>(count) + idx;
  if (idx == 0 || r < 0 || r >= static_cast<long long>(count))
    throw std::runtime_error("OBJ line " + std::to_string(line) + ": index out of range");
  return static_cast<std::uint32_t>(r);
}

}  // namespace

TriangleMesh parse_obj(const std::string& text) {
  TriangleMesh mesh;
  std::vector<Vec3> file_normals;
  // Per-vertex normals are only kept if every face corner references one and
  // each position consistently maps to the same normal; otherwise we compute
  // smooth normals ourselves. This keeps the mesh indexed by position.
  std::vector<long long> normal_of_position;
  bool normals_usable = true;

  std::istringstream in(text);
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    std::istringstream ls(line);
    std::string tag;
    if (!(ls >> tag) || tag[0] == '#') continue;
    if (tag == "v") {
      Vec3 p;
      if (!(ls >> p.x >> p.y >> p.z)) throw std::runtime_error("OBJ line " + std::to_string(line_no) + ": bad vertex");
      mesh.positions.push_back(p);
      normal_of_position.push_back(-1);
    } else if (tag == "vn") {
      Vec3 n;
      if (!(ls >> n.x >> n.y >> n.z)) throw std::runtime_error("OBJ line " + std::to_string(line_no) + ": bad normal");
      file_normals.push_back(normalize_or(n, Vec3{0, 1, 0}));
    } else if (tag == "f") {
      std::vector<std::uint32_t> poly;
      std::string corner;
      while (ls >> corner) {
        long long vi = 0, ni = 0;
        bool has_n = false;
        const auto s1 = corner.find('/');
        try {
          vi = std::stoll(corner.substr(0, s1));
          if (s1 != std::string::npos) {
            const auto s2 = corner.find('/', s1 + 1);
            if (s2 != std::string::npos && s2 + 1 < corner.size()) {
              ni = std::stoll(corner.substr(s2 + 1));
              has_n = true;
            }
          }
        } catch (const std::exception&) {
          throw std::runtime_error("OBJ line " + std::to_string(line_no) + ": bad face corner '" + corner + "'");
        }
        const auto v = resolve(vi, mesh.positions.size(), line_no);
        if (has_n) {
          const auto n = static_cast<long long>(resolve(ni, file_normals.size(), line_no));
          if (normal_of_position[v] == -1) normal_of_position[v] = n;
          else if (normal_of_position[v] != n) normals_usable = false;
        } else {
          normals_usable = false;
        }
        poly.push_back(v);
      }
      if (poly.size() < 3) throw std::runtime_error("OBJ line " + std::to_string(line_no) + ": face has < 3 vertices");
      for (std::size_t k = 1; k + 1 < poly.size(); ++k) mesh.faces.push_back({poly[0], poly[k], poly[k + 1]});
    }
  }
  if (normals_usable && !file_normals.empty() && !mesh.faces.empty()) {
    mesh.normals.resize(mesh.positions.size(), Vec3{0, 1, 0});
    for (std::size_t i = 0; i < mesh.positions.size(); ++i)
      if (normal_of_position[i] >= 0) mesh.normals[i] = file_normals[static_cast<std::size_t>(normal_of_position[i])];
  }
  return mesh;
}

TriangleMesh load_obj(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open OBJ file: " + path);
  std::stringstream ss;
  ss << f.rdbuf();
  return parse_obj(ss.str());
}

}  // namespace rt
