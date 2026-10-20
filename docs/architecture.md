# Architecture

## Layers

```
 browser editor (React/TS)      edits a JSON scene, shows frames
        │                                   │
        │ postMessage (Web Worker)          │ HTTP + Server-Sent Events   (?engine=server)
 rt_wasm (apps/rt_wasm.cpp)          editor backend (Go)   validates, runs `rt --stream`
   C++ → WebAssembly, pthreads              │  stdin: scene JSON   stdout: FRAME/DONE/ERROR
        │                            rt CLI (apps/rt_cli.cpp)   arguments, signals, files
        │  C++ API                          │  C++ API
 rtcore library                 everything that renders
   io/         JSON, scene (de)serialisation + validation, OBJ, PNG/PPM
   core/       SceneDesc (editable)  ──compile──▶  Scene (render-ready)
   geometry/   shapes, meshes, BVH
   materials/  BSDF sampling and evaluation
   rendering/  integrators, framebuffer, tone mapping, threaded renderer
   math/       Vec3, Ray, Interval, AABB, RNG, sampling, ONB
```

Dependencies only point downwards. `rtcore` has no knowledge of the CLI, HTTP, or the browser, and it builds and runs without them (the editor is optional). The browser contains no rendering code; the only "graphics" it does is drawing the PNG it receives onto a canvas.

## Two representations of a scene

| | `SceneDesc` (`core/scene_desc.hpp`) | `Scene` (`core/scene.hpp`) |
|---|---|---|
| Purpose | What the user edits; round-trips to JSON | What the integrator traverses |
| Materials | Named (`"glass"`) | Indexed (`MaterialId`) |
| Shapes | Sphere, plane, triangle, quad, box, mesh descriptors | Flat `std::vector<std::variant<Sphere, Triangle>>` plus planes |
| Lights | Point lights | Point lights + list of emissive primitives for sampling |
| Acceleration | none | BVH over primitive bounds |

`Scene::compile` resolves names, tessellates quads/boxes/meshes into triangles, drops degenerate triangles (counted in `SceneBuildInfo`), builds the light list and the BVH. Parsing and validation never see acceleration structures, and rendering never sees strings. This split is also what makes the JSON round-trip test (`parse → serialise → parse` equals the original) straightforward.

## Value types and ownership

- Math types, shapes and materials are small aggregates with value semantics. Materials are a `std::variant` rather than a class hierarchy: copyable, comparable (`operator==` is defaulted, which the round-trip test relies on), stored contiguously, and `std::visit` makes forgetting a case a compile error.
- `Scene` owns all of its data by value (`std::vector`s). Copying a scene is a deep copy; moving it is cheap. Nothing in it is reference-counted or shared.
- `Renderer` holds a `const Scene&` and owns its `Framebuffer`. The scene must outlive the renderer. `RenderSession` encodes that by owning both: it holds the `Scene` by value and a `std::unique_ptr<Renderer>` that points into it.
- `RenderSession`'s destructor cancels and joins its worker (`std::jthread`), so a session can never outlive the thread that renders into it — RAII applied to a thread.
- The only manual memory in the project is in vendored `stb_image_write`, isolated in `image_io.cpp`.

## Threading model

```
coordinator thread                 workers (N-1 jthreads + coordinator)
──────────────────                 ────────────────────────────────────
for each progressive pass:
  spawn workers ───────────────▶  loop: tile = next.fetch_add(1)
                                         render tile (all its pixels,
                                         samples [s0, s1))
  join (jthread scope exit)  ◀──   exit when tiles run out or cancel set
  call progress callback
  (framebuffer is quiescent)
```

- **No locks on the image.** Tiles are disjoint, so every pixel is written by exactly one thread during a pass. The framebuffer is only read (by the callback, PNG encoding) between passes, after the join, which provides the happens-before edge.
- **Dynamic scheduling.** Tiles are claimed from one atomic counter, so a thread that drew cheap sky tiles simply takes more of them.
- **Statistics** are accumulated in a per-tile local `PathStats` and added to shared atomics once per tile. Each shared counter is `alignas(64)` to avoid false sharing between them.
- **Cancellation** is an `std::atomic<bool>` checked before claiming a tile and at the start of every tile row, so the worst-case latency is one row of one tile. Because each pixel stores its own sample count, a pass interrupted halfway still resolves to a correct average.
- **Determinism.** The random stream for sample *s* of pixel *p* is seeded from `(seed, p, s)`. Results are therefore identical for any number of threads, tile size or pass schedule; `test_render.cpp` checks this bit-for-bit.

The `Scene` is immutable during rendering and read concurrently without synchronisation.

## Progressive rendering and streaming

Pass sizes grow 1, 1, 2, 4, 8, … samples per pixel (capped by `--max-pass`, default 32), so the first image appears after a single sample and the number of passes stays logarithmic. In `--stream` mode the CLI writes after every pass:

```
FRAME {"pass":3,"spp_done":4,"spp_target":64,...,"bytes":110527}\n<PNG bytes>
DONE {"cancelled":false,"elapsed_ms":...}\n
```

or `ERROR {"message":"objects[2].radius: must be > 0"}` if the scene is rejected. SIGTERM/SIGINT set the cancel flag; the CLI then finishes the current tile rows, emits a final frame, and exits with status 130.

## WebAssembly build

`apps/rt_wasm.cpp` is a second front end for `rtcore`, compiled with Emscripten (`tools/build_wasm.sh`) in two variants:

- **`rt-mt`** (`-pthread`): `rt_start` creates a `RenderSession`, whose coordinator runs on a pthread and spawns the usual tile workers, so the threading model above is unchanged. The Web Worker that hosts the module therefore never blocks, and it can receive a cancel request at any time. The pthread pool is pre-sized to `navigator.hardwareConcurrency` so that no thread waits for a Web Worker to start. Frames are tone-mapped into an RGBA buffer on the coordinator and handed to the worker's event loop with `MAIN_THREAD_ASYNC_EM_ASM`. JavaScript copies the buffer and frees it.
- **`rt-st`**: used when the page is not cross-origin isolated (no `SharedArrayBuffer`). It renders synchronously with one thread, and the page cancels it by terminating the worker.

Both are built with native wasm exceptions (zero cost when nothing throws) and `-msimd128`. Because sample seeds do not depend on scheduling, both variants produce the same bytes; `tools/wasm_smoke.mjs` checks this.

The page side (`web/frontend/src/wasm/`) mirrors the server client: every job gets an id, a new job supersedes the old one, and replies from superseded jobs are dropped. The worker keeps at most one pending job, so a burst of camera-drag previews collapses into "cancel the current one, start the newest".

## Editor backend

`web/backend` (Go, standard library only):

- One job at a time. Starting a render cancels the previous job (SIGTERM, then SIGKILL after 5 s via `exec.Cmd.WaitDelay`).
- Job IDs increase monotonically. Every event and frame URL carries the job ID and the browser ignores anything that is not from its current job, so a slow, stale render cannot overwrite a newer one. The client also bumps a generation counter before the job ID is known, which covers the window while `POST /api/render` is in flight.
- Frames are not pushed through SSE. Events carry metadata only and the browser fetches `/api/render/{id}/frame` (the latest PNG). A slow browser therefore skips intermediate frames instead of building up a queue; the job's event fan-out drops frames for slow subscribers but always delivers the terminal event.
- Request bodies are capped at 4 MB, and OBJ paths must be relative and stay inside the scenes folder. The server listens on 127.0.0.1 by default because it is a local tool.

## Editor frontend

`web/frontend` (React 18 + TypeScript + Vite, no UI library):

- `types.ts` mirrors the JSON schema; `scene.ts` holds pure, tested helpers (orbit/pan/dolly maths, sRGB↔linear colour conversion, naming, duplication).
- `api.ts` — `RenderClient` encapsulates the start/cancel/stale-frame logic.
- `Viewport.tsx` turns pointer gestures into camera edits. While dragging it requests throttled preview renders (≤240 px, 2 spp, 4 bounces); on release it requests a full render.
- `panels.tsx` — hierarchy and inspector. Colour pickers convert between the sRGB swatch the user sees and the linear albedo the renderer uses.

## Design decisions worth revisiting

- `double` everywhere. Simple and robust; a float/SIMD BVH would be faster (see performance.md).
- Planes are kept outside the BVH (they are unbounded) and tested linearly; fine for a handful of planes.
- Uniform light selection. Power-proportional selection would reduce noise in scenes with very unequal lights.
- Process-per-render in the editor. Spawning `rt` costs a few milliseconds and gives isolation for free (a crash cannot take down the server); a long-lived renderer process would make previews slightly snappier.
