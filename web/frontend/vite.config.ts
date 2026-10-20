import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Cross-origin isolation enables SharedArrayBuffer, which the threaded
// WebAssembly renderer needs. Static hosts that cannot send these headers
// (GitHub Pages) get them from public/coi-sw.js instead.
const isolation = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
};

export default defineConfig({
  plugins: [react()],
  base: "./",  // works at any URL prefix, e.g. https://<user>.github.io/ray-tracer/
  worker: { format: "es" },
  server: {
    headers: isolation,
    fs: { allow: ["../.."] },  // example scenes are bundled from assets/scenes
    // With `?engine=server` the Go backend runs on :8080; Vite proxies /api to it.
    proxy: { "/api": "http://127.0.0.1:8080" },
  },
  preview: { headers: isolation },
});
