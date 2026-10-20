# Development log

A milestone-by-milestone record. Each milestone ended with a build, the full test suite, and (where relevant) a render that was looked at.

## Environment

Ubuntu 24.04 container, GCC 13.3, Clang available, CMake 3.28, Ninja, Go 1.24, Node 22, 2 hardware threads. No GoogleTest/Catch2 installed system-wide, so Catch2 v3.5.4 (amalgamated, two files) is vendored in `third_party/`. PNG encoding uses vendored `stb_image_write.h`: writing DEFLATE and CRC32 by hand is unrelated to rendering, and PPM output needs no library at all.

## M1 — Math foundation

`Vec2/Vec3`, `Ray`, `Interval`, `AABB`, PCG32 RNG, sampling routines, ONB, camera basis. 11 test cases.

- **Caught by tests:** the slab test returned "miss" for a ray grazing a box face exactly (`0·∞ = NaN`). Fixed by treating that axis as unbounded. Later, a stricter test with a `−0.0` direction showed the first fix was incomplete; the final version skips the axis whenever a NaN appears.
- A strict equality check on a normalised vector failed by one ulp — a reminder to compare floating point with tolerances.

## M2 — First image

Sphere/plane/triangle intersection, hit records, Lambertian, point lights with hard shadows, PPM/PNG output, JSON scene loading and validation, CLI. `first_light.json` rendered correctly on the first attempt (`docs/images/first_light.png`).

- **Caught by tests:** a 1 mm sphere 10 km away lost ~6 significant digits in the textbook quadratic. Replaced with the Ray Tracing Gems formulation.

## M3 — Materials and optics

Material variant (Lambertian, metal, dielectric, emissive), Schlick Fresnel, total internal reflection, recursive specular paths, ray-origin offsets. Tests check Snell's law on entry and exit, TIR beyond the critical angle, the 4 % normal-incidence reflectance of glass, and BRDF energy conservation.

## M4 — Path tracing

Monte Carlo path tracer with cosine sampling, next-event estimation (point, sphere-cone and triangle-area sampling), Russian roulette, progressive passes, ACES/Reinhard and exact sRGB.

- **Visual bug 1:** a faint dark arc across the floor of `materials.json`. Isolated by rendering the floor alone (clean), then each sphere alone (clean), then the checker texture: hit points at `y = ±1e-17` flipped `floor(y)` between 0 and −1. Fixed with a constant offset; regression test added.
- **Visual bug 2:** fireflies across the Cornell box. Cause: two-sided area light 1 cm below the ceiling also lit the ceiling, creating a very bright hidden patch. Emitters became one-sided by default (`two_sided` is opt-in) and light sampling respects orientation.
- **Test redesign:** the first "furnace" test was only approximately solvable. Replaced by exact analytic scenes (sphere light over a plane, uniform sky, ball inside a glowing shell) plus an NEE-vs-BSDF-only agreement test. The shell test exposed that shading points *inside* a sphere light were never lit by it when light sampling was on (the cone sampler gave up and BSDF hits were suppressed to avoid double counting) — fixed with area sampling from inside.

## M5 — BVH

Binned SAH build, flattened depth-first layout, ordered traversal, any-hit for shadows, degenerate-case handling (empty, single, all-identical centroids, empty bounds, degenerate triangles). Equivalence with brute force tested on 15 000 random rays over three random scenes, including axis-parallel rays.

## M6 — Multithreading

Tile scheduler with an atomic work counter, per-pixel sample counts, cancellation, `RenderSession` with RAII shutdown, per-sample RNG streams. Tests: bit-identical output across thread counts / tile sizes / pass schedules, cancellation latency < 1 s from a 100 000 spp job, restart after cancel, destructor during a render. ThreadSanitizer clean.

## M7 — Performance

Profiled with callgrind; three targeted changes gave ~2.1× single-thread speedup (details and the rejected options in `performance.md`). `-march=native` measured: no gain; handwritten SIMD therefore not added.

## M8 — Browser editor

Go backend (stdlib only) streaming frames from `rt --stream` over SSE, one job at a time, monotonically increasing job IDs against stale results, SIGTERM cancellation, path-traversal checks for OBJ paths. React/TypeScript frontend with orbit/pan/zoom, preview renders during drags, hierarchy, inspector, materials, lights, camera/render/background settings, JSON import/export, browser-local saves.

- Go tests include real end-to-end renders through the HTTP API, error propagation from the C++ validator, and cancellation of a superseded job.
- Frontend: 11 unit tests for camera maths/colour/scene helpers and 5 jsdom tests of the real `App` (add/duplicate/delete, refusing to delete a used material, stale-frame rejection, error display, auto re-render).
- **Caught by tests:** the status readout only updated once a frame image had decoded; it now uses the streamed metadata directly.
- **Not verified:** no browser could be installed in the development sandbox (downloads blocked), so the layout and styling have not been checked visually. The HTTP flow and the UI logic are tested; the look is not.

## M9 — Gallery and documentation

Nine scenes rendered by `tools/render_gallery.sh`; convergence strip from per-pass snapshots (`--snapshot-dir`) assembled by `tools/montage.py`.
