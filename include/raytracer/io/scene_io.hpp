#pragma once
#include <stdexcept>
#include <string>
#include <vector>

#include "raytracer/core/scene_desc.hpp"
#include "raytracer/io/json.hpp"

namespace rt {

// Raised for any problem in a scene description. The message names the
// offending field with a JSON-path-like location, e.g. "objects[2].radius".
class SceneError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Parse + validate. Throws SceneError (structural or semantic problems).
SceneDesc scene_from_json(const Json& j);
SceneDesc load_scene(const std::string& path);           // sets base_dir
SceneDesc parse_scene(const std::string& json_text);

Json scene_to_json(const SceneDesc& s);

// Semantic validation (ranges, references, finiteness). Returns all problems
// found rather than stopping at the first, which is friendlier for the editor.
std::vector<std::string> validate_scene(const SceneDesc& s);

const char* to_string(Integrator i);
const char* to_string(ToneMap t);

}  // namespace rt
