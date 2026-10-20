// Messages between the page (engine.ts) and the render worker (worker.ts).
import type { FrameInfo } from "../api";

export type FrameMeta = Omit<FrameInfo, "job_id" | "seq">;
export type DoneStats = Record<string, number | boolean>;

export type ToWorker =
  | { type: "init"; base: string }                       // base URL of rt-mt/rt-st .mjs + .wasm
  | { type: "validate"; id: number; scene: string }
  | { type: "start"; job: number; scene: string; maxPass: number }
  | { type: "cancel" };

export type FromWorker =
  | { type: "ready"; threaded: boolean; threads: number }
  | { type: "failed"; message: string }                  // the module could not be loaded
  | { type: "validated"; id: number; error: string }
  | { type: "frame"; job: number; pixels: Uint8Array; info: FrameMeta }
  | { type: "done"; job: number; stats: DoneStats }
  | { type: "error"; job: number; message: string };
