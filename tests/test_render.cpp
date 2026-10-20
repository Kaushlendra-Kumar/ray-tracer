// Integration tests: render small scenes end-to-end.
#include <catch2/catch_amalgamated.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#include "raytracer/core/scene.hpp"
#include "raytracer/io/image_io.hpp"
#include "raytracer/io/scene_io.hpp"
#include "raytracer/rendering/integrator.hpp"
#include "raytracer/rendering/renderer.hpp"

using namespace rt;
using Catch::Approx;

namespace {

SceneDesc load_asset(const std::string& name, int w, int h, int spp) {
  SceneDesc d = load_scene(std::string(RT_ASSET_DIR) + "/scenes/" + name);
  d.render.width = w;
  d.render.height = h;
  d.render.spp = spp;
  return d;
}

// Analytic reference: a diffuse plane (albedo a) lit only by a spherical
// emitter of radiance Le, radius r, centre at height d above the shading
// point. Irradiance from a uniform spherical source is E = pi Le (r/d)^2, so
// the reflected radiance is exactly a * Le * (r/d)^2. No other light paths
// exist (black background, a plane cannot see itself), so the estimate must
// converge to this value with or without light sampling.
SceneDesc sphere_light_over_plane(bool nee) {
  SceneDesc d;
  d.camera.look_from = {3, 3, 0.0};
  d.camera.look_at = {0, 0, 0};
  d.camera.vfov_deg = 0.2;  // the image covers a tiny patch around the origin
  d.background.kind = Background::Kind::Solid;
  d.background.top = Color{0};
  d.render.width = 4;
  d.render.height = 4;
  d.render.spp = nee ? 4096 : 65536;
  d.render.max_depth = 16;
  d.render.light_sampling = nee;
  d.render.threads = 2;
  d.materials.push_back({"plane", Lambertian{{Color{0.8}, Color{0.8}, 0}}});
  d.materials.push_back({"glow", Emissive{Color{1}, 4}});
  d.objects.push_back({"floor", "plane", PlaneDesc{{0, 0, 0}, {0, 1, 0}}});
  d.objects.push_back({"light", "glow", SphereDesc{{0, 2, 0}, 0.5}});
  return d;
}

Real mean_luminance(const Framebuffer& fb) {
  Real s = 0;
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) s += fb.mean(x, y).y;
  return s / (fb.width() * fb.height());
}

}  // namespace

TEST_CASE("Rendering is deterministic for a fixed seed regardless of threads, tiles and passes", "[render]") {
  SceneDesc d = load_asset("cornell_box.json", 48, 48, 8);
  d.render.threads = 1;
  d.render.tile_size = 16;
  const Scene s1 = Scene::compile(d);
  Renderer r1(s1, RenderOptions{false, 32});
  r1.render();

  d.render.threads = 4;
  d.render.tile_size = 7;
  const Scene s2 = Scene::compile(d);
  Renderer r2(s2, RenderOptions{true, 2});
  r2.render();

  const Image8 a = r1.image(), b = r2.image();
  REQUIRE(a.rgb.size() == b.rgb.size());
  CHECK(a.rgb == b.rgb);
  for (int y = 0; y < 48; y += 5)
    for (int x = 0; x < 48; x += 5) CHECK(r1.framebuffer().mean(x, y) == r2.framebuffer().mean(x, y));

  d.render.seed = 99;  // different seed => different noise
  const Scene s3 = Scene::compile(d);
  Renderer r3(s3);
  r3.render();
  CHECK(r3.image().rgb != a.rgb);
}

TEST_CASE("Path tracer converges to an analytic solution", "[render][unbiased]") {
  const Real expected = 0.8 * 4 * (0.5 * 0.5) / (2.0 * 2.0);  // a Le (r/d)^2 = 0.2
  for (bool nee : {true, false}) {
    INFO("light sampling " << nee);
    const Scene s = Scene::compile(sphere_light_over_plane(nee));
    Renderer r(s);
    r.render();
    CHECK(mean_luminance(r.framebuffer()) == Approx(expected).epsilon(nee ? 0.01 : 0.03));
    CHECK(r.stats().invalid_samples == 0);
  }
}

TEST_CASE("Inside a two-sided emissive sphere the answer is exact", "[render][unbiased]") {
  // A diffuse ball of albedo a inside a closed two-sided emitter of radiance E:
  // every direction sees E (directly) or the ball itself is convex so it never
  // sees itself; hence L = a * E exactly.
  for (bool nee : {true, false}) {
    INFO("light sampling " << nee);
    SceneDesc d;
    d.camera.look_from = {0, 0, 2.5};
    d.camera.look_at = {0, 0, 0};
    d.camera.vfov_deg = 5;
    d.render.width = 4;
    d.render.height = 4;
    d.render.spp = 512;
    d.render.light_sampling = nee;
    d.render.threads = 2;
    d.materials.push_back({"ball", Lambertian{{Color{0.6}, Color{0.6}, 0}}});
    d.materials.push_back({"shell", Emissive{Color{1}, 2, true}});
    d.objects.push_back({"ball", "ball", SphereDesc{{0, 0, 0}, 1}});
    d.objects.push_back({"shell", "shell", SphereDesc{{0, 0, 0}, 5}});
    const Scene s = Scene::compile(d);
    Renderer r(s);
    r.render();
    CHECK(mean_luminance(r.framebuffer()) == Approx(0.6 * 2).epsilon(0.01));
  }
}

TEST_CASE("Uniform sky over a diffuse plane gives albedo * sky", "[render][unbiased]") {
  SceneDesc d = sphere_light_over_plane(true);
  d.objects.pop_back();  // no sphere light
  d.background.top = Color{0.5};
  d.render.spp = 2048;
  const Scene s = Scene::compile(d);
  Renderer r(s);
  r.render();
  CHECK(mean_luminance(r.framebuffer()) == Approx(0.8 * 0.5).epsilon(0.02));
}

TEST_CASE("Light sampling and BSDF sampling agree in expectation", "[render][unbiased]") {
  SceneDesc d = load_asset("cornell_box.json", 16, 16, 1024);
  d.render.threads = 2;
  d.render.tone_map = ToneMap::Clamp;
  const Scene with = Scene::compile(d);
  d.render.light_sampling = false;
  const Scene without = Scene::compile(d);
  Renderer a(with), b(without);
  a.render();
  b.render();
  const Real la = mean_luminance(a.framebuffer()), lb = mean_luminance(b.framebuffer());
  INFO("NEE " << la << " vs BSDF-only " << lb);
  CHECK(la == Approx(lb).epsilon(0.04));
}

TEST_CASE("Direct light from a point light follows the inverse-square law", "[render]") {
  SceneDesc d;
  d.materials.push_back({"white", Lambertian{{Color{1}, Color{1}, 0}}});
  d.objects.push_back({"floor", "white", PlaneDesc{{0, 0, 0}, {0, 1, 0}}});
  d.lights.push_back({"l", {0, 2, 0}, Color{8}});
  const Scene s = Scene::compile(d);
  HitRecord rec;
  REQUIRE(s.intersect(Ray{{0, 1, 0}, {0, -1, 0}}, Interval(1e-6, kInfinity), rec));
  Rng rng(1);
  PathStats st;
  // E = I cos / r^2 = 8 / 4 = 2; Lo = albedo/pi * E.
  const Color L = sample_direct_light(s, rec, s.material(rec.material), rng, st);
  CHECK(L.x == Approx(2 / kPi));
  // Blocked by an occluder: hard shadow.
  d.objects.push_back({"blocker", "white", SphereDesc{{0, 1, 0}, 0.2}});
  const Scene s2 = Scene::compile(d);
  REQUIRE(s2.intersect(Ray{{0.0, 0.5, 0.0}, {0, -1, 0}}, Interval(1e-6, kInfinity), rec));
  CHECK(sample_direct_light(s2, rec, s2.material(rec.material), rng, st).is_zero());
}

TEST_CASE("Every shipped scene renders to a valid image", "[render][integration]") {
  const auto out_dir = std::filesystem::temp_directory_path() / "rt_test_out";
  std::filesystem::create_directories(out_dir);
  for (const auto& entry : std::filesystem::directory_iterator(std::string(RT_ASSET_DIR) + "/scenes")) {
    if (entry.path().extension() != ".json") continue;
    INFO(entry.path().string());
    SceneDesc d = load_scene(entry.path().string());
    d.render.width = 32;
    d.render.height = 24;
    d.render.spp = 2;
    const Scene s = Scene::compile(d);
    Renderer r(s);
    const RenderProgress p = r.render();
    CHECK(p.finished);
    CHECK(r.stats().invalid_samples == 0);
    const Image8 img = r.image();
    REQUIRE(img.rgb.size() == 32 * 24 * 3);
    // Not a blank frame: some variation in the pixels.
    int lo = 255, hi = 0;
    for (auto v : img.rgb) { lo = std::min<int>(lo, v); hi = std::max<int>(hi, v); }
    CHECK(hi - lo > 10);
    // PNG + PPM writers produce well-formed files.
    const auto png = encode_png(img);
    REQUIRE(png.size() > 33);
    CHECK(png[0] == 0x89);
    CHECK(png[1] == 'P');
    CHECK(png[2] == 'N');
    CHECK(png[3] == 'G');
    // IHDR width/height are big-endian at offset 16/20.
    CHECK(((png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19]) == 32);
    CHECK(((png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23]) == 24);
    const auto ppm = (out_dir / (entry.path().stem().string() + ".ppm")).string();
    write_ppm(ppm, img);
    const Image8 back = read_ppm(ppm);
    CHECK(back.rgb == img.rgb);
  }
}

TEST_CASE("Progressive rendering reports increasing sample counts", "[render][progressive]") {
  SceneDesc d = load_asset("first_light.json", 32, 32, 20);
  const Scene s = Scene::compile(d);
  Renderer r(s, RenderOptions{true, 8});
  std::vector<std::uint32_t> spp;
  r.render([&](const Framebuffer& fb, const RenderProgress& p) {
    spp.push_back(p.spp_done);
    CHECK(fb.min_samples() == p.spp_done);
  });
  CHECK(spp == std::vector<std::uint32_t>{1, 2, 4, 8, 16, 20});
}

TEST_CASE("Cancellation stops quickly and leaves a consistent buffer", "[render][threads]") {
  SceneDesc d = load_asset("cornell_box.json", 128, 128, 100000);
  d.render.threads = 4;
  RenderSession session(Scene::compile(d));
  std::atomic<int> callbacks{0};
  session.start([&](const Framebuffer&, const RenderProgress&) { callbacks++; });
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(session.running());
  const auto t0 = std::chrono::steady_clock::now();
  session.cancel();
  const RenderProgress p = session.wait();
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(p.cancelled);
  CHECK_FALSE(p.finished);
  CHECK_FALSE(session.running());
  CHECK(ms < 1000);  // stops within one tile row, not after 100000 spp
  CHECK(callbacks > 0);
  // Every pixel's average is finite even though the last pass was partial.
  const Framebuffer& fb = session.renderer().framebuffer();
  for (int y = 0; y < fb.height(); ++y)
    for (int x = 0; x < fb.width(); ++x) REQUIRE(fb.mean(x, y).is_finite());

  // Restart with a small job runs to completion.
  d.render.spp = 4;
  d.render.width = 16;
  d.render.height = 16;
  session.restart(Scene::compile(d));
  const RenderProgress p2 = session.wait();
  CHECK(p2.finished);
  CHECK_FALSE(p2.cancelled);
}

TEST_CASE("Destroying a running session cancels and joins (RAII)", "[render][threads]") {
  SceneDesc d = load_asset("cornell_box.json", 64, 64, 100000);
  d.render.threads = 3;
  const auto t0 = std::chrono::steady_clock::now();
  {
    RenderSession session(Scene::compile(d));
    session.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }  // destructor runs here
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(ms < 2000);
}

TEST_CASE("Tone mapping and sRGB encoding", "[render]") {
  CHECK(linear_to_srgb8(0) == 0);
  CHECK(linear_to_srgb8(1) == 255);
  CHECK(linear_to_srgb8(0.5) == 188);   // sRGB(0.5) = 0.7354
  CHECK(linear_to_srgb8(-1) == 0);
  CHECK(linear_to_srgb8(10) == 255);
  const Color nan_in{std::nan(""), 1, 1};
  CHECK(tone_map(nan_in, ToneMap::Aces, 1).is_finite());
  CHECK(tone_map(Color{1e9}, ToneMap::Reinhard, 1).x < 1.0 + 1e-12);
  CHECK(tone_map(Color{1e9}, ToneMap::Aces, 1).x <= 1.0);
}
