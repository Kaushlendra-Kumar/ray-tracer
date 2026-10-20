import type { Scene } from "./types";

export interface FrameInfo {
  job_id: number;
  seq: number;
  pass: number;
  spp_done: number;
  spp_target: number;
  elapsed_ms: number;
  rays: number;
  width: number;
  height: number;
  finished: boolean;
  cancelled: boolean;
}

// A decoded frame ready to draw: an <img> from the Go backend's PNG, or an
// ImageBitmap built from the WebAssembly renderer's pixels.
export type FrameImage = HTMLImageElement | ImageBitmap;

export type RenderStatus =
  | { state: "idle" }
  | { state: "starting"; preview: boolean }
  | { state: "rendering"; preview: boolean; frame?: FrameInfo }
  | { state: "done"; preview: boolean; frame?: FrameInfo; stats?: Record<string, number | boolean> }
  | { state: "cancelled"; frame?: FrameInfo }
  | { state: "error"; message: string };

export async function listScenes(): Promise<string[]> {
  const r = await fetch("/api/scenes");
  if (!r.ok) throw new Error(`could not list scenes (${r.status})`);
  return r.json();
}

export async function fetchScene(name: string): Promise<unknown> {
  const r = await fetch(`/api/scenes/${encodeURIComponent(name)}`);
  if (!r.ok) throw new Error(`could not load scene ${name} (${r.status})`);
  return r.json();
}

export async function validateScene(scene: Scene): Promise<{ ok: boolean; error?: string }> {
  const r = await fetch("/api/validate", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ scene }),
  });
  return r.json();
}

export interface RenderClientLike {
  start(scene: Scene, preview: boolean): Promise<void>;
  cancel(): Promise<void>;
  dispose(): void;
}

// Where scenes come from and where they are rendered. Two implementations:
// `serverEngine` (the Go backend running the native `rt` binary) and the
// in-browser WebAssembly engine in wasm/engine.ts.
export interface Engine {
  listScenes(): Promise<string[]>;
  fetchScene(name: string): Promise<unknown>;
  validateScene(scene: Scene): Promise<{ ok: boolean; error?: string }>;
  createClient(onStatus: (s: RenderStatus) => void, onImage: (img: FrameImage, info: FrameInfo) => void): RenderClientLike;
  describe(): Promise<string>;  // short label for the toolbar
  unavailable: string;          // shown when listScenes fails
}

// Drives one render at a time. Every start() supersedes the previous job:
// the old job is cancelled on the server and, because events and frames are
// tagged with job ids, anything that still arrives from it is ignored. This
// is what prevents a slow, stale render from overwriting a newer one.
export class RenderClient implements RenderClientLike {
  private current = 0;      // id of the job whose output we accept
  private generation = 0;   // bumps on every start(), even before the id is known
  private source: EventSource | null = null;

  constructor(
    private onStatus: (s: RenderStatus) => void,
    private onImage: (img: FrameImage, info: FrameInfo) => void,
  ) {}

  async start(scene: Scene, preview: boolean): Promise<void> {
    const gen = ++this.generation;
    this.closeStream();
    this.onStatus({ state: "starting", preview });
    let jobId: number;
    try {
      const r = await fetch("/api/render", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ scene, preview }),
      });
      const body = await r.json();
      if (!r.ok) throw new Error(body.error ?? `render request failed (${r.status})`);
      jobId = body.job_id;
    } catch (e) {
      if (gen === this.generation) this.onStatus({ state: "error", message: String((e as Error).message ?? e) });
      return;
    }
    if (gen !== this.generation) return; // superseded while the request was in flight
    this.current = jobId;
    const es = new EventSource(`/api/render/${jobId}/events`);
    this.source = es;
    let lastFrame: FrameInfo | undefined;

    es.addEventListener("frame", (ev) => {
      const info = JSON.parse((ev as MessageEvent).data) as FrameInfo;
      if (info.job_id !== this.current) return;
      lastFrame = info;
      this.onStatus({ state: "rendering", preview, frame: info });
      const img = new Image();
      img.onload = () => {
        if (info.job_id === this.current) this.onImage(img, info);
      };
      img.src = `/api/render/${jobId}/frame?seq=${info.seq}`;
    });
    es.addEventListener("done", (ev) => {
      const d = JSON.parse((ev as MessageEvent).data);
      es.close();
      if (d.job_id !== this.current) return;
      if (d.state === "cancelled") this.onStatus({ state: "cancelled", frame: lastFrame });
      else this.onStatus({ state: "done", preview, frame: lastFrame, stats: d.stats });
    });
    es.addEventListener("error", (ev) => {
      const data = (ev as MessageEvent).data;
      es.close();
      if (jobId !== this.current) return;
      if (data) {
        const d = JSON.parse(data);
        this.onStatus({ state: "error", message: d.message });
      } else if (es.readyState === EventSource.CLOSED) {
        this.onStatus({ state: "error", message: "lost connection to the editor backend" });
      }
    });
  }

  async cancel(): Promise<void> {
    this.generation++;
    const id = this.current;
    if (!id) return;
    await fetch(`/api/render/${id}/cancel`, { method: "POST" }).catch(() => undefined);
  }

  private closeStream() {
    this.source?.close();
    this.source = null;
  }

  dispose() {
    this.closeStream();
  }
}

export const serverEngine: Engine = {
  listScenes,
  fetchScene,
  validateScene,
  createClient: (onStatus, onImage) => new RenderClient(onStatus, onImage),
  describe: async () => {
    const r = await fetch("/api/health");
    const h = await r.json();
    return h.renderer_found ? "Native renderer via Go backend" : "Go backend: renderer binary not built";
  },
  unavailable: "The editor backend is not reachable. Start it with `go run .` in web/backend.",
};
