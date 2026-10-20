import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { serverEngine, type Engine, type FrameImage, type FrameInfo, type RenderStatus } from "./api";
import { Hierarchy, Inspector } from "./panels";
import {
  blankScene, defaultLight, defaultMaterial, defaultObject, duplicateObject, materialUsers, normalizeScene,
  previewScene, uniqueName,
} from "./scene";
import type { Camera, ObjectType, Scene, Selection } from "./types";
import { Viewport } from "./Viewport";

const STORAGE_KEY = "rt-editor:saved-scenes";

function loadSaved(): Record<string, Scene> {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "{}");
  } catch {
    return {};
  }
}

const REPO_URL = "https://github.com/Kaushlendra-Kumar/ray-tracer";

// `engine` decides where rendering happens (see api.ts). `initialExample`
// opens and renders that example scene on start-up.
export function App({ engine = serverEngine, initialExample }: { engine?: Engine; initialExample?: string }) {
  const [scene, setSceneState] = useState<Scene>(blankScene);
  const [selection, setSelection] = useState<Selection>(null);
  const [status, setStatus] = useState<RenderStatus>({ state: "idle" });
  const [image, setImage] = useState<FrameImage | null>(null);
  const [engineLabel, setEngineLabel] = useState("");
  const [frame, setFrame] = useState<FrameInfo | null>(null);
  const [examples, setExamples] = useState<string[]>([]);
  const [saved, setSaved] = useState<Record<string, Scene>>(loadSaved);
  const [autoRender, setAutoRender] = useState(true);
  const [notice, setNotice] = useState<string | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);

  const client = useMemo(
    () => engine.createClient(setStatus, (img, info) => { setImage(img); setFrame(info); }),
    [engine],
  );
  useEffect(() => () => client.dispose(), [client]);

  const sceneRef = useRef(scene);
  sceneRef.current = scene;

  const render = useCallback((s: Scene = sceneRef.current) => client.start(s, false), [client]);

  // Edits re-render automatically after a short pause (progressive, so the
  // first noisy frame appears quickly).
  const editTimer = useRef<number | undefined>(undefined);
  const setScene = useCallback((fn: (s: Scene) => Scene) => {
    setSceneState((prev) => {
      const next = fn(prev);
      if (autoRender && next !== prev) {
        window.clearTimeout(editTimer.current);
        editTimer.current = window.setTimeout(() => render(next), 300);
      }
      return next;
    });
  }, [autoRender, render]);

  useEffect(() => {
    engine.listScenes().then(setExamples).catch(() => setNotice(engine.unavailable));
    engine.describe().then(setEngineLabel).catch((e: Error) => setNotice(e.message));
    if (initialExample) openExample(initialExample);
  }, [engine]);

  // --- Camera gestures: low-res previews while dragging, full render after.
  const previewTimer = useRef<number | undefined>(undefined);
  const lastPreview = useRef(0);
  const onCameraPreview = (camera: Camera) => {
    const next = { ...sceneRef.current, camera };
    setSceneState(next);
    sceneRef.current = next;
    const now = performance.now();
    window.clearTimeout(previewTimer.current);
    // Throttle preview jobs to ~8 per second; each new one cancels the last.
    const fire = () => { lastPreview.current = performance.now(); client.start(previewScene(sceneRef.current), true); };
    if (now - lastPreview.current > 120) fire();
    else previewTimer.current = window.setTimeout(fire, 120);
  };
  const onCameraCommit = (camera: Camera) => {
    window.clearTimeout(previewTimer.current);
    const next = { ...sceneRef.current, camera };
    setSceneState(next);
    render(next);
  };

  // --- Structure edits
  const addObject = (t: ObjectType) => {
    const s = sceneRef.current;
    let mat = Object.keys(s.materials)[0];
    let materials = s.materials;
    if (!mat) { mat = "default"; materials = { default: defaultMaterial("lambertian") }; }
    const obj = defaultObject(t, s, mat);
    setScene((p) => ({ ...p, materials, objects: [...p.objects, obj] }));
    setSelection({ kind: "object", index: s.objects.length });
  };
  const addLight = () => {
    setScene((p) => ({ ...p, lights: [...p.lights, defaultLight(p)] }));
    setSelection({ kind: "light", index: sceneRef.current.lights.length });
  };
  const addMaterial = () => {
    const name = uniqueName("material", Object.keys(sceneRef.current.materials));
    setScene((p) => ({ ...p, materials: { ...p.materials, [name]: defaultMaterial("lambertian") } }));
    setSelection({ kind: "material", name });
  };
  const duplicate = () => {
    if (selection?.kind !== "object") return;
    setScene((p) => duplicateObject(p, selection.index));
    setSelection({ kind: "object", index: selection.index + 1 });
  };
  const remove = () => {
    if (!selection) return;
    if (selection.kind === "object") setScene((p) => ({ ...p, objects: p.objects.filter((_, i) => i !== selection.index) }));
    else if (selection.kind === "light") setScene((p) => ({ ...p, lights: p.lights.filter((_, i) => i !== selection.index) }));
    else if (selection.kind === "material") {
      const users = materialUsers(sceneRef.current, selection.name);
      if (users.length) {
        setNotice(`“${selection.name}” is still used by ${users.join(", ")}. Assign those objects another material first.`);
        return;
      }
      setScene((p) => {
        const materials = { ...p.materials };
        delete materials[selection.name];
        return { ...p, materials };
      });
    }
    setSelection(null);
  };

  // --- Files and storage
  const loadScene = async (raw: unknown, label: string) => {
    const s = normalizeScene(raw);
    const v = await engine.validateScene(s).catch(() => ({ ok: true }) as { ok: boolean; error?: string });
    if (!v.ok) {
      setNotice(`${label} was not loaded: ${v.error}`);
      return;
    }
    setNotice(null);
    setSelection(null);
    setSceneState(s);
    setImage(null);
    setFrame(null);
    render(s);
  };
  const openExample = async (name: string) => {
    try {
      await loadScene(await engine.fetchScene(name), name);
    } catch (e) {
      setNotice(String((e as Error).message));
    }
  };
  const importFile = async (file: File) => {
    try {
      await loadScene(JSON.parse(await file.text()), file.name);
    } catch (e) {
      setNotice(`${file.name} is not valid JSON: ${(e as Error).message}`);
    }
  };
  const exportFile = () => {
    const blob = new Blob([JSON.stringify(scene, null, 2)], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `${scene.name.replace(/[^\w-]+/g, "_") || "scene"}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
  };
  const downloadImage = () => {
    if (!image) return;
    const c = document.createElement("canvas");
    c.width = image.width;
    c.height = image.height;
    c.getContext("2d")?.drawImage(image, 0, 0);
    c.toBlob((blob) => {
      if (!blob) return;
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${scene.name.replace(/[^\w-]+/g, "_") || "render"}.png`;
      a.click();
      URL.revokeObjectURL(a.href);
    }, "image/png");
  };
  const saveLocal = () => {
    const next = { ...saved, [scene.name]: scene };
    setSaved(next);
    localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
    setNotice(`Saved “${scene.name}” in this browser.`);
  };
  const deleteLocal = (name: string) => {
    const next = { ...saved };
    delete next[name];
    setSaved(next);
    localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
  };

  const busy = status.state === "rendering" || status.state === "starting";

  return (
    <div className="app">
      <header className="toolbar">
        <input
          className="title"
          aria-label="Scene name"
          value={scene.name}
          onChange={(e) => setSceneState({ ...scene, name: e.target.value })}
        />
        <div className="tools">
          <select aria-label="Open an example scene" value="" onChange={(e) => e.target.value && openExample(e.target.value)}>
            <option value="">Open example…</option>
            {examples.map((n) => <option key={n} value={n}>{n.replace(/_/g, " ")}</option>)}
          </select>
          <select aria-label="Open a saved scene" value="" onChange={(e) => e.target.value && loadScene(saved[e.target.value], e.target.value)}>
            <option value="">Open saved…</option>
            {Object.keys(saved).map((n) => <option key={n} value={n}>{n}</option>)}
          </select>
          <button onClick={saveLocal}>Save</button>
          {saved[scene.name] && <button className="quiet" onClick={() => deleteLocal(scene.name)}>Forget saved copy</button>}
          <button onClick={() => fileInput.current?.click()}>Import JSON</button>
          <button onClick={exportFile}>Export JSON</button>
          <button onClick={() => loadScene(blankScene(), "New scene")}>New</button>
          <button onClick={downloadImage} disabled={!image}>Download PNG</button>
          <input ref={fileInput} type="file" accept="application/json,.json" hidden
            onChange={(e) => { const f = e.target.files?.[0]; if (f) importFile(f); e.target.value = ""; }} />
        </div>
        <div className="render-controls">
          {engineLabel && <span className="engine" title="Where the path tracer is running">{engineLabel}</span>}
          <a className="repo" href={REPO_URL} target="_blank" rel="noreferrer">Source</a>
          <label className="auto">
            <input type="checkbox" checked={autoRender} onChange={(e) => setAutoRender(e.target.checked)} /> Re-render on edit
          </label>
          {busy ? (
            <button className="primary" onClick={() => client.cancel()}>Cancel</button>
          ) : (
            <button className="primary" onClick={() => render()}>Render</button>
          )}
          <button onClick={() => render()} disabled={!busy}>Restart</button>
        </div>
      </header>

      {notice && (
        <div className="notice" role="status">
          <span>{notice}</span>
          <button className="quiet" onClick={() => setNotice(null)}>Dismiss</button>
        </div>
      )}

      <main className="workspace">
        <Hierarchy
          scene={scene}
          selection={selection}
          select={setSelection}
          addObject={addObject}
          addLight={addLight}
          addMaterial={addMaterial}
          duplicate={duplicate}
          remove={remove}
        />
        <Viewport
          image={image}
          frame={frame}
          status={status}
          camera={scene.camera}
          aspect={scene.render.width / scene.render.height}
          onCameraPreview={onCameraPreview}
          onCameraCommit={onCameraCommit}
        />
        <Inspector scene={scene} selection={selection} setScene={setScene} select={setSelection} />
      </main>
    </div>
  );
}
