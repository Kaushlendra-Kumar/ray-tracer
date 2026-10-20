// Smoke test for the WebAssembly renderer (run after tools/build_wasm.sh):
// renders a shipped scene with both variants in Node and checks the frames,
// the completion record, cancellation and error reporting.
// Usage: node tools/wasm_smoke.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const scene = JSON.parse(readFileSync(path.join(root, "assets/scenes/cornell_box.json"), "utf8"));
scene.render = { ...scene.render, width: 64, height: 64, spp: 16 };

function check(cond, msg) {
  if (!cond) { console.error(`FAIL: ${msg}`); process.exit(1); }
}

async function run(variant) {
  const url = pathToFileURL(path.join(root, `web/frontend/public/wasm/rt-${variant}.mjs`)).href;
  const factory = (await import(url)).default;
  const events = [];
  let resolveDone;
  const done = new Promise((r) => { resolveDone = r; });
  const mod = await factory({
    onFrame: (job, pixels, info) => events.push({ job, pixels, info }),
    onDone: (job, stats) => resolveDone({ job, stats }),
  });
  const withString = (s, fn) => { const p = mod.stringToNewUTF8(s); try { return fn(p); } finally { mod._free(p); } };

  const err = withString('{"objects": [{"type": "sphere", "center": [0,0,0], "radius": -1, "material": "x"}]}',
    (p) => mod.UTF8ToString(mod._rt_validate(p)));
  check(err.length > 0, `${variant}: invalid scene accepted`);

  check(withString(JSON.stringify(scene), (p) => mod._rt_start(7, p, 0)) === 0, `${variant}: start failed: ${mod.UTF8ToString(mod._rt_message())}`);
  const { job, stats } = await done;
  check(job === 7 && !stats.cancelled && stats.spp_done === 16, `${variant}: bad done record ${JSON.stringify(stats)}`);
  const last = events.at(-1);
  check(events.length >= 2 && last.info.finished, `${variant}: expected progressive frames, got ${events.length}`);
  check(last.pixels.length === 64 * 64 * 4, `${variant}: wrong frame size`);
  let sum = 0;
  for (let i = 0; i < last.pixels.length; i += 4) sum += last.pixels[i] + last.pixels[i + 1] + last.pixels[i + 2];
  check(sum / (64 * 64 * 3) > 10, `${variant}: image is black`);
  console.log(`rt-${variant}: ${events.length} frames, ${stats.threads} threads, ${stats.elapsed_ms.toFixed(0)} ms, ` +
    `${stats.mrays_per_sec.toFixed(2)} Mrays/s, mean ${(sum / (64 * 64 * 3)).toFixed(1)}`);

  if (variant === "mt") {
    // Cancellation: a long render stops promptly and reports cancelled.
    const big = { ...scene, render: { ...scene.render, width: 256, height: 256, spp: 100000 } };
    const cancelled = new Promise((r) => { resolveDone = r; });
    check(withString(JSON.stringify(big), (p) => mod._rt_start(8, p, 0)) === 0, "mt: second start failed");
    const t0 = Date.now();
    setTimeout(() => mod._rt_cancel(), 300);
    const d = await cancelled;
    check(d.job === 8 && d.stats.cancelled, "mt: render was not cancelled");
    console.log(`rt-mt: cancelled after ${Date.now() - t0} ms at ${d.stats.spp_done} spp`);
    while (mod._rt_running()) await new Promise((r) => setTimeout(r, 5));
  }
  return last.pixels;
}

// Emscripten unrefs its pthread workers in Node; keep the process alive.
const keepAlive = setInterval(() => {}, 1000);
const st = await run("st");
const mt = await run("mt");
// Per-sample RNG seeding makes the image independent of the thread count.
check(Buffer.compare(Buffer.from(st), Buffer.from(mt)) === 0, "single- and multi-threaded images differ");
console.log("rt-st and rt-mt images are bit-identical");
console.log("wasm smoke test passed");
clearInterval(keepAlive);
process.exit(0);
