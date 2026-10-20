// Service worker that makes the page cross-origin isolated on hosts that
// cannot set response headers (GitHub Pages). It re-serves every response
// with COOP/COEP so that SharedArrayBuffer, and with it the threaded
// WebAssembly renderer, is available. Registered from index.html.
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (event) => event.waitUntil(self.clients.claim()));

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.cache === "only-if-cached" && req.mode !== "same-origin") return;
  event.respondWith(
    fetch(req).then((res) => {
      if (res.status === 0) return res;  // opaque cross-origin response: leave untouched
      const headers = new Headers(res.headers);
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
      headers.set("Cross-Origin-Embedder-Policy", "require-corp");
      headers.set("Cross-Origin-Resource-Policy", "cross-origin");
      return new Response(res.body, { status: res.status, statusText: res.statusText, headers });
    }),
  );
});
