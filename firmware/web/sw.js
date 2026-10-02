// coffee-scale service worker — precaches the whole app shell so the PWA
// opens offline (BLE doesn't need the network). site.sh stamps __BUILD__
// with a hash of the assembled assets, so any content change produces a
// new sw.js and browsers pick it up on the next update check.
const CACHE = 'coffee-scale-__BUILD__';
// OCR vendor assets (~49 MB) live in a version-independent cache that
// survives app updates — they are the dominant download and must not be
// evicted every deploy. Bump the name only to purge stale models; the
// vendor/ocr/vN/ path versioning means old entries simply go unused.
const OCR_CACHE = 'coffee-scale-ocr-2';
const ASSETS = [
  './',
  './index.html',
  './app.js',
  './ble.js',
  './chart.js',
  './store.js',
  './scan.js',
  './ocr_worker.js',
  './ocr_pp.js',
  './bean_parse.js',
  './manifest.webmanifest',
  './vendor/uPlot.iife.min.js',
  './vendor/uPlot.min.css',
  './vendor/logo.woff2',
  './vendor/barlow-semi-condensed-latin-400-normal.woff2',
  './vendor/barlow-semi-condensed-latin-500-normal.woff2',
  './vendor/barlow-semi-condensed-latin-600-normal.woff2',
  './dist/scale_screen.mjs',
  './dist/scale_screen.wasm',
  './icons/icon-192.png',
  './icons/icon-512.png',
  './icons/maskable-512.png',
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(ASSETS)));
  self.skipWaiting();
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(
        keys.filter(k => k !== CACHE && k !== OCR_CACHE)
            .map(k => caches.delete(k))))
      .then(() => self.clients.claim()));
});

// Navigations go network-first so a deployed update lands on the very
// next load; the precached shell is only the offline fallback. Hashed
// asset requests stay cache-first — they are immutable within a build.
// Non-GET and cross-origin requests go straight to the network.
self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.origin !== location.origin) {
    return;
  }
  if (e.request.mode === 'navigate') {
    e.respondWith(
      fetch(e.request).catch(() =>
        caches.match('./index.html', { ignoreSearch: true })));
    return;
  }
  // Big lazy assets (vendor/ort wasm runtime, vendor/ocr models) are NOT in
  // the precache — they download on first scan use, then land in OCR_CACHE
  // which outlives app-version caches so redeploys don't re-download ~49 MB.
  const ocrAsset = url.pathname.startsWith(
      new URL('./vendor/ort/', location.href).pathname) ||
      url.pathname.startsWith(
      new URL('./vendor/ocr/', location.href).pathname);
  e.respondWith(
    caches.match(e.request).then(hit =>
      hit || fetch(e.request).then(res => {
        if (res.ok) {
          const put = caches.open(ocrAsset ? OCR_CACHE : CACHE)
            .then(c => c.put(e.request, res.clone()));
          e.waitUntil(put);
        }
        return res;
      })));
});
