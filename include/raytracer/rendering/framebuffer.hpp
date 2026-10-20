#pragma once
#include <cstdint>
#include <vector>

#include "raytracer/core/scene_desc.hpp"
#include "raytracer/io/image_io.hpp"

namespace rt {

// Linear-radiance accumulation buffer. Each pixel stores the running *sum* of
// its samples plus its own sample count, so a render cancelled half-way
// through a pass still resolves to a correct (just noisier) average.
//
// Ownership/threading: during a pass each pixel is written by exactly one
// worker (tiles are disjoint), so no locks or atomics are needed.
class Framebuffer {
 public:
  Framebuffer() = default;
  Framebuffer(int w, int h) { resize(w, h); }

  void resize(int w, int h) {
    width_ = w;
    height_ = h;
    const auto n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    sum_.assign(n, Color{});
    count_.assign(n, 0);
  }
  int width() const { return width_; }
  int height() const { return height_; }

  void add(int x, int y, const Color& c) {
    const auto i = index(x, y);
    sum_[i] += c;
    count_[i]++;
  }
  Color mean(int x, int y) const {
    const auto i = index(x, y);
    return count_[i] ? sum_[i] / Real(count_[i]) : Color{};
  }
  std::uint32_t samples(int x, int y) const { return count_[index(x, y)]; }
  std::uint32_t min_samples() const;

 private:
  std::size_t index(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
  }
  int width_ = 0, height_ = 0;
  std::vector<Color> sum_;
  std::vector<std::uint32_t> count_;
};

// Linear HDR -> display: exposure, tone mapping operator, then the sRGB
// transfer function, then 8-bit quantisation.
Color tone_map(const Color& c, ToneMap op, Real exposure);
std::uint8_t linear_to_srgb8(Real v);
Image8 resolve(const Framebuffer& fb, ToneMap op, Real exposure);

}  // namespace rt
