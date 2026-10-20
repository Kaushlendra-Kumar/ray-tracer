import { CheckField, ColorField, NumberField, SelectField, TextField, Vec3Field } from "./fields";
import { defaultMaterial, materialUsers, renameMaterial, uniqueName } from "./scene";
import type { Material, MaterialType, ObjectType, PointLight, Scene, SceneObject, Selection, Vec3 } from "./types";

type SetScene = (fn: (s: Scene) => Scene) => void;

const OBJECT_TYPES: { type: ObjectType; label: string }[] = [
  { type: "sphere", label: "Sphere" },
  { type: "plane", label: "Plane" },
  { type: "quad", label: "Quad" },
  { type: "box", label: "Box" },
  { type: "triangle", label: "Triangle" },
  { type: "mesh", label: "Mesh" },
];

export function Hierarchy(props: {
  scene: Scene;
  selection: Selection;
  select: (s: Selection) => void;
  addObject: (t: ObjectType) => void;
  addLight: () => void;
  addMaterial: () => void;
  duplicate: () => void;
  remove: () => void;
}) {
  const { scene, selection, select } = props;
  const isSel = (s: Selection) => JSON.stringify(s) === JSON.stringify(selection);
  const canEdit = selection?.kind === "object" || selection?.kind === "light" || selection?.kind === "material";
  return (
    <nav className="hierarchy" aria-label="Scene contents">
      <div className="group">
        <h2>Objects</h2>
        <ul>
          {scene.objects.map((o, i) => (
            <li key={i}>
              <button className={isSel({ kind: "object", index: i }) ? "row selected" : "row"} onClick={() => select({ kind: "object", index: i })}>
                <span className={`glyph ${o.type}`} aria-hidden />
                <span className="name">{o.name}</span>
                <span className="meta">{o.material}</span>
              </button>
            </li>
          ))}
          {scene.objects.length === 0 && <li className="empty">Add a shape below to start building the scene.</li>}
        </ul>
        <div className="adders">
          {OBJECT_TYPES.map((t) => (
            <button key={t.type} onClick={() => props.addObject(t.type)} title={`Add a ${t.label.toLowerCase()}`}>
              + {t.label}
            </button>
          ))}
        </div>
      </div>

      <div className="group">
        <h2>Point lights</h2>
        <ul>
          {scene.lights.map((l, i) => (
            <li key={i}>
              <button className={isSel({ kind: "light", index: i }) ? "row selected" : "row"} onClick={() => select({ kind: "light", index: i })}>
                <span className="glyph light" aria-hidden />
                <span className="name">{l.name}</span>
              </button>
            </li>
          ))}
        </ul>
        <div className="adders">
          <button onClick={props.addLight}>+ Point light</button>
        </div>
      </div>

      <div className="group">
        <h2>Materials</h2>
        <ul>
          {Object.entries(scene.materials).map(([name, m]) => (
            <li key={name}>
              <button className={isSel({ kind: "material", name }) ? "row selected" : "row"} onClick={() => select({ kind: "material", name })}>
                <span className="swatch" style={{ background: swatch(m) }} aria-hidden />
                <span className="name">{name}</span>
                <span className="meta">{m.type}</span>
              </button>
            </li>
          ))}
        </ul>
        <div className="adders">
          <button onClick={props.addMaterial}>+ Material</button>
        </div>
      </div>

      <div className="group">
        <button className={isSel({ kind: "camera" }) ? "row selected" : "row"} onClick={() => select({ kind: "camera" })}>
          <span className="glyph camera" aria-hidden />
          <span className="name">Camera and render settings</span>
        </button>
      </div>

      <div className="edit-actions">
        <button disabled={selection?.kind !== "object"} onClick={props.duplicate}>Duplicate</button>
        <button disabled={!canEdit} onClick={props.remove} className="danger">Delete</button>
      </div>
    </nav>
  );
}

function swatch(m: Material): string {
  const c = (v: Vec3) => `rgb(${v.map((x) => Math.round(Math.min(1, Math.pow(Math.max(0, x), 1 / 2.2)) * 255)).join(",")})`;
  switch (m.type) {
    case "lambertian": return c(m.albedo);
    case "metal": return `linear-gradient(135deg, ${c(m.albedo)}, #fff 45%, ${c(m.albedo)})`;
    case "dielectric": return `radial-gradient(circle at 35% 30%, #fff, ${c(m.tint ?? [0.8, 0.9, 1])} 60%, #9fb4c8)`;
    case "emissive": return `radial-gradient(circle, #fff, ${c(m.color)})`;
  }
}

// ---------------------------------------------------------------------------
export function Inspector(props: { scene: Scene; selection: Selection; setScene: SetScene; select: (s: Selection) => void }) {
  const { scene, selection, setScene } = props;
  if (!selection) {
    return <aside className="inspector"><p className="empty">Select an object, light or material to edit it. Drag in the viewport to orbit the camera.</p></aside>;
  }
  if (selection.kind === "camera") return <aside className="inspector"><CameraEditor scene={scene} setScene={setScene} /></aside>;
  if (selection.kind === "light") {
    const l = scene.lights[selection.index];
    if (!l) return null;
    const set = (patch: Partial<PointLight>) =>
      setScene((s) => ({ ...s, lights: s.lights.map((x, i) => (i === selection.index ? { ...x, ...patch } : x)) }));
    return (
      <aside className="inspector">
        <h2>Point light</h2>
        <TextField label="Name" value={l.name} onCommit={(name) => set({ name })} />
        <Vec3Field label="Position" value={l.position} onChange={(position) => set({ position })} />
        <Vec3Field label="Intensity (W/sr)" value={l.intensity} step={1} onChange={(intensity) => set({ intensity })} />
        <p className="note">Point lights cast hard shadows. For soft shadows, give a sphere or quad an emissive material.</p>
      </aside>
    );
  }
  if (selection.kind === "material") {
    return <aside className="inspector"><MaterialEditor scene={scene} name={selection.name} setScene={setScene} select={props.select} /></aside>;
  }
  const o = scene.objects[selection.index];
  if (!o) return null;
  return <aside className="inspector"><ObjectEditor scene={scene} index={selection.index} obj={o} setScene={setScene} select={props.select} /></aside>;
}

function ObjectEditor(props: { scene: Scene; index: number; obj: SceneObject; setScene: SetScene; select: (s: Selection) => void }) {
  const { scene, index, obj, setScene } = props;
  const set = (patch: Partial<SceneObject>) =>
    setScene((s) => ({ ...s, objects: s.objects.map((x, i) => (i === index ? ({ ...x, ...patch } as SceneObject) : x)) }));
  const mats = Object.keys(scene.materials);
  return (
    <>
      <h2>{obj.type[0].toUpperCase() + obj.type.slice(1)}</h2>
      <TextField label="Name" value={obj.name} onCommit={(name) => set({ name })} />
      <div className="field">
        <span>Material</span>
        <span className="inline">
          <select value={obj.material} onChange={(e) => set({ material: e.target.value })}>
            {mats.map((m) => <option key={m}>{m}</option>)}
          </select>
          <button className="link" onClick={() => props.select({ kind: "material", name: obj.material })}>Edit</button>
        </span>
      </div>
      {obj.type === "sphere" && (
        <>
          <Vec3Field label="Centre" value={obj.center} onChange={(center) => set({ center })} />
          <NumberField label="Radius" value={obj.radius} min={0.001} step={0.05} onChange={(radius) => set({ radius })} />
        </>
      )}
      {obj.type === "plane" && (
        <>
          <Vec3Field label="Point" value={obj.point} onChange={(point) => set({ point })} />
          <Vec3Field label="Normal" value={obj.normal} onChange={(normal) => set({ normal })} />
        </>
      )}
      {obj.type === "triangle" && (
        <>
          <Vec3Field label="Vertex 0" value={obj.v0} onChange={(v0) => set({ v0 })} />
          <Vec3Field label="Vertex 1" value={obj.v1} onChange={(v1) => set({ v1 })} />
          <Vec3Field label="Vertex 2" value={obj.v2} onChange={(v2) => set({ v2 })} />
        </>
      )}
      {obj.type === "quad" && (
        <>
          <Vec3Field label="Corner" value={obj.corner} onChange={(corner) => set({ corner })} />
          <Vec3Field label="Edge u" value={obj.u} onChange={(u) => set({ u })} />
          <Vec3Field label="Edge v" value={obj.v} onChange={(v) => set({ v })} />
          <p className="note">The lit side of an emissive quad faces along u × v.</p>
        </>
      )}
      {obj.type === "box" && (
        <>
          <Vec3Field label="Min corner" value={obj.min} onChange={(min) => set({ min })} />
          <Vec3Field label="Max corner" value={obj.max} onChange={(max) => set({ max })} />
          <NumberField label="Rotate Y (°)" value={obj.rotate_y ?? 0} step={5} onChange={(rotate_y) => set({ rotate_y })} />
        </>
      )}
      {obj.type === "mesh" && (
        <>
          <SelectField
            label="Source"
            value={obj.source}
            options={[{ value: "icosphere", label: "Icosphere" }, { value: "torus", label: "Torus" }, { value: "obj", label: "OBJ file" }] as const}
            onChange={(source) => set({ source })}
          />
          {obj.source === "icosphere" && (
            <NumberField label="Subdivisions" value={obj.subdivisions ?? 3} integer min={0} max={7} step={1}
              hint={`${20 * 4 ** (obj.subdivisions ?? 3)} triangles`} onChange={(subdivisions) => set({ subdivisions })} />
          )}
          {obj.source === "torus" && (
            <>
              <NumberField label="Major radius" value={obj.major_radius ?? 1} min={0.001} step={0.05} onChange={(major_radius) => set({ major_radius })} />
              <NumberField label="Minor radius" value={obj.minor_radius ?? 0.35} min={0.001} step={0.05} onChange={(minor_radius) => set({ minor_radius })} />
              <NumberField label="Segments" value={obj.segments ?? 48} integer min={3} max={2048} step={4} onChange={(segments) => set({ segments })} />
            </>
          )}
          {obj.source === "obj" && (
            <TextField label="OBJ path" value={obj.path ?? ""} onCommit={(path) => set({ path })} />
          )}
          <CheckField label="Smooth normals" value={obj.smooth ?? true} onChange={(smooth) => set({ smooth })} />
          <Vec3Field label="Translate" value={obj.translate ?? [0, 0, 0]} onChange={(translate) => set({ translate })} />
          <Vec3Field label="Scale" value={obj.scale ?? [1, 1, 1]} onChange={(scale) => set({ scale })} />
          <NumberField label="Rotate Y (°)" value={obj.rotate_y ?? 0} step={5} onChange={(rotate_y) => set({ rotate_y })} />
        </>
      )}
    </>
  );
}

const MATERIAL_TYPES: { value: MaterialType; label: string }[] = [
  { value: "lambertian", label: "Diffuse" },
  { value: "metal", label: "Metal" },
  { value: "dielectric", label: "Glass" },
  { value: "emissive", label: "Light (emissive)" },
];

function MaterialEditor(props: { scene: Scene; name: string; setScene: SetScene; select: (s: Selection) => void }) {
  const { scene, name, setScene } = props;
  const m = scene.materials[name];
  if (!m) return <p className="empty">This material no longer exists.</p>;
  const set = (next: Material) => setScene((s) => ({ ...s, materials: { ...s.materials, [name]: next } }));
  const users = materialUsers(scene, name);
  return (
    <>
      <h2>Material</h2>
      <TextField
        label="Name"
        value={name}
        onCommit={(to) => {
          const target = uniqueName(to, Object.keys(scene.materials).filter((k) => k !== name));
          setScene((s) => renameMaterial(s, name, target));
          props.select({ kind: "material", name: target });
        }}
      />
      <SelectField label="Type" value={m.type} options={MATERIAL_TYPES} onChange={(t) => set(defaultMaterial(t))} />
      {m.type === "lambertian" && (
        <>
          <ColorField label="Albedo" value={m.albedo} onChange={(albedo) => set({ ...m, albedo })} />
          <CheckField
            label="Checkerboard"
            value={(m.checker_scale ?? 0) > 0}
            onChange={(on) => set({ ...m, checker_scale: on ? 1 : 0, albedo2: m.albedo2 ?? [0.2, 0.2, 0.2] })}
          />
          {(m.checker_scale ?? 0) > 0 && (
            <>
              <ColorField label="Second colour" value={m.albedo2 ?? [0.2, 0.2, 0.2]} onChange={(albedo2) => set({ ...m, albedo2 })} />
              <NumberField label="Cells per unit" value={m.checker_scale ?? 1} min={0.01} step={0.25} onChange={(checker_scale) => set({ ...m, checker_scale })} />
            </>
          )}
        </>
      )}
      {m.type === "metal" && (
        <>
          <ColorField label="Reflectance" value={m.albedo} onChange={(albedo) => set({ ...m, albedo })} />
          <NumberField label="Roughness" value={m.roughness} min={0} max={1} step={0.05} onChange={(roughness) => set({ ...m, roughness })} />
        </>
      )}
      {m.type === "dielectric" && (
        <>
          <NumberField label="Index of refraction" value={m.ior} min={1} max={4} step={0.05}
            hint="Water 1.33, glass 1.5, diamond 2.42" onChange={(ior) => set({ ...m, ior })} />
          <ColorField label="Tint" value={m.tint ?? [1, 1, 1]} onChange={(tint) => set({ ...m, tint })} />
        </>
      )}
      {m.type === "emissive" && (
        <>
          <ColorField label="Colour" value={m.color} onChange={(color) => set({ ...m, color })} />
          <NumberField label="Intensity" value={m.intensity} min={0} step={0.5} onChange={(intensity) => set({ ...m, intensity })} />
          <CheckField label="Emit from both sides" value={m.two_sided ?? false} onChange={(two_sided) => set({ ...m, two_sided })} />
        </>
      )}
      <p className="note">{users.length ? `Used by ${users.join(", ")}.` : "Not used by any object yet."}</p>
    </>
  );
}

function CameraEditor(props: { scene: Scene; setScene: SetScene }) {
  const { scene, setScene } = props;
  const cam = scene.camera;
  const r = scene.render;
  const bg = scene.background;
  const setCam = (patch: Partial<Scene["camera"]>) => setScene((s) => ({ ...s, camera: { ...s.camera, ...patch } }));
  const setRender = (patch: Partial<Scene["render"]>) => setScene((s) => ({ ...s, render: { ...s.render, ...patch } }));
  const setBg = (patch: Partial<Scene["background"]>) => setScene((s) => ({ ...s, background: { ...s.background, ...patch } }));
  return (
    <>
      <h2>Camera</h2>
      <Vec3Field label="Position" value={cam.look_from} onChange={(look_from) => setCam({ look_from })} />
      <Vec3Field label="Target" value={cam.look_at} onChange={(look_at) => setCam({ look_at })} />
      <NumberField label="Field of view (°)" value={cam.vfov} min={1} max={170} step={1} onChange={(vfov) => setCam({ vfov })} />
      <NumberField label="Aperture" value={cam.aperture ?? 0} min={0} step={0.01} hint="0 = pinhole (everything sharp)" onChange={(aperture) => setCam({ aperture })} />
      <NumberField label="Focus distance" value={cam.focus_dist ?? 0} min={0} step={0.1} hint="0 = distance to the target" onChange={(focus_dist) => setCam({ focus_dist })} />

      <h2>Render</h2>
      <div className="pair">
        <NumberField label="Width" value={r.width} integer min={16} max={4096} step={16} onChange={(width) => setRender({ width })} />
        <NumberField label="Height" value={r.height} integer min={16} max={4096} step={16} onChange={(height) => setRender({ height })} />
      </div>
      <NumberField label="Samples per pixel" value={r.spp} integer min={1} max={100000} step={16} onChange={(spp) => setRender({ spp })} />
      <NumberField label="Max bounces" value={r.max_depth} integer min={1} max={100} step={1} onChange={(max_depth) => setRender({ max_depth })} />
      <SelectField
        label="Integrator"
        value={r.integrator}
        options={[{ value: "path", label: "Path tracing" }, { value: "direct", label: "Direct lighting" }, { value: "normals", label: "Normals (debug)" }] as const}
        onChange={(integrator) => setRender({ integrator })}
      />
      <CheckField label="Light sampling" value={r.light_sampling ?? true} onChange={(light_sampling) => setRender({ light_sampling })} />
      <SelectField
        label="Tone mapping"
        value={r.tone_map}
        options={[{ value: "aces", label: "ACES filmic" }, { value: "reinhard", label: "Reinhard" }, { value: "clamp", label: "Clamp" }] as const}
        onChange={(tone_map) => setRender({ tone_map })}
      />
      <NumberField label="Exposure" value={r.exposure} min={0.01} max={100} step={0.1} onChange={(exposure) => setRender({ exposure })} />
      <NumberField label="Threads" value={r.threads ?? 0} integer min={0} max={256} step={1} hint="0 = all cores" onChange={(threads) => setRender({ threads })} />
      <NumberField label="Seed" value={r.seed ?? 1} integer min={0} step={1} onChange={(seed) => setRender({ seed })} />

      <h2>Background</h2>
      <SelectField
        label="Kind"
        value={bg.type}
        options={[{ value: "gradient", label: "Sky gradient" }, { value: "solid", label: "Solid colour" }] as const}
        onChange={(type) => setBg(type === "solid" ? { type, color: bg.color ?? bg.top ?? [0, 0, 0] } : { type, top: bg.top ?? [0.5, 0.7, 1], bottom: bg.bottom ?? [1, 1, 1] })}
      />
      {bg.type === "gradient" ? (
        <>
          <ColorField label="Zenith" value={bg.top ?? [0.5, 0.7, 1]} onChange={(top) => setBg({ top })} />
          <ColorField label="Horizon" value={bg.bottom ?? [1, 1, 1]} onChange={(bottom) => setBg({ bottom })} />
        </>
      ) : (
        <ColorField label="Colour" value={bg.color ?? [0, 0, 0]} onChange={(color) => setBg({ color })} />
      )}
      <NumberField label="Intensity" value={bg.intensity ?? 1} min={0} step={0.1} onChange={(intensity) => setBg({ intensity })} />
    </>
  );
}
