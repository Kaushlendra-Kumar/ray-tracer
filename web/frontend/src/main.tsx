import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { serverEngine } from "./api";
import { App } from "./App";
import { wasmEngine } from "./wasm/engine";
import "./styles.css";

// Default: render in the browser with the WebAssembly build of the C++
// renderer. `?engine=server` uses the Go backend and the native binary instead.
const useServer = new URLSearchParams(location.search).get("engine") === "server";
const engine = useServer ? serverEngine : wasmEngine(new URL("wasm/", document.baseURI).href);

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App engine={engine} initialExample="cornell_glass_metal" />
  </StrictMode>,
);
