// Pure scene-editing helpers (unit tested in scene.test.ts).
import type { Camera, Material, MaterialType, ObjectType, PointLight, Scene, SceneObject, Vec3 } from "./types";

export const add = (a: Vec3, b: Vec3): Vec3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const sub = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const scale = (a: Vec3, s: number): Vec3 => [a[0] * s, a[1] * s, a[2] * s];
export const dot = (a: Vec3, b: Vec3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
export const cross = (a: Vec3, b: Vec3): Vec3 => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
export const length = (a: Vec3) => Math.sqrt(dot(a, a));
export const normalize = (a: Vec3): Vec3 => {
  const l = length(a);
  return l > 1e-12 ? scale(a, 1 / l) : [0, 0, 0];
};

// --- Camera navigation -----------------------------------------------------
// Orbit keeps the target fixed and moves the eye on a sphere around it, using
// world +Y as the up axis. Elevation is clamped short of the poles so the view
// basis never degenerates.
export function orbit(cam: Camera, dAzimuth: number, dElevation: number): Camera {
  const offset = sub(cam.look_from, cam.look_at);
  const r = length(offset);
  if (r < 1e-9) return cam;
  let az = Math.atan2(offset[0], offset[2]);
  let el = Math.asin(Math.max(-1, Math.min(1, offset[1] / r)));
  az += dAzimuth;
  const limit = (89 * Math.PI) / 180;
  el = Math.max(-limit, Math.min(limit, el + dElevation));
  const next: Vec3 = [r * Math.cos(el) * Math.sin(az), r * Math.sin(el), r * Math.cos(el) * Math.cos(az)];
  return { ...cam, look_from: add(cam.look_at, next) };
}

// Pan moves eye and target together in the image plane; dx/dy are in pixels.
export function pan(cam: Camera, dxPx: number, dyPx: number, viewHeightPx: number): Camera {
  const view = sub(cam.look_at, cam.look_from);
  const dist = length(view);
  const w = normalize(scale(view, -1));
  const right = normalize(cross([0, 1, 0], w));
  const up = cross(w, right);
  const worldPerPx = (2 * dist * Math.tan(((cam.vfov ?? 40) * Math.PI) / 360)) / Math.max(1, viewHeightPx);
  const delta = add(scale(right, -dxPx * worldPerPx), scale(up, dyPx * worldPerPx));
  return { ...cam, look_from: add(cam.look_from, delta), look_at: add(cam.look_at, delta) };
}

// Zoom scales the eye-target distance (factor > 1 moves away).
export function dolly(cam: Camera, factor: number): Camera {
  const offset = sub(cam.look_from, cam.look_at);
  const r = length(offset);
  const nr = Math.max(0.05, Math.min(1e4, r * factor));
  return { ...cam, look_from: add(cam.look_at, scale(offset, nr / Math.max(r, 1e-9))) };
}

// --- Colour ------------------------------------------------------------------
// Albedos are *linear* reflectances; the colour picker works in sRGB.
export function linearToSrgb(v: number): number {
  const c = Math.max(0, Math.min(1, v));
  return c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055;
}
export function srgbToLinear(v: number): number {
  return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}
export function vecToHex(v: Vec3): string {
  return (
    "#" +
    v
      .map((c) => Math.round(linearToSrgb(c) * 255).toString(16).padStart(2, "0"))
      .join("")
  );
}
export function hexToVec(hex: string): Vec3 {
  const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex);
  if (!m) return [0.5, 0.5, 0.5];
  return [1, 2, 3].map((i) => +srgbToLinear(parseInt(m[i], 16) / 255).toFixed(4)) as Vec3;
}

// --- Naming / structure ---------------------------------------------------------
export function uniqueName(base: string, taken: Iterable<string>): string {
  const set = new Set(taken);
  if (!set.has(base)) return base;
  const stem = base.replace(/ \d+$/, "");
  for (let i = 2; ; i++) if (!set.has(`${stem} ${i}`)) return `${stem} ${i}`;
}

export function defaultMaterial(type: MaterialType): Material {
  switch (type) {
    case "lambertian":
      return { type, albedo: [0.7, 0.7, 0.7] };
    case "metal":
      return { type, albedo: [0.9, 0.9, 0.9], roughness: 0.05 };
    case "dielectric":
      return { type, ior: 1.5, tint: [1, 1, 1] };
    case "emissive":
      return { type, color: [1, 0.9, 0.8], intensity: 5 };
  }
}

// New objects are placed in front of the camera target so they are visible.
export function defaultObject(type: ObjectType, scene: Scene, material: string): SceneObject {
  const at = scene.camera.look_at;
  const name = uniqueName(type, scene.objects.map((o) => o.name));
  const base = { name, material };
  switch (type) {
    case "sphere":
      return { ...base, type, center: [...at] as Vec3, radius: 0.5 };
    case "plane":
      return { ...base, type, point: [0, 0, 0], normal: [0, 1, 0] };
    case "triangle":
      return { ...base, type, v0: add(at, [-0.5, 0, 0]), v1: add(at, [0.5, 0, 0]), v2: add(at, [0, 0.8, 0]) };
    case "quad":
      return { ...base, type, corner: add(at, [-0.5, 0, -0.5]), u: [1, 0, 0], v: [0, 0, 1] };
    case "box":
      return { ...base, type, min: add(at, [-0.4, -0.4, -0.4]), max: add(at, [0.4, 0.4, 0.4]), rotate_y: 0 };
    case "mesh":
      return { ...base, type, source: "torus", major_radius: 0.6, minor_radius: 0.2, segments: 48, smooth: true,
               scale: [1, 1, 1], rotate_y: 0, translate: [...at] as Vec3 };
  }
}

export function defaultLight(scene: Scene): PointLight {
  return {
    type: "point",
    name: uniqueName("light", scene.lights.map((l) => l.name)),
    position: add(scene.camera.look_at, [2, 4, 2]),
    intensity: [30, 30, 30],
  };
}

export function duplicateObject(scene: Scene, index: number): Scene {
  const src = scene.objects[index];
  const copy: SceneObject = structuredClone(src);
  copy.name = uniqueName(src.name, scene.objects.map((o) => o.name));
  // Offset the copy so it does not sit exactly on top of the original.
  const shift: Vec3 = [0.3, 0, 0.3];
  switch (copy.type) {
    case "sphere": copy.center = add(copy.center, shift); break;
    case "triangle": copy.v0 = add(copy.v0, shift); copy.v1 = add(copy.v1, shift); copy.v2 = add(copy.v2, shift); break;
    case "quad": copy.corner = add(copy.corner, shift); break;
    case "box": copy.min = add(copy.min, shift); copy.max = add(copy.max, shift); break;
    case "mesh": copy.translate = add(copy.translate ?? [0, 0, 0], shift); break;
    case "plane": break;
  }
  const objects = [...scene.objects];
  objects.splice(index + 1, 0, copy);
  return { ...scene, objects };
}

export function materialUsers(scene: Scene, name: string): string[] {
  return scene.objects.filter((o) => o.material === name).map((o) => o.name);
}

export function renameMaterial(scene: Scene, from: string, to: string): Scene {
  if (from === to || !to || scene.materials[to]) return scene;
  const materials: Record<string, Material> = {};
  for (const [k, v] of Object.entries(scene.materials)) materials[k === from ? to : k] = v;
  return {
    ...scene,
    materials,
    objects: scene.objects.map((o) => (o.material === from ? { ...o, material: to } : o)),
  };
}

// Fills in any fields the C++ side defaults, so the editor can rely on them.
export function normalizeScene(raw: unknown): Scene {
  const s = (raw ?? {}) as Partial<Scene>;
  return {
    name: s.name ?? "untitled",
    description: s.description,
    camera: { look_from: [0, 1, 4], look_at: [0, 0.5, 0], vfov: 40, ...(s.camera ?? {}) },
    render: {
      width: 640, height: 360, spp: 64, max_depth: 8, integrator: "path", tone_map: "aces", exposure: 1,
      ...(s.render ?? {}),
    },
    background: s.background ?? { type: "gradient", top: [0.5, 0.7, 1], bottom: [1, 1, 1] },
    materials: s.materials ?? { default: defaultMaterial("lambertian") },
    objects: s.objects ?? [],
    lights: s.lights ?? [],
  };
}

// Quick, low-cost settings used while the camera is being dragged.
export function previewScene(scene: Scene): Scene {
  const r = scene.render;
  const f = Math.min(1, 240 / Math.max(r.width, r.height));
  return {
    ...scene,
    render: {
      ...r,
      width: Math.max(16, Math.round(r.width * f)),
      height: Math.max(16, Math.round(r.height * f)),
      spp: 2,
      max_depth: Math.min(r.max_depth, 4),
    },
  };
}

export function blankScene(): Scene {
  return normalizeScene({
    name: "New scene",
    materials: {
      ground: { type: "lambertian", albedo: [0.75, 0.75, 0.75] },
      clay: { type: "lambertian", albedo: [0.8, 0.45, 0.3] },
    },
    objects: [
      { type: "plane", name: "ground", point: [0, 0, 0], normal: [0, 1, 0], material: "ground" },
      { type: "sphere", name: "sphere", center: [0, 0.5, 0], radius: 0.5, material: "clay" },
    ],
    lights: [{ type: "point", name: "key", position: [3, 5, 3], intensity: [40, 40, 40] }],
  });
}
