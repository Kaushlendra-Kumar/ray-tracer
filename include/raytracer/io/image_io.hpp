#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rt {

// 8-bit sRGB image, row-major, top row first, RGB interleaved.
struct Image8 {
  int width = 0, height = 0;
  std::vector<std::uint8_t> rgb;
};

void write_ppm(const std::string& path, const Image8& img);           // binary P6
std::vector<std::uint8_t> encode_png(const Image8& img);              // in-memory PNG
void write_png(const std::string& path, const Image8& img);
// Writes PNG or PPM depending on the extension (.png / .ppm).
void write_image(const std::string& path, const Image8& img);

// Minimal P6 reader (tests and image diffing).
Image8 read_ppm(const std::string& path);

}  // namespace rt
