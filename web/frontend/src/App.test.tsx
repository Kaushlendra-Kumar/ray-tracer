// @vitest-environment jsdom
// UI behaviour test: renders the real App in jsdom with the backend mocked.
import { act, cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { serverEngine } from "./api";
import { App } from "./App";

class FakeEventSource {
  static instances: FakeEventSource[] = [];
  listeners: Record<string, ((e: { data: string }) => void)[]> = {};
  readyState = 1;
  constructor(public url: string) { FakeEventSource.instances.push(this); }
  addEventListener(type: string, fn: (e: { data: string }) => void) { (this.listeners[type] ??= []).push(fn); }
  emit(type: string, data: unknown) { this.listeners[type]?.forEach((fn) => fn({ data: JSON.stringify(data) })); }
  close() { this.readyState = 2; }
  static CLOSED = 2;
}

let renderBodies: { scene: { objects: unknown[]; render: { spp: number } }; preview: boolean }[] = [];
let nextJob = 0;

beforeEach(() => {
  renderBodies = [];
  nextJob = 0;
  FakeEventSource.instances = [];
  vi.stubGlobal("EventSource", FakeEventSource);
  vi.stubGlobal("fetch", vi.fn(async (url: string, init?: RequestInit) => {
    const json = (v: unknown, status = 200) => ({ ok: status < 400, status, json: async () => v });
    if (url === "/api/scenes") return json(["cornell_box"]);
    if (url === "/api/validate") return json({ ok: true });
    if (url === "/api/render") {
      renderBodies.push(JSON.parse(String(init?.body)));
      return json({ job_id: ++nextJob }, 202);
    }
    if (url.endsWith("/cancel")) return json({});
    return json({ error: "not found" }, 404);
  }));
});
afterEach(() => { cleanup(); vi.unstubAllGlobals(); vi.useRealTimers(); });

const flush = () => act(async () => { await new Promise((r) => setTimeout(r, 0)); });

describe("scene editor", () => {
  it("lists the starter scene and adds, edits and deletes objects", async () => {
    render(<App engine={serverEngine} />);
    const nav = screen.getByRole("navigation", { name: "Scene contents" });
    expect(within(nav).getByRole("button", { name: /^ground ground$/ })).toBeTruthy();  // object "ground" using material "ground"
    expect(within(nav).getByRole("button", { name: /^sphere clay$/ })).toBeTruthy();

    fireEvent.click(within(nav).getByText("+ Box"));
    expect(within(nav).getByText("box")).toBeTruthy();
    // The new box is selected and editable in the inspector.
    const inspector = screen.getByRole("complementary");
    expect(within(inspector).getByText("Box")).toBeTruthy();
    fireEvent.change(within(inspector).getByLabelText("Rotate Y (°)"), { target: { value: "45" } });
    expect((within(inspector).getByLabelText("Rotate Y (°)") as HTMLInputElement).value).toBe("45");

    fireEvent.click(within(nav).getByText("Duplicate"));
    expect(within(nav).getByText("box 2")).toBeTruthy();
    fireEvent.click(within(nav).getByText("Delete"));
    expect(within(nav).queryByText("box 2")).toBeNull();
  });

  it("refuses to delete a material that is in use", async () => {
    render(<App engine={serverEngine} />);
    const nav = screen.getByRole("navigation", { name: "Scene contents" });
    fireEvent.click(within(nav).getByRole("button", { name: /^clay lambertian$/ }));
    fireEvent.click(within(nav).getByText("Delete"));
    expect(screen.getByRole("status").textContent).toMatch(/still used by sphere/);
    expect(within(nav).getByRole("button", { name: /^clay lambertian$/ })).toBeTruthy();
  });

  it("starts a render and ignores frames from superseded jobs", async () => {
    render(<App engine={serverEngine} />);
    fireEvent.click(screen.getByText("Render"));
    await flush();
    expect(renderBodies).toHaveLength(1);
    fireEvent.click(screen.getByText("Cancel")); // button flips while rendering
    await flush();
    act(() => FakeEventSource.instances[0].emit("done", { job_id: 1, state: "cancelled", stats: {} }));
    expect(screen.getByText(/^Cancelled/)).toBeTruthy();
    fireEvent.click(screen.getByText("Render"));
    await flush();
    expect(renderBodies).toHaveLength(2);
    const [oldEs, newEs] = FakeEventSource.instances;
    const frame = (job: number, spp: number) => ({ job_id: job, seq: 1, pass: 1, spp_done: spp, spp_target: 64,
      elapsed_ms: 100, rays: 1e5, width: 640, height: 360, finished: false, cancelled: false });
    act(() => oldEs.emit("frame", frame(1, 50)));
    expect(screen.getByText(/^Starting render/)).toBeTruthy(); // stale job ignored
    act(() => newEs.emit("frame", frame(2, 3)));
    expect(screen.getByText(/3 of 64 samples per pixel/)).toBeTruthy();
    act(() => newEs.emit("done", { job_id: 2, state: "done", stats: {} }));
    expect(screen.getByText(/^Finished/)).toBeTruthy();
  });

  it("shows renderer errors in the viewport", async () => {
    render(<App engine={serverEngine} />);
    fireEvent.click(screen.getByText("Render"));
    await flush();
    act(() => FakeEventSource.instances[0].emit("error", { job_id: 1, message: "objects[1].radius: must be > 0" }));
    expect(screen.getByRole("alert").textContent).toContain("objects[1].radius");
  });

  it("re-renders automatically after an edit", async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true });
    render(<App engine={serverEngine} />);
    const nav = screen.getByRole("navigation", { name: "Scene contents" });
    fireEvent.click(within(nav).getByText("+ Sphere"));
    await act(async () => { vi.advanceTimersByTime(400); });
    await flush();
    expect(renderBodies.length).toBe(1);
    expect(renderBodies[0].scene.objects).toHaveLength(3);
    expect(renderBodies[0].preview).toBe(false);
  });
});
