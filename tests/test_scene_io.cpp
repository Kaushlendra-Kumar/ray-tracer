#include <catch2/catch_amalgamated.hpp>

#include <filesystem>

#include "raytracer/core/scene.hpp"
#include "raytracer/io/scene_io.hpp"

using namespace rt;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("JSON parser accepts valid documents", "[json]") {
  const Json j = Json::parse(R"({"a": [1, 2.5, -3e2, true, false, null], "s": "x\"\\\n\u00e9\ud83d\ude00", "o": {}})");
  CHECK(j.find("a")->as_array().size() == 6);
  CHECK(j.find("a")->as_array()[2].as_number() == -300);
  CHECK(j.find("s")->as_string() == "x\"\\\n\xC3\xA9\xF0\x9F\x98\x80");
  CHECK(j.find("o")->as_object().empty());
  CHECK(j.find("missing") == nullptr);
  // Round trip through the serialiser, compact and pretty.
  CHECK(Json::parse(j.dump(-1)) == j);
  CHECK(Json::parse(j.dump(2)) == j);
  CHECK(Json::parse("0.1").as_number() == 0.1);
  CHECK(Json(0.1).dump() == "0.1");  // shortest round-trip formatting
}

TEST_CASE("JSON parser rejects malformed input with positions", "[json]") {
  CHECK_THROWS_WITH(Json::parse("{\"a\": 1,}"), ContainsSubstring("line 1"));
  CHECK_THROWS_WITH(Json::parse("{\n  \"a\" 1}"), ContainsSubstring("line 2"));
  for (const char* bad : {"", "[", "{\"a\":}", "01", "1.", "-", "\"abc", "[1 2]", "tru", "{\"a\":1}x",
                          "\"\\x\"", "\"\\ud800\"", "{\"a\":1,\"a\":2}", "1e999", "\"\x01\""}) {
    INFO(bad);
    CHECK_THROWS_AS(Json::parse(bad), JsonError);
  }
  std::string deep(1000, '[');
  CHECK_THROWS_WITH(Json::parse(deep), ContainsSubstring("nesting"));
}

TEST_CASE("Every shipped scene parses, validates, compiles and round-trips", "[scene]") {
  int count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(std::string(RT_ASSET_DIR) + "/scenes")) {
    if (entry.path().extension() != ".json") continue;
    INFO(entry.path().string());
    const SceneDesc a = load_scene(entry.path().string());
    CHECK(validate_scene(a).empty());
    const SceneDesc b = parse_scene(scene_to_json(a).dump());
    CHECK(a == b);  // serialisation is lossless
    SceneDesc c = b;
    c.base_dir = a.base_dir;
    const Scene s = Scene::compile(c);
    CHECK(s.primitives.size() + s.planes.size() > 0);
    ++count;
  }
  CHECK(count >= 8);
}

TEST_CASE("Scene validation reports precise locations", "[scene]") {
  auto err = [](const std::string& json) {
    try {
      parse_scene(json);
    } catch (const SceneError& e) {
      return std::string(e.what());
    }
    return std::string("no error");
  };
  const std::string mat = R"("materials": {"m": {"type": "lambertian"}})";
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "sphere", "center": [0,0,0], "radius": -1, "material": "m"}]})"),
             ContainsSubstring("objects[0].radius"));
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "sphere", "center": [0,0,0], "material": "nope"}]})"),
             ContainsSubstring("unknown material 'nope'"));
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "cube", "material": "m"}]})"), ContainsSubstring("unknown object type"));
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "sphere", "center": [0,0], "material": "m"}]})"),
             ContainsSubstring("objects[0].center"));
  CHECK_THAT(err(R"({"render": {"width": 0}})"), ContainsSubstring("render.width"));
  CHECK_THAT(err(R"({"render": {"spp": 1.5}})"), ContainsSubstring("render.spp"));
  CHECK_THAT(err(R"({"render": {"integrator": "magic"}})"), ContainsSubstring("render.integrator"));
  CHECK_THAT(err(R"({"camera": {"look_from": [0,0,0], "look_at": [0,0,0]}})"), ContainsSubstring("must differ"));
  CHECK_THAT(err(R"({"camera": {"vfov": 180}})"), ContainsSubstring("camera.vfov"));
  CHECK_THAT(err(R"({"materials": {"g": {"type": "dielectric", "ior": 0.5}}})"), ContainsSubstring("materials.g.ior"));
  CHECK_THAT(err(R"({"materials": {"g": {"type": "metal", "roughness": 2}}})"), ContainsSubstring("roughness"));
  CHECK_THAT(err(R"({"materials": {"g": {"type": "lambertian", "albedo": [-1, 0, 0]}}})"), ContainsSubstring(">= 0"));
  CHECK_THAT(err(R"({"bogus": 1})"), ContainsSubstring("unknown top-level key"));
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "quad", "corner": [0,0,0], "u": [1,0,0], "v": [2,0,0], "material": "m"}]})"),
             ContainsSubstring("degenerate"));
  CHECK_THAT(err(R"({)" + mat + R"(, "objects": [{"type": "mesh", "source": "icosphere", "subdivisions": 12, "material": "m"}]})"),
             ContainsSubstring("subdivisions"));
  CHECK_THAT(err("not json"), ContainsSubstring("JSON parse error"));
  CHECK(err(R"({)" + mat + R"(, "objects": [{"type": "sphere", "center": [0,0,0], "material": "m"}]})") == "no error");
}

TEST_CASE("Defaults produce a valid empty scene", "[scene]") {
  const SceneDesc s = parse_scene("{}");
  CHECK(s.render.width == 640);
  CHECK(validate_scene(s).empty());
  const Scene compiled = Scene::compile(s);
  CHECK(compiled.primitives.empty());
}
