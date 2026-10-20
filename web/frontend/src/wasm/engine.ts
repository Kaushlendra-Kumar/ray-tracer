// In-browser engine: renders with the C++ path tracer compiled to
// WebAssembly (apps/rt_wasm.cpp) in a Web Worker, so the editor works as a
// static site with no server at all.
import type { Engine, FrameImage, FrameInfo, RenderClientLike, RenderStatus } from "../api";
import type { Scene } from "../types";
import type { DoneStats, FrameMeta, FromWorker, ToWorker } from "./protocol";

// The example scenes are bundled from assets/scenes at build time.
const examples = import.meta.glob("../../../../assets/scenes/*.json", { import: "default" });
const exampleName = (path: string) => path.slice(path.lastIndexOf("/") + 1, -".json".length);

interface JobHandlers {
  frame(info: FrameMeta, pixels: Uint8Array): void;
  done(stats: DoneStats): void;
  error(message: string): void;
}

interface Ready {
  threaded: boolean;
  threads: number;
}

// Owns the render worker. One job runs at a time and starting a job
// supersedes the previous one; replies from superseded jobs are dropped here,
// and job ids let clients drop anything that still slips through.
class WorkerHost {
  private worker!: Worker;
  ready!: Promise<Ready>;
  private loaded: Ready | null = null;
  private jobs = new Map<number, JobHandlers>();
  private validations = new Map<number, (error: string) => void>();
  private nextId = 0;
  private running = 0;  // job currently rendering, 0 = idle

  constructor(private base: string) {
    this.spawn();
  }

  private send(m: ToWorker) {
    this.worker.postMessage(m);
  }

  private spawn() {
    const w = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
    this.worker = w;
    this.loaded = null;
    this.ready = new Promise<Ready>((resolve, reject) => {
      w.onmessage = (ev: MessageEvent<FromWorker>) => {
        const m = ev.data;
        switch (m.type) {
          case "ready": this.loaded = m; resolve(m); break;
          case "failed": reject(new Error(m.message)); this.failAll(m.message); break;
          case "validated": this.validations.get(m.id)?.(m.error); this.validations.delete(m.id); break;
          case "frame": this.jobs.get(m.job)?.frame(m.info, m.pixels); break;
          case "done": this.finish(m.job)?.done(m.stats); break;
          case "error": this.finish(m.job)?.error(m.message); break;
        }
      };
      w.onerror = (e) => {
        e.preventDefault();
        const message = `the render worker crashed${e.message ? `: ${e.message}` : ""}`;
        reject(new Error(message));
        this.failAll(message);
      };
    });
    this.ready.catch(() => undefined);
    this.send({ type: "init", base: this.base });
  }

  private finish(job: number): JobHandlers | undefined {
    const h = this.jobs.get(job);
    this.jobs.delete(job);
    if (this.running === job) this.running = 0;
    return h;
  }

  private failAll(message: string) {
    for (const job of [...this.jobs.keys()]) this.finish(job)?.error(message);
  }

  // The single-threaded worker is busy inside rt_start and cannot read a
  // cancel message, so stopping it means replacing it. `notify` reports the
  // stop as a cancellation (not wanted when a new job supersedes the old).
  private stopBlockingRender(notify: boolean): boolean {
    if (!this.running || !this.loaded || this.loaded.threaded) return false;
    const job = this.running;
    this.worker.terminate();
    this.spawn();
    const h = this.finish(job);
    if (notify) h?.done({ cancelled: true });
    return true;
  }

  render(scene: Scene, maxPass: number, h: JobHandlers): number {
    const job = ++this.nextId;
    this.stopBlockingRender(false);
    this.jobs.clear();  // superseded jobs: their replies are no longer wanted
    this.jobs.set(job, h);
    this.running = job;
    const msg: ToWorker = { type: "start", job, scene: JSON.stringify(scene), maxPass };
    this.ready.then(() => this.send(msg), (e: Error) => this.finish(job)?.error(e.message));
    return job;
  }

  cancel(job: number) {
    if (job !== this.running) return;
    if (!this.stopBlockingRender(true)) this.send({ type: "cancel" });
  }

  async validate(scene: Scene): Promise<string> {
    this.stopBlockingRender(true);  // a scene is being replaced; the old render can go
    await this.ready;
    const id = ++this.nextId;
    return new Promise((resolve) => {
      this.validations.set(id, resolve);
      this.send({ type: "validate", id, scene: JSON.stringify(scene) });
    });
  }
}

class WasmRenderClient implements RenderClientLike {
  private current = 0;  // id of the job whose output we accept
  private seq = 0;
  private shown = 0;    // seq of the frame on screen (bitmaps decode out of order)

  constructor(
    private host: WorkerHost,
    private onStatus: (s: RenderStatus) => void,
    private onImage: (img: FrameImage, info: FrameInfo) => void,
  ) {}

  async start(scene: Scene, preview: boolean): Promise<void> {
    this.onStatus({ state: "starting", preview });
    let last: FrameInfo | undefined;
    // Previews (camera drags) show every sample as soon as it exists.
    const job = this.host.render(scene, preview ? 1 : 0, {
      frame: (meta, pixels) => {
        if (job !== this.current) return;
        const info: FrameInfo = { ...meta, job_id: job, seq: ++this.seq };
        last = info;
        this.onStatus({ state: "rendering", preview, frame: info });
        const data = new ImageData(new Uint8ClampedArray(pixels.buffer, pixels.byteOffset, pixels.byteLength), meta.width, meta.height);
        createImageBitmap(data).then((bmp) => {
          if (job === this.current && info.seq > this.shown) {
            this.shown = info.seq;
            this.onImage(bmp, info);
          } else {
            bmp.close();
          }
        });
      },
      done: (stats) => {
        if (job !== this.current) return;
        this.onStatus(stats.cancelled ? { state: "cancelled", frame: last } : { state: "done", preview, frame: last, stats });
      },
      error: (message) => {
        if (job === this.current) this.onStatus({ state: "error", message });
      },
    });
    this.current = job;
  }

  async cancel(): Promise<void> {
    this.host.cancel(this.current);
  }

  dispose() {}  // the worker is shared by all clients of the engine
}

// `base` is the URL of the folder holding rt-mt.* and rt-st.* (public/wasm).
export function wasmEngine(base: string): Engine {
  let host: WorkerHost | null = null;
  const getHost = () => (host ??= new WorkerHost(base));
  return {
    listScenes: async () => Object.keys(examples).map(exampleName).sort(),
    fetchScene: async (name) => {
      const key = Object.keys(examples).find((k) => exampleName(k) === name);
      if (!key) throw new Error(`no example scene called ${name}`);
      return examples[key]();
    },
    validateScene: async (scene) => {
      const error = await getHost().validate(scene);
      return error ? { ok: false, error } : { ok: true };
    },
    createClient: (onStatus, onImage) => new WasmRenderClient(getHost(), onStatus, onImage),
    describe: async () => {
      const r = await getHost().ready;
      return r.threaded
        ? `C++ → WebAssembly · ${r.threads} threads`
        : "C++ → WebAssembly · 1 thread (page is not cross-origin isolated)";
    },
    unavailable: "The example scenes could not be loaded.",
  };
}
