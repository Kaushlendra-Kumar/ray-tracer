import { describe, expect, it } from "vitest";
import {
  blankScene, dolly, duplicateObject, hexToVec, length, normalizeScene, orbit, pan, previewScene, renameMaterial,
  sub, uniqueName, vecToHex,
} from "./scene";
import type { Camera } from "./types";

const cam: Camera = { look_from: [0, 1, 5], look_at: [0, 1, 0], vfov: 40 };

describe("camera navigation", () => {
  it("orbit preserves distance to the target", () => {
    const c = orbit(cam, 0.7, 0.3);
    expect(length(sub(c.look_from, c.look_at))).toBeCloseTo(5, 9);
    expect(c.look_at).toEqual(cam.look_at);
  });
  it("orbit clamps elevation short of the pole", () => {
    const c = orbit(cam, 0, 10);
    const off = sub(c.look_from, c.look_at);
    expect(off[1] / length(off)).toBeLessThan(Math.sin((89.5 * Math.PI) / 180));
  });
  it("pan moves eye and target together", () => {
    const c = pan(cam, 100, 0, 500);
    expect(sub(c.look_from, c.look_at)).toEqual(sub(cam.look_from, cam.look_at));
    expect(c.look_at[0]).toBeLessThan(0); // dragging right moves the scene right => camera left
  });
  it("dolly scales distance and keeps direction", () => {
    const c = dolly(cam, 2);
    expect(length(sub(c.look_from, c.look_at))).toBeCloseTo(10, 9);
    expect(dolly(cam, 1e-9).look_from[2]).toBeGreaterThan(0); // never crosses the target
  });
});

describe("colour conversion", () => {
  it("round-trips through sRGB hex", () => {
    for (const hex of ["#000000", "#ffffff", "#7f7f7f", "#3a9fd2"]) expect(vecToHex(hexToVec(hex))).toBe(hex);
  });
  it("treats albedo as linear", () => {
    expect(hexToVec("#bcbcbc")[0]).toBeCloseTo(0.5, 2); // sRGB 188 ≈ linear 0.5
  });
});

describe("scene structure", () => {
  it("generates unique names", () => {
    expect(uniqueName("sphere", ["sphere", "sphere 2"])).toBe("sphere 3");
    expect(uniqueName("box", [])).toBe("box");
  });
  it("duplicates an object next to the original with a new name", () => {
    const s = blankScene();
    const d = duplicateObject(s, 1);
    expect(d.objects).toHaveLength(3);
    expect(d.objects[2].name).toBe("sphere 2");
    expect(s.objects).toHaveLength(2); // input untouched
  });
  it("renaming a material updates the objects that use it", () => {
    const s = renameMaterial(blankScene(), "clay", "terracotta");
    expect(s.materials.terracotta).toBeDefined();
    expect(s.materials.clay).toBeUndefined();
    expect(s.objects.find((o) => o.name === "sphere")!.material).toBe("terracotta");
    expect(renameMaterial(s, "terracotta", "ground")).toBe(s); // refuses to clobber
  });
  it("normalizes partial scenes with renderer defaults", () => {
    const s = normalizeScene({ objects: [] });
    expect(s.render.spp).toBe(64);
    expect(s.camera.vfov).toBe(40);
  });
  it("preview settings are cheap but keep the aspect ratio", () => {
    const s = blankScene();
    s.render = { ...s.render, width: 1280, height: 720, spp: 1024 };
    const p = previewScene(s);
    expect(p.render.spp).toBe(2);
    expect(p.render.width / p.render.height).toBeCloseTo(1280 / 720, 1);
    expect(Math.max(p.render.width, p.render.height)).toBeLessThanOrEqual(240);
  });
});
