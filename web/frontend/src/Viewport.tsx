import { useEffect, useRef } from "react";
import type { FrameImage, FrameInfo, RenderStatus } from "./api";
import { dolly, orbit, pan } from "./scene";
import type { Camera } from "./types";

// Displays the frames produced by the C++ renderer on a canvas and turns
// pointer gestures into camera edits:
//   drag            orbit around the target
//   shift/right drag  pan
//   wheel           dolly in/out
export function Viewport(props: {
  image: FrameImage | null;
  frame: FrameInfo | null;
  status: RenderStatus;
  camera: Camera;
  aspect: number;
  onCameraPreview: (c: Camera) => void;  // during the gesture (low-res renders)
  onCameraCommit: (c: Camera) => void;   // gesture finished (full render)
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const drag = useRef<{ x: number; y: number; mode: "orbit" | "pan"; cam: Camera } | null>(null);
  const camRef = useRef(props.camera);
  camRef.current = props.camera;
  const wheelTimer = useRef<number | undefined>(undefined);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !props.image) return;
    canvas.width = props.image.width;
    canvas.height = props.image.height;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    // Preview frames are tiny; keep them crisp-but-smooth when scaled up.
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(props.image, 0, 0);
  }, [props.image]);

  const onPointerDown = (e: React.PointerEvent) => {
    (e.target as Element).setPointerCapture(e.pointerId);
    drag.current = { x: e.clientX, y: e.clientY, mode: e.shiftKey || e.button === 2 ? "pan" : "orbit", cam: camRef.current };
  };
  const onPointerMove = (e: React.PointerEvent) => {
    const d = drag.current;
    if (!d) return;
    const dx = e.clientX - d.x, dy = e.clientY - d.y;
    const h = canvasRef.current?.getBoundingClientRect().height ?? 400;
    const next = d.mode === "orbit" ? orbit(d.cam, -dx * 0.008, dy * 0.008) : pan(d.cam, dx, dy, h);
    props.onCameraPreview(next);
  };
  const onPointerUp = () => {
    if (!drag.current) return;
    drag.current = null;
    props.onCameraCommit(camRef.current);
  };
  const onWheel = (e: React.WheelEvent) => {
    const next = dolly(camRef.current, Math.exp(e.deltaY * 0.0015));
    props.onCameraPreview(next);
    window.clearTimeout(wheelTimer.current);
    wheelTimer.current = window.setTimeout(() => props.onCameraCommit(camRef.current), 350);
  };

  const s = props.status;
  // Prefer the newest frame metadata from the event stream; the displayed
  // image may lag behind by one decode.
  const f = ("frame" in s && s.frame) || props.frame;
  const progress = f ? Math.min(1, f.spp_done / Math.max(1, f.spp_target)) : 0;
  const preview = (s.state === "rendering" || s.state === "starting" || s.state === "done") && s.preview;

  return (
    <section className="viewport" aria-label="Render viewport">
      <div className="stage">
        <div className="frame" style={{ "--aspect": String(props.aspect) } as React.CSSProperties}>
          <canvas
            ref={canvasRef}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerCancel={onPointerUp}
            onWheel={onWheel}
            onContextMenu={(e) => e.preventDefault()}
            className={props.image ? "" : "blank"}
          />
          {!props.image && s.state !== "error" && <p className="placeholder">Press Render to see the scene.</p>}
          {s.state === "error" && (
            <div className="error" role="alert">
              <strong>The renderer rejected this scene.</strong>
              <p>{s.message}</p>
            </div>
          )}
        </div>
      </div>
      <div className="spectrum" aria-hidden>
        <div className="fill" style={{ width: `${progress * 100}%` }} />
      </div>
      <p className="readout" aria-live="polite">
        {statusText(s, f, preview)}
      </p>
    </section>
  );
}

function statusText(s: RenderStatus, f: FrameInfo | null, preview: boolean): string {
  const frame = f
    ? `${f.spp_done} of ${f.spp_target} samples per pixel, ${f.width}×${f.height}, ${(f.elapsed_ms / 1000).toFixed(1)} s, ` +
      `${(f.rays / Math.max(1, f.elapsed_ms) / 1000).toFixed(1)} M rays/s`
    : "";
  switch (s.state) {
    case "idle": return "Ready. Drag to orbit, shift-drag to pan, scroll to zoom.";
    case "starting": return preview ? "Starting preview…" : "Starting render…";
    case "rendering": return (preview ? "Preview: " : "Rendering: ") + frame;
    case "done": return (preview ? "Preview finished: " : "Finished: ") + frame;
    case "cancelled": return "Cancelled. " + frame;
    case "error": return "Render failed. Fix the scene and render again.";
  }
}
