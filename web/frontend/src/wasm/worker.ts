/// <reference lib="webworker" />
// Dedicated worker that hosts the C++ renderer compiled to WebAssembly
// (apps/rt_wasm.cpp). The page never blocks on rendering: it posts scenes
// here and receives RGBA frames back.
//
// On a cross-origin isolated page the threaded build (rt-mt) is used: the
// render runs on pthreads, this worker's event loop stays free, and a new
// job first cancels the running one and waits for it to stop. Otherwise the
// single-threaded build (rt-st) renders synchronously inside rt_start, and
// the page cancels by terminating this worker.
import type { FromWorker, ToWorker } from "./protocol";

interface RtModule {
  _rt_validate(json: number): number;
  _rt_start(job: number, json: number, maxPass: number): number;
  _rt_cancel(): void;
  _rt_running(): number;
  _rt_message(): number;
  _rt_threads(): number;
  _free(ptr: number): void;
  stringToNewUTF8(s: string): number;
  UTF8ToString(ptr: number): string;
}

const scope = self as unknown as DedicatedWorkerGlobalScope;
const post = (m: FromWorker, transfer: Transferable[] = []) => scope.postMessage(m, transfer);

let mod: RtModule | null = null;
let pending: { job: number; scene: string; maxPass: number } | null = null;
let pumpScheduled = false;

function withString<T>(m: RtModule, s: string, fn: (ptr: number) => T): T {
  const p = m.stringToNewUTF8(s);
  try {
    return fn(p);
  } finally {
    m._free(p);
  }
}

async function init(base: string) {
  const threaded = scope.crossOriginIsolated === true;
  const variant = threaded ? "rt-mt" : "rt-st";
  try {
    const url = new URL(`${variant}.mjs`, base).href;
    const factory = (await import(/* @vite-ignore */ url)).default;
    mod = (await factory({
      onFrame: (job: number, pixels: Uint8Array, info: Extract<FromWorker, { type: "frame" }>["info"]) =>
        post({ type: "frame", job, pixels, info }, [pixels.buffer]),
      onDone: (job: number, stats: Record<string, number | boolean>) => post({ type: "done", job, stats }),
      locateFile: (path: string) => new URL(path, base).href,
    })) as RtModule;
    post({ type: "ready", threaded, threads: mod._rt_threads() });
    pump();  // a job may have arrived while the module was loading
  } catch (e) {
    post({ type: "failed", message: `could not load the WebAssembly renderer: ${(e as Error).message ?? e}` });
  }
}

// Starts the newest pending job once the previous render has stopped.
function pump() {
  pumpScheduled = false;
  const m = mod;
  if (!m || !pending) return;
  if (m._rt_running()) {
    m._rt_cancel();
    pumpScheduled = true;
    setTimeout(pump, 4);
    return;
  }
  const { job, scene, maxPass } = pending;
  pending = null;
  if (withString(m, scene, (p) => m._rt_start(job, p, maxPass)) !== 0)
    post({ type: "error", job, message: m.UTF8ToString(m._rt_message()) });
}

scope.onmessage = (ev: MessageEvent<ToWorker>) => {
  const msg = ev.data;
  switch (msg.type) {
    case "init":
      void init(msg.base);
      break;
    case "validate": {
      const m = mod;
      const error = m ? withString(m, msg.scene, (p) => m.UTF8ToString(m._rt_validate(p))) : "renderer not loaded";
      post({ type: "validated", id: msg.id, error });
      break;
    }
    case "start":
      pending = msg;  // a newer request supersedes any that has not started yet
      if (!pumpScheduled) pump();
      break;
    case "cancel":
      pending = null;
      mod?._rt_cancel();
      break;
  }
};
