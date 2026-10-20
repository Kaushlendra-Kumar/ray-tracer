# Performance

All numbers in this file were **measured** on the development machine: a virtualised Intel Xeon @ 2.1 GHz with **2 hardware threads**, 4 MB L2, GCC 13.3, `-O3` (CMake Release). They are reproducible with the commands given; expect different absolute values elsewhere. "Rays" counts both closest-hit rays and shadow rays.

## Method: profile first

Hotspots were found with `valgrind --tool=callgrind` on a `-O3 -g` build (`build-prof`), then each change was measured with the same render before and after. Only changes that the profile pointed at were made.

### Optimisations made (Cornell box, 512×512, 16 spp, 1 thread)

| Step | Time | Mrays/s | What the profile showed |
|---|---:|---:|---|
| First working version | 9.53 s | 2.02 | `fmin`/`fmax` from libm were 18 % of instructions: they are NaN-aware library calls, not instructions |
| Slab test with plain comparisons (`minsd`/`maxsd`) | 6.16 s | 3.12 | intersection code still dominant |
| Defer shading data to the final hit | 4.71 s | 4.09 | every *candidate* hit was computing a normalised normal, UVs and a full `HitRecord`; now traversal computes only `t` and barycentrics |
| Explicit per-axis slab test | — | +~40 % on the BVH micro-benchmark (4.8 → 6.9 Mrays/s, 22 triangles) | the generic `operator[]` loop was not reliably unrolled |

Overall: **about 2.1× faster** single-threaded than the first correct version, with all tests still passing after each step.

### Considered and rejected (with evidence)

- **`-march=native`** (option `RT_NATIVE`), 320×200, 8 spp, 1 thread:

  | Scene | Default | Native |
  |---|---:|---:|
  | cornell_box | 4.43 Mrays/s | 4.30 Mrays/s |
  | sphere_field | 3.88 Mrays/s | 4.07 Mrays/s |
  | mesh_bvh | 2.51 Mrays/s | 2.40 Mrays/s |

  Differences are within run-to-run noise. Auto-vectorisation finds nothing to do in branchy one-ray-at-a-time traversal.

- **Handwritten SIMD / AVX2.** The roadmap asks for SIMD only if profiling demonstrates a bottleneck it would fix. The profile shows the cost is spread across traversal control flow (stack pushes, branch mispredictions on box hits) and memory access, not arithmetic throughput: a slab test is ~12 flops. SIMD would pay off only with a different data layout — a 4-wide BVH (one node tests four child boxes in one AVX2 instruction sequence) or ray packets for coherent primary rays — which is a substantial redesign. It is listed as future work rather than implemented half-way. Nothing in the code base pretends otherwise.

## BVH vs brute force

`./build/bench_bvh 7 6000` — icospheres of increasing subdivision plus a floor quad, 256×256 primary rays and one diffuse bounce each:

| triangles | BVH nodes | depth | build ms | BVH trace ms | BVH Mrays/s | nodes/ray | tests/ray (BVH) | brute trace ms | brute Mrays/s | tests/ray (brute) | speedup | results equal |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|:---:|
| 22 | 17 | 5 | 0.07 | 14.9 | 7.17 | 8.1 | 3.5 | 28.0 | 3.818 | 22 | 1.9x | yes |
| 82 | 57 | 7 | 0.08 | 19.2 | 5.69 | 12.4 | 3.6 | 88.1 | 1.237 | 82 | 4.6x | yes |
| 322 | 201 | 8 | 0.42 | 25.8 | 4.25 | 16.5 | 4.3 | 291.7 | 0.376 | 322 | 11.3x | yes |
| 1282 | 837 | 11 | 1.50 | 39.5 | 2.78 | 21.2 | 4.0 | 1064.7 | 0.103 | 1282 | 27.0x | yes |
| 5122 | 3317 | 13 | 6.27 | 45.8 | 2.40 | 24.5 | 4.1 | 3891.0 | 0.028 | 5122 | 84.9x | yes |
| 20482 | 13025 | 16 | 29.15 | 52.8 | 2.08 | 28.8 | 4.1 | skipped | - | - | - | - |
| 81922 | 52499 | 18 | 130.14 | 65.5 | 1.68 | 32.2 | 4.1 | skipped | - | - | - | - |
| 327682 | 209495 | 20 | 601.68 | 77.3 | 1.42 | 36.1 | 4.1 | skipped | - | - | - | - |

What it shows:

- **Brute force is linear** in the triangle count (tests/ray = N). The BVH tests **~4 triangles per ray regardless of size**; nodes visited per ray grow by about 2 per 4× more triangles — logarithmic, as expected.
- Trace time grows only 5× from 22 to 327 682 triangles (15 000× more geometry). Beyond ~5 000 triangles the growth comes from cache misses: the 327 k-triangle tree is ~12 MB of nodes plus 52 MB of triangles, far more than L2.
- **Build cost** is O(N log N): 0.6 s for 327 k triangles. In the `mesh_bvh` demo scene (174 k triangles) the build takes 0.29 s of a 17 s render.
- Brute force on the 174 k-triangle demo scene would take roughly 174 000 / 4 ≈ 40 000× more intersection tests; it was not run to completion.
- "results equal" compares the number of hits; the unit tests compare every hit distance to 1e-12.

## Multithreading

`./build/bench_threads assets/scenes/sphere_field.json 4 4` (479 spheres with glass, metal, depth of field; 4 spp; single pass):

| resolution | threads | ms | speedup | Mrays/s |
|---|---:|---:|---:|---:|
| 320x180 | 1 | 132 | 1.00x | 4.07 |
| 320x180 | 2 | 69 | 1.93x | 7.85 |
| 320x180 | 4 | 70 | 1.90x | 7.74 |
| 640x360 | 1 | 556 | 1.00x | 3.86 |
| 640x360 | 2 | 259 | 2.15x | 8.30 |
| 640x360 | 4 | 263 | 2.11x | 8.17 |

Tile size at 640×360, 2 threads:

| tile | ms | Mrays/s |
|---:|---:|---:|
| 1 | 337 | 6.37 |
| 4 | 296 | 7.27 |
| 8 | 294 | 7.30 |
| 16 | 246 | 8.73 |
| 32 | 239 | 8.99 |
| 64 | 262 | 8.21 |
| 128 | 269 | 7.98 |

Interpretation:

- **Near-linear scaling on the two available cores** (1.9–2.15×; values slightly above 2× are within run-to-run noise on a shared VM). Path tracing is embarrassingly parallel: threads share only the read-only scene and a tile counter. A machine with more cores is needed to see where scaling flattens; the expected limits are memory bandwidth for large meshes and the per-pass barrier for very small images.
- **Oversubscription** (4 threads on 2 cores) costs nothing measurable, because work is claimed dynamically.
- **Tile size:** 1×1 tiles are ~40 % slower: one atomic `fetch_add` and one statistics flush per pixel, and neighbouring threads writing adjacent pixels of the same cache lines (false sharing on the framebuffer). Very large tiles hurt load balance at the end of a pass (128×128 gives only 15 tiles for 2 threads). 16–32 is the sweet spot; 16 is the default.
- **Per-pass barrier:** each progressive pass joins all workers. Pass sizes double, so a render has only ~log₂(spp) passes and the barrier cost is negligible; with `--max-pass 1` (editor previews) every sample is a pass, which is acceptable only because previews use 2 spp.

### Synchronisation costs that were avoided

- Shared statistics are updated once per tile from thread-local counters, and each atomic counter has its own 64-byte cache line (`alignas(64)`), so workers do not invalidate each other's lines.
- The framebuffer needs no locks: tiles are disjoint and reads happen only after the pass barrier.
- `std::memory_order_relaxed` is used for the counters and cancel flag: they carry no data dependencies; the join provides the ordering for the image itself.

## Correctness tooling

- **AddressSanitizer + UndefinedBehaviorSanitizer** (`-DRT_SANITIZE=ON`, Debug): the full test suite passes with no reports, including LeakSanitizer. The CLI was also run under ASan on the 174 k-triangle scene and through a SIGTERM cancellation in streaming mode.
- **ThreadSanitizer** (`-DRT_TSAN=ON`): the rendering, cancellation, restart and RAII-shutdown tests pass with no reported races.

## Reproducing

```sh
cmake -S . -B build -G Ninja && cmake --build build
./build/bench_bvh 7 6000
./build/bench_threads assets/scenes/sphere_field.json 4
cmake -S . -B build-native -DRT_NATIVE=ON && cmake --build build-native
./build/rt assets/scenes/cornell_box.json -o /tmp/a.png --spp 16 --threads 1
```

## Future work, in expected order of payoff

1. Multiple importance sampling (a variance reduction, i.e. "speed" at equal quality, larger than any micro-optimisation here).
2. Float-precision 4-wide BVH with SIMD box tests and precomputed triangle edges.
3. Triangle data split from vertex normals (normals are only needed for the final hit), halving the bytes touched per leaf.
4. Power-proportional light selection.
