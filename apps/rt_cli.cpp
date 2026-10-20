// rt — command-line front end for the rendering core.
//
//   rt scene.json -o out.png [overrides...]
//   rt - --stream < scene.json        (scene on stdin, progressive frames on stdout)
//
// Streaming protocol (used by the Go editor backend), all on stdout:
//   FRAME <json header>\n<png bytes>     one per progressive pass; header has "bytes"
//   DONE <json stats>\n                  render finished or was cancelled
//   ERROR <json {"message": ...}>\n      scene could not be loaded/validated
// Diagnostics go to stderr. SIGINT/SIGTERM cancel the render cleanly.
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "raytracer/core/scene.hpp"
#include "raytracer/io/image_io.hpp"
#include "raytracer/io/scene_io.hpp"
#include "raytracer/rendering/renderer.hpp"

using namespace rt;

namespace {

std::atomic<bool> g_cancel{false};
extern "C" void on_signal(int) { g_cancel.store(true); }

void usage() {
  std::cerr <<
      "usage: rt <scene.json | -> [options]\n"
      "  -o, --output FILE      output image (.png or .ppm), default render.png\n"
      "  --width N --height N   override resolution\n"
      "  --spp N                samples per pixel\n"
      "  --max-depth N          maximum bounces\n"
      "  --integrator NAME      path | direct | normals\n"
      "  --threads N            worker threads (0 = all cores)\n"
      "  --tile N               tile size in pixels\n"
      "  --seed N               RNG seed\n"
      "  --exposure X           exposure multiplier\n"
      "  --no-bvh               brute-force intersection (reference)\n"
      "  --no-light-sampling    disable next-event estimation\n"
      "  --single-pass          render all samples in one pass\n"
      "  --max-pass N           cap samples per progressive pass (default 32)\n"
      "  --stream               write progressive PNG frames to stdout (see source)\n"
      "  --save-every-pass      rewrite the output image after every pass\n"
      "  --snapshot-dir DIR     also save each pass as DIR/spp_NNNNN.png\n"
      "  --validate             only parse + validate the scene\n"
      "  --dump-scene           print the normalised scene JSON and exit\n"
      "  -q, --quiet            no progress output\n";
}

std::string json_escape(const std::string& s) { return Json(s).dump(-1); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 1; }
  std::string scene_path, output = "render.png";
  std::optional<int> width, height, spp, max_depth, threads, tile, max_pass;
  std::optional<std::uint64_t> seed;
  std::optional<double> exposure;
  std::optional<std::string> integrator;
  std::string snapshot_dir;
  bool no_bvh = false, no_nee = false, single_pass = false, stream = false, quiet = false;
  bool validate_only = false, dump = false, save_every = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(1); }
      return argv[++i];
    };
    auto next_int = [&]() {
      const std::string v = next();
      try { return std::stoi(v); } catch (...) { std::cerr << "invalid integer for " << a << ": " << v << "\n"; std::exit(1); }
    };
    if (a == "-o" || a == "--output") output = next();
    else if (a == "--width") width = next_int();
    else if (a == "--height") height = next_int();
    else if (a == "--spp") spp = next_int();
    else if (a == "--max-depth") max_depth = next_int();
    else if (a == "--threads") threads = next_int();
    else if (a == "--tile") tile = next_int();
    else if (a == "--max-pass") max_pass = next_int();
    else if (a == "--seed") seed = static_cast<std::uint64_t>(next_int());
    else if (a == "--exposure") exposure = std::stod(next());
    else if (a == "--integrator") integrator = next();
    else if (a == "--no-bvh") no_bvh = true;
    else if (a == "--no-light-sampling") no_nee = true;
    else if (a == "--single-pass") single_pass = true;
    else if (a == "--stream") stream = true;
    else if (a == "--save-every-pass") save_every = true;
    else if (a == "--snapshot-dir") snapshot_dir = next();
    else if (a == "--validate") validate_only = true;
    else if (a == "--dump-scene") dump = true;
    else if (a == "-q" || a == "--quiet") quiet = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (!a.empty() && a[0] == '-' && a != "-") { std::cerr << "unknown option " << a << "\n"; usage(); return 1; }
    else scene_path = a;
  }
  if (scene_path.empty()) { usage(); return 1; }

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  SceneDesc desc;
  Scene scene;
  try {
    if (scene_path == "-") {
      const std::string text{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
      desc = parse_scene(text);
    } else {
      desc = load_scene(scene_path);
    }
    auto& R = desc.render;
    if (width) R.width = *width;
    if (height) R.height = *height;
    if (spp) R.spp = *spp;
    if (max_depth) R.max_depth = *max_depth;
    if (threads) R.threads = *threads;
    if (tile) R.tile_size = *tile;
    if (seed) R.seed = *seed;
    if (exposure) R.exposure = *exposure;
    if (no_bvh) R.use_bvh = false;
    if (no_nee) R.light_sampling = false;
    if (integrator) {
      if (*integrator == "path") R.integrator = Integrator::Path;
      else if (*integrator == "direct") R.integrator = Integrator::Direct;
      else if (*integrator == "normals") R.integrator = Integrator::Normals;
      else throw SceneError("--integrator: expected path, direct or normals");
    }
    if (const auto errs = validate_scene(desc); !errs.empty()) throw SceneError(errs.front());
    if (dump) { std::cout << scene_to_json(desc).dump() << "\n"; return 0; }
    if (validate_only) { std::cerr << "scene OK: " << desc.objects.size() << " objects\n"; return 0; }
    scene = Scene::compile(desc);
  } catch (const std::exception& e) {
    if (stream) std::cout << "ERROR {\"message\":" << json_escape(e.what()) << "}\n" << std::flush;
    std::cerr << "error: " << e.what() << "\n";
    return 2;
  }

  const auto& info = scene.info;
  if (!quiet)
    std::cerr << "scene '" << desc.name << "': " << info.spheres << " spheres, " << info.triangles << " triangles, "
              << info.planes << " planes, " << info.lights << " lights"
              << (info.degenerate_skipped ? ", " + std::to_string(info.degenerate_skipped) + " degenerate skipped" : "")
              << "\nBVH: " << info.bvh.nodes << " nodes, depth " << info.bvh.max_depth << ", built in "
              << info.bvh.build_ms << " ms\n";

  RenderOptions opts;
  opts.progressive = !single_pass;
  if (max_pass) opts.max_pass_spp = *max_pass;
  Renderer renderer(scene, opts);
  if (!quiet)
    std::cerr << "rendering " << scene.settings.width << "x" << scene.settings.height << " @ " << scene.settings.spp
              << " spp, " << renderer.thread_count() << " threads, integrator=" << to_string(scene.settings.integrator) << "\n";

  auto on_pass = [&](const Framebuffer&, const RenderProgress& p) {
    if (stream) {
      const auto png = encode_png(renderer.image());
      std::string hdr = "FRAME {\"pass\":" + std::to_string(p.pass) + ",\"spp_done\":" + std::to_string(p.spp_done) +
                        ",\"spp_target\":" + std::to_string(p.spp_target) +
                        ",\"elapsed_ms\":" + std::to_string(p.elapsed_ms) + ",\"rays\":" + std::to_string(p.rays) +
                        ",\"width\":" + std::to_string(scene.settings.width) + ",\"height\":" + std::to_string(scene.settings.height) +
                        ",\"finished\":" + (p.finished ? "true" : "false") + ",\"cancelled\":" + (p.cancelled ? "true" : "false") +
                        ",\"bytes\":" + std::to_string(png.size()) + "}\n";
      std::fwrite(hdr.data(), 1, hdr.size(), stdout);
      std::fwrite(png.data(), 1, png.size(), stdout);
      std::fflush(stdout);
    }
    if (save_every) write_image(output, renderer.image());
    if (!snapshot_dir.empty() && !p.cancelled) {
      char name[32];
      std::snprintf(name, sizeof name, "/spp_%05u.png", p.spp_done);
      write_image(snapshot_dir + name, renderer.image());
    }
    if (!quiet)
      std::cerr << "\r  pass " << p.pass << ": " << p.spp_done << "/" << p.spp_target << " spp, "
                << static_cast<long>(p.elapsed_ms) << " ms" << std::flush;
  };

  const RenderProgress final = renderer.render(on_pass, &g_cancel);
  if (!quiet) std::cerr << "\n";

  try {
    if (!stream || output != "render.png") write_image(output, renderer.image());
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 3;
  }

  const auto st = renderer.stats();
  const std::string stats_json =
      "{\"cancelled\":" + std::string(final.cancelled ? "true" : "false") + ",\"spp_done\":" + std::to_string(final.spp_done) +
      ",\"elapsed_ms\":" + std::to_string(st.elapsed_ms) + ",\"rays\":" + std::to_string(st.rays) +
      ",\"shadow_rays\":" + std::to_string(st.shadow_rays) + ",\"mrays_per_sec\":" + std::to_string(st.mrays_per_sec()) +
      ",\"invalid_samples\":" + std::to_string(st.invalid_samples) + ",\"threads\":" + std::to_string(renderer.thread_count()) + "}";
  if (stream) std::cout << "DONE " << stats_json << "\n" << std::flush;
  if (!quiet)
    std::cerr << (final.cancelled ? "cancelled" : "done") << " in " << st.elapsed_ms << " ms, "
              << (st.rays + st.shadow_rays) << " rays (" << st.mrays_per_sec() << " Mrays/s)"
              << (st.invalid_samples ? ", " + std::to_string(st.invalid_samples) + " invalid samples" : "")
              << " -> " << (!stream || output != "render.png" ? output : "stdout") << "\n";
  return final.cancelled ? 130 : 0;
}
