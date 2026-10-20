// Mirrors the scene JSON format understood by the C++ renderer
// (src/io/scene_io.cpp). The browser only edits descriptions; it never renders.
export type Vec3 = [number, number, number];

export interface Camera {
  look_from: Vec3;
  look_at: Vec3;
  vup?: Vec3;
  vfov: number;
  aperture?: number;
  focus_dist?: number;
}

export type Integrator = "path" | "direct" | "normals";
export type ToneMap = "clamp" | "reinhard" | "aces";

export interface RenderSettings {
  width: number;
  height: number;
  spp: number;
  max_depth: number;
  rr_min_depth?: number;
  integrator: Integrator;
  light_sampling?: boolean;
  seed?: number;
  tone_map: ToneMap;
  exposure: number;
  threads?: number;
  tile_size?: number;
  use_bvh?: boolean;
}

export interface Background {
  type: "gradient" | "solid";
  top?: Vec3;
  bottom?: Vec3;
  color?: Vec3;
  intensity?: number;
}

export type Material =
  | { type: "lambertian"; albedo: Vec3; albedo2?: Vec3; checker_scale?: number }
  | { type: "metal"; albedo: Vec3; roughness: number }
  | { type: "dielectric"; ior: number; tint?: Vec3 }
  | { type: "emissive"; color: Vec3; intensity: number; two_sided?: boolean };

export type MaterialType = Material["type"];

interface Base {
  name: string;
  material: string;
}

export type SceneObject =
  | (Base & { type: "sphere"; center: Vec3; radius: number })
  | (Base & { type: "plane"; point: Vec3; normal: Vec3 })
  | (Base & { type: "triangle"; v0: Vec3; v1: Vec3; v2: Vec3 })
  | (Base & { type: "quad"; corner: Vec3; u: Vec3; v: Vec3 })
  | (Base & { type: "box"; min: Vec3; max: Vec3; rotate_y?: number })
  | (Base & {
      type: "mesh";
      source: "icosphere" | "torus" | "obj";
      subdivisions?: number;
      major_radius?: number;
      minor_radius?: number;
      segments?: number;
      path?: string;
      smooth?: boolean;
      scale?: Vec3;
      rotate_y?: number;
      translate?: Vec3;
    });

export type ObjectType = SceneObject["type"];

export interface PointLight {
  type: "point";
  name: string;
  position: Vec3;
  intensity: Vec3;
}

export interface Scene {
  name: string;
  description?: string;
  camera: Camera;
  render: RenderSettings;
  background: Background;
  materials: Record<string, Material>;
  objects: SceneObject[];
  lights: PointLight[];
}

export type Selection =
  | { kind: "object"; index: number }
  | { kind: "light"; index: number }
  | { kind: "material"; name: string }
  | { kind: "camera" }
  | null;
