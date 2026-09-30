// coffee-scale service worker — precaches the whole app shell so the PWA
// opens offline (BLE doesn't need the network). Bump CACHE on every
// deploy that changes any listed asset.
const CACHE = 'coffee-scale-v3';
const ASSETS = [
  './',
  './index.html',
  './app.js',
  './ble.js',
  './chart.js',
  './store.js',
  './manifest.webmanifest',
  './vendor/uPlot.iife.min.js',
  './vendor/uPlot.min.css',
  './vendor/logo.woff2',
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
        keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim()));
});

// Cache-first: app shell never changes within a version. Non-GET and
// cross-origin requests go straight to the network.
self.addEventListener('fetch', e => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.origin !== location.origin) {
    return;
  }
  e.respondWith(
    caches.match(e.request, { ignoreSearch: true }).then(hit =>
      hit || fetch(e.request)));
});
