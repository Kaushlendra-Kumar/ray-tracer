#pragma once
#include <cstdint>

#include "raytracer/math/vec3.hpp"

namespace rt {

// SplitMix64 finaliser: a cheap, well-mixed 64-bit hash used to derive
// independent RNG streams from (seed, pixel, sample) tuples.
constexpr std::uint64_t splitmix64(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// PCG32 (O'Neill, 2014): 64-bit state, 32-bit output, tiny and statistically
// solid. One instance per pixel sample => results are independent of how work
// is distributed across threads (bit-identical output for any worker count).
class Rng {
 public:
  explicit constexpr Rng(std::uint64_t seed = 0x853c49e6748fea9bull, std::uint64_t stream = 0xda3e39cb94b95bdbull)
      : inc_((stream << 1u) | 1u) {
    next_u32();
    state_ += seed;
    next_u32();
  }

  static constexpr Rng for_sample(std::uint64_t seed, std::uint64_t pixel, std::uint64_t sample) {
    const std::uint64_t h = splitmix64(seed ^ splitmix64(pixel ^ splitmix64(sample)));
    return Rng(h, splitmix64(h));
  }

  constexpr std::uint32_t next_u32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ull + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
  }

  // Uniform in [0, 1). Uses 32 random bits; the result is strictly < 1.
  constexpr Real uniform() { return next_u32() * (1.0 / 4294967296.0); }
  constexpr Real uniform(Real lo, Real hi) { return lo + (hi - lo) * uniform(); }
  // Uniform integer in [0, n).
  std::uint32_t below(std::uint32_t n) { return static_cast<std::uint32_t>(uniform() * n); }

 private:
  std::uint64_t state_ = 0;
  std::uint64_t inc_;
};

}  // namespace rt
