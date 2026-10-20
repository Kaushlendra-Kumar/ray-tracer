#include "raytracer/io/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace rt {

namespace {
const char* type_name(const Json::Value& v) {
  switch (v.index()) {
    case 0: return "null";
    case 1: return "bool";
    case 2: return "number";
    case 3: return "string";
    case 4: return "array";
    default: return "object";
  }
}
}  // namespace

#define RT_JSON_GET(T, name)                                                              \
  if (auto* p = std::get_if<T>(&v_)) return *p;                                           \
  throw JsonError(std::string("expected ") + name + ", got " + type_name(v_));

bool Json::as_bool() const { RT_JSON_GET(bool, "bool") }
double Json::as_number() const { RT_JSON_GET(double, "number") }
const std::string& Json::as_string() const { RT_JSON_GET(std::string, "string") }
const JsonArray& Json::as_array() const { RT_JSON_GET(JsonArray, "array") }
const JsonObject& Json::as_object() const { RT_JSON_GET(JsonObject, "object") }
JsonArray& Json::as_array() { RT_JSON_GET(JsonArray, "array") }
JsonObject& Json::as_object() { RT_JSON_GET(JsonObject, "object") }
#undef RT_JSON_GET

const Json* Json::find(const std::string& key) const {
  const auto* obj = std::get_if<JsonObject>(&v_);
  if (!obj) return nullptr;
  for (const auto& [k, v] : *obj)
    if (k == key) return &v;
  return nullptr;
}

Json& Json::operator[](const std::string& key) {
  if (is_null()) v_ = JsonObject{};
  auto& obj = as_object();
  for (auto& [k, v] : obj)
    if (k == key) return v;
  obj.emplace_back(key, Json{});
  return obj.back().second;
}

// ---------------------------------------------------------------------------
// Serialisation
namespace {

void escape(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

void write_number(std::string& out, double d) {
  if (!std::isfinite(d)) { out += "null"; return; }  // JSON has no inf/nan
  char buf[32];
  // Shortest representation that round-trips exactly.
  auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, d);
  out.append(buf, ptr);
}

void dump_impl(std::string& out, const Json& j, int indent, int level) {
  const bool pretty = indent >= 0;
  auto newline = [&](int lvl) {
    if (!pretty) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent * lvl), ' ');
  };
  const auto& v = j.value();
  if (j.is_null()) out += "null";
  else if (j.is_bool()) out += j.as_bool() ? "true" : "false";
  else if (j.is_number()) write_number(out, j.as_number());
  else if (j.is_string()) escape(out, j.as_string());
  else if (j.is_array()) {
    const auto& a = std::get<JsonArray>(v);
    // Short arrays of numbers (vectors, colours) stay on one line.
    bool flat = a.size() <= 4;
    for (const auto& e : a) flat = flat && (e.is_number() || e.is_bool() || e.is_null());
    out += '[';
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (i) out += flat || !pretty ? (pretty ? ", " : ",") : ",";
      if (!flat) newline(level + 1);
      dump_impl(out, a[i], indent, level + 1);
    }
    if (!flat && !a.empty()) newline(level);
    out += ']';
  } else {
    const auto& o = std::get<JsonObject>(v);
    out += '{';
    for (std::size_t i = 0; i < o.size(); ++i) {
      if (i) out += ',';
      newline(level + 1);
      escape(out, o[i].first);
      out += pretty ? ": " : ":";
      dump_impl(out, o[i].second, indent, level + 1);
    }
    if (!o.empty()) newline(level);
    out += '}';
  }
}

// ---------------------------------------------------------------------------
// Parsing: recursive descent with an explicit nesting limit so hostile input
// cannot overflow the stack.
class Parser {
 public:
  Parser(const std::string& s, int max_depth) : s_(s), max_depth_(max_depth) {}

  Json parse_document() {
    Json v = parse_value(0);
    skip_ws();
    if (pos_ != s_.size()) fail("trailing characters after JSON value");
    return v;
  }

 private:
  [[noreturn]] void fail(const std::string& msg) const {
    int line = 1, col = 1;
    for (std::size_t i = 0; i < pos_ && i < s_.size(); ++i) {
      if (s_[i] == '\n') { ++line; col = 1; } else { ++col; }
    }
    throw JsonError("JSON parse error at line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + msg);
  }
  void skip_ws() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
  }
  char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
  void expect(char c) {
    if (peek() != c) fail(std::string("expected '") + c + "'");
    ++pos_;
  }
  bool consume_literal(const char* lit) {
    const std::string_view l(lit);
    if (s_.compare(pos_, l.size(), l) == 0) { pos_ += l.size(); return true; }
    return false;
  }

  Json parse_value(int depth) {
    if (depth > max_depth_) fail("nesting too deep");
    skip_ws();
    const char c = peek();
    if (c == '{') return parse_object(depth);
    if (c == '[') return parse_array(depth);
    if (c == '"') return Json(parse_string());
    if (c == '-' || (c >= '0' && c <= '9')) return Json(parse_number());
    if (consume_literal("true")) return Json(true);
    if (consume_literal("false")) return Json(false);
    if (consume_literal("null")) return Json(nullptr);
    if (c == '\0') fail("unexpected end of input");
    fail(std::string("unexpected character '") + c + "'");
  }

  Json parse_object(int depth) {
    expect('{');
    JsonObject obj;
    skip_ws();
    if (peek() == '}') { ++pos_; return Json(std::move(obj)); }
    for (;;) {
      skip_ws();
      if (peek() != '"') fail("expected string key");
      std::string key = parse_string();
      for (const auto& kv : obj)
        if (kv.first == key) fail("duplicate key '" + key + "'");
      skip_ws();
      expect(':');
      obj.emplace_back(std::move(key), parse_value(depth + 1));
      skip_ws();
      if (peek() == ',') { ++pos_; continue; }
      expect('}');
      return Json(std::move(obj));
    }
  }

  Json parse_array(int depth) {
    expect('[');
    JsonArray arr;
    skip_ws();
    if (peek() == ']') { ++pos_; return Json(std::move(arr)); }
    for (;;) {
      arr.push_back(parse_value(depth + 1));
      skip_ws();
      if (peek() == ',') { ++pos_; continue; }
      expect(']');
      return Json(std::move(arr));
    }
  }

  static void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  std::uint32_t parse_hex4() {
    if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      const char h = s_[pos_++];
      v <<= 4;
      if (h >= '0' && h <= '9') v |= static_cast<std::uint32_t>(h - '0');
      else if (h >= 'a' && h <= 'f') v |= static_cast<std::uint32_t>(h - 'a' + 10);
      else if (h >= 'A' && h <= 'F') v |= static_cast<std::uint32_t>(h - 'A' + 10);
      else fail("bad hex digit in \\u escape");
    }
    return v;
  }

  std::string parse_string() {
    expect('"');
    std::string out;
    for (;;) {
      if (pos_ >= s_.size()) fail("unterminated string");
      const char c = s_[pos_++];
      if (c == '"') return out;
      if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
      if (c != '\\') { out += c; continue; }
      if (pos_ >= s_.size()) fail("unterminated escape");
      const char e = s_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          std::uint32_t cp = parse_hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
            if (!consume_literal("\\u")) fail("unpaired surrogate");
            const std::uint32_t lo = parse_hex4();
            if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid low surrogate");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            fail("unpaired surrogate");
          }
          append_utf8(out, cp);
          break;
        }
        default: fail("invalid escape");
      }
    }
  }

  double parse_number() {
    const std::size_t start = pos_;
    if (peek() == '-') ++pos_;
    if (peek() == '0') ++pos_;
    else if (peek() >= '1' && peek() <= '9') while (peek() >= '0' && peek() <= '9') ++pos_;
    else fail("invalid number");
    if (peek() == '.') {
      ++pos_;
      if (!(peek() >= '0' && peek() <= '9')) fail("digit expected after '.'");
      while (peek() >= '0' && peek() <= '9') ++pos_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++pos_;
      if (peek() == '+' || peek() == '-') ++pos_;
      if (!(peek() >= '0' && peek() <= '9')) fail("digit expected in exponent");
      while (peek() >= '0' && peek() <= '9') ++pos_;
    }
    double d = 0;
    const auto [ptr, ec] = std::from_chars(s_.data() + start, s_.data() + pos_, d);
    if (ec != std::errc() || ptr != s_.data() + pos_ || !std::isfinite(d)) fail("number out of range");
    return d;
  }

  const std::string& s_;
  std::size_t pos_ = 0;
  int max_depth_;
};

}  // namespace

std::string Json::dump(int indent) const {
  std::string out;
  dump_impl(out, *this, indent, 0);
  return out;
}

Json Json::parse(const std::string& text, int max_depth) { return Parser(text, max_depth).parse_document(); }

}  // namespace rt
