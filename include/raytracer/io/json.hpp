#pragma once
// A small, strict JSON implementation (RFC 8259) for scene files. Written in
// house to avoid a dependency for ~300 lines of code; it supports exactly what
// scenes need and reports errors with line/column positions.
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rt {

class Json;
using JsonArray = std::vector<Json>;
// Objects keep insertion order so serialised scenes stay readable and diffable.
using JsonObject = std::vector<std::pair<std::string, Json>>;

class JsonError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class Json {
 public:
  using Value = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

  Json() : v_(nullptr) {}
  Json(std::nullptr_t) : v_(nullptr) {}
  Json(bool b) : v_(b) {}
  Json(double d) : v_(d) {}
  Json(int i) : v_(static_cast<double>(i)) {}
  Json(std::int64_t i) : v_(static_cast<double>(i)) {}
  Json(std::uint64_t i) : v_(static_cast<double>(i)) {}
  Json(const char* s) : v_(std::string(s)) {}
  Json(std::string s) : v_(std::move(s)) {}
  Json(JsonArray a) : v_(std::move(a)) {}
  Json(JsonObject o) : v_(std::move(o)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(v_); }
  bool is_bool() const { return std::holds_alternative<bool>(v_); }
  bool is_number() const { return std::holds_alternative<double>(v_); }
  bool is_string() const { return std::holds_alternative<std::string>(v_); }
  bool is_array() const { return std::holds_alternative<JsonArray>(v_); }
  bool is_object() const { return std::holds_alternative<JsonObject>(v_); }

  bool as_bool() const;
  double as_number() const;
  const std::string& as_string() const;
  const JsonArray& as_array() const;
  const JsonObject& as_object() const;
  JsonArray& as_array();
  JsonObject& as_object();

  // Object helpers. `find` returns nullptr when the key is absent.
  const Json* find(const std::string& key) const;
  Json& operator[](const std::string& key);  // inserts null if absent (object only)

  std::string dump(int indent = 2) const;  // indent < 0 => compact
  static Json parse(const std::string& text, int max_depth = 128);

  const Value& value() const { return v_; }
  bool operator==(const Json& o) const { return v_ == o.v_; }

 private:
  Value v_;
};

}  // namespace rt
