#include "raytracer/io/image_io.hpp"

#include <fstream>
#include <stdexcept>

// stb_image_write is a public-domain single-header PNG encoder. PNG needs
// DEFLATE + CRC32; re-implementing zlib is out of scope for a renderer, and
// stb is vendored (no system dependency). Only this translation unit sees it.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace rt {

namespace {
void check(const Image8& img) {
  if (img.width <= 0 || img.height <= 0 ||
      img.rgb.size() != static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3)
    throw std::runtime_error("invalid image dimensions");
}
bool ends_with(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
}  // namespace

void write_ppm(const std::string& path, const Image8& img) {
  check(img);
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + path);
  f << "P6\n" << img.width << ' ' << img.height << "\n255\n";
  f.write(reinterpret_cast<const char*>(img.rgb.data()), static_cast<std::streamsize>(img.rgb.size()));
  if (!f) throw std::runtime_error("write failed: " + path);
}

std::vector<std::uint8_t> encode_png(const Image8& img) {
  check(img);
  std::vector<std::uint8_t> out;
  auto sink = [](void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<std::uint8_t>*>(ctx);
    const auto* p = static_cast<const std::uint8_t*>(data);
    v->insert(v->end(), p, p + size);
  };
  if (!stbi_write_png_to_func(sink, &out, img.width, img.height, 3, img.rgb.data(), img.width * 3))
    throw std::runtime_error("PNG encoding failed");
  return out;
}

void write_png(const std::string& path, const Image8& img) {
  const auto bytes = encode_png(img);
  std::ofstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot write " + path);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!f) throw std::runtime_error("write failed: " + path);
}

void write_image(const std::string& path, const Image8& img) {
  if (ends_with(path, ".ppm")) write_ppm(path, img);
  else if (ends_with(path, ".png")) write_png(path, img);
  else throw std::runtime_error("unsupported output extension (use .png or .ppm): " + path);
}

Image8 read_ppm(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open " + path);
  std::string magic;
  int maxval = 0;
  Image8 img;
  f >> magic >> img.width >> img.height >> maxval;
  if (magic != "P6" || maxval != 255 || img.width <= 0 || img.height <= 0) throw std::runtime_error("unsupported PPM");
  f.get();  // single whitespace after header
  img.rgb.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3);
  f.read(reinterpret_cast<char*>(img.rgb.data()), static_cast<std::streamsize>(img.rgb.size()));
  if (!f) throw std::runtime_error("truncated PPM");
  return img;
}

}  // namespace rt
