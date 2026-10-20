#include <algorithm>
#include <cmath>

#include "raytracer/rendering/framebuffer.hpp"

namespace rt {

std::uint32_t Framebuffer::min_samples() const {
  if (count_.empty()) return 0;
  return *std::min_element(count_.begin(), count_.end());
}

namespace {
// ACES filmic curve fit (Narkowicz 2015), applied per channel.
Real aces(Real x) {
  const Real a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
  return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), Real(0), Real(1));
}
}  // namespace

Color tone_map(const Color& in, ToneMap op, Real exposure) {
  Color c = in * exposure;
  if (!c.is_finite()) c = Color{};  // never let a NaN reach the image
  switch (op) {
    case ToneMap::Clamp: return {std::clamp(c.x, Real(0), Real(1)), std::clamp(c.y, Real(0), Real(1)), std::clamp(c.z, Real(0), Real(1))};
    case ToneMap::Reinhard: return {c.x / (1 + c.x), c.y / (1 + c.y), c.z / (1 + c.z)};
    case ToneMap::Aces: return {aces(c.x), aces(c.y), aces(c.z)};
  }
  return c;
}

std::uint8_t linear_to_srgb8(Real v) {
  v = std::clamp(v, Real(0), Real(1));
  // Exact sRGB OETF (IEC 61966-2-1) rather than a plain gamma 2.2.
  const Real s = v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
  return static_cast<std::uint8_t>(std::lround(std::clamp(s, Real(0), Real(1)) * 255.0));
}

Image8 resolve(const Framebuffer& fb, ToneMap op, Real exposure) {
  Image8 img;
  img.width = fb.width();
  img.height = fb.height();
  img.rgb.resize(static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height) * 3);
  std::size_t k = 0;
  for (int y = 0; y < img.height; ++y)
    for (int x = 0; x < img.width; ++x) {
      const Color c = tone_map(fb.mean(x, y), op, exposure);
      img.rgb[k++] = linear_to_srgb8(c.x);
      img.rgb[k++] = linear_to_srgb8(c.y);
      img.rgb[k++] = linear_to_srgb8(c.z);
    }
  return img;
}

}  // namespace rt
