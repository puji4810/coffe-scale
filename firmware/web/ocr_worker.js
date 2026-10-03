// OCR worker — PaddleOCR (PP-OCRv5) det+rec on onnxruntime-web, WebGPU with
// WASM fallback. Runs entirely off the main thread; everything below
// vendor/ort|ocr is fetched lazily on first `warm` and cached by the SW.
//
// messages in:
//   {t:'warm'}                          → progress / ready / error
//   {t:'detect', id, w,h,rgba}          → {t:'boxes',  id, boxes, ms}
//   {t:'read',    id, w,h,rgba}         → {t:'lines',  id, lines, ms}
// rgba is a transferred ArrayBuffer of w*h*4 bytes.

import * as PP from './ocr_pp.js';

const ORT_MJS  = './vendor/ort/ort.webgpu.min.mjs';
const OCR_DIR  = './vendor/ocr/v5';   // dir name = model version
const ASSETS = {
    det:  `${OCR_DIR}/det.onnx`,      // PP-OCRv5 mobile
    rec:  `${OCR_DIR}/rec.onnx`,
    keys: `${OCR_DIR}/keys.txt`,
    rec4: `${OCR_DIR}/rec4.onnx`,     // PP-OCRv4 rec — second opinion for
    keys4: `${OCR_DIR}/keys4.txt`,    // weak lines (italic/display fonts)
    sizes: `${OCR_DIR}/manifest.json`,
};
// warm-download set, in manifest-key form (path relative to web root)
const WARM = [
    'vendor/ort/ort.webgpu.min.mjs',
    'vendor/ort/ort-wasm-simd-threaded.asyncify.mjs',
    'vendor/ort/ort-wasm-simd-threaded.asyncify.wasm',
    'vendor/ocr/v5/det.onnx',
    'vendor/ocr/v5/rec.onnx',
    'vendor/ocr/v5/keys.txt',
];
const FALLBACK_TOTAL = 49e6;          // if manifest.json is absent

let ort = null, detSess = null, recSess = null, keys = null;
let rec4Sess = null, keys4 = null, rec4Loading = null;
let warming = null;
let jobs = Promise.resolve(), epoch = 0;
const enqueue = fn => {
    const job = jobs.then(fn);
    jobs = job.catch(() => {});
    return job;
};

const post = m => self.postMessage(m);

// Model files run 10-30 MB — a flat timeout aborts a slow-but-healthy
// download, and the aborted response never lands in the cache, so the next
// scan re-downloads from zero. Bound STALLS instead: 45 s without a byte.
async function fetchBuf(url, onBytes, discard = false) {
    const stall = (p, what) => {
        let t;
        return Promise.race([p, new Promise((_, rej) => t = setTimeout(
            () => rej(new Error(`${url} ${what} stalled`)), 45000))])
            .finally(() => clearTimeout(t));
    };
    const r = await stall(fetch(url), 'headers');
    if (!r.ok) throw new Error(`${url} → HTTP ${r.status}`);
    const total = +r.headers.get('content-length') || 0;
    const rd = r.body.getReader(), chunks = [];
    let got = 0;
    try {
        for (;;) {
            const { done, value } = await stall(rd.read(), 'download');
            if (done) break;
            if (!discard) chunks.push(value);
            got += value.length;
            onBytes?.(got, total);
        }
    } catch (e) {
        try { rd.cancel(); } catch {}
        throw e;
    }
    if (discard) return;
    const buf = new Uint8Array(got);
    let o = 0;
    for (const c of chunks) { buf.set(c, o); o += c.length; }
    return buf.buffer;
}

// One-time lazy load: ort runtime + models, with byte-level progress.
// The total comes from manifest.json — content-length under compression
// reports the TRANSFER size while we count DECODED bytes, so it lies.
function warm() {
    if (warming) return warming;
    warming = (async () => {
        let sizes = {};
        try {
            sizes = await (await fetch(ASSETS.sizes)).json();
        } catch { /* manifest optional */ }
        const total = WARM.reduce((s, f) => s + (sizes[f] || 0), 0)
                      || FALLBACK_TOTAL;
        const got = new Map();
        const track = f => (g) => {
            got.set(f, g);
            let G = 0;
            for (const v of got.values()) G += v;
            post({ t: 'progress', got: G, total });
        };
        // prefetch the mjs glue + wasm binaries so ort's own internal
        // fetch (which can't report progress) is a cache hit
        await Promise.all(WARM.slice(0, 3).map(f => fetchBuf('./' + f, track(f), true)));
        ort = await import(ORT_MJS);
        ort.env.logLevel = 'warning';
        ort.env.wasm.wasmPaths = new URL('./vendor/ort/', import.meta.url).href;

        const [keyBuf, detBuf, recBuf] = await Promise.all([
            fetchBuf(ASSETS.keys, track(WARM[5])),
            fetchBuf(ASSETS.det, track(WARM[3])),
            fetchBuf(ASSETS.rec, track(WARM[4])),
        ]);
        keys = decodeKeys(keyBuf);
        for (const [name, buf] of [['det', detBuf], ['rec', recBuf]]) {
            post({ t: 'progress', building: name });
            const sess = await ort.InferenceSession.create(buf, {
                executionProviders: ['webgpu', 'wasm'],
                graphOptimizationLevel: 'all',
            });
            if (name === 'det') detSess = sess; else recSess = sess;
        }
        post({ t: 'ready', gpu: !!navigator.gpu });
    })().catch(e => {
        // The UI replaces this worker on a failed warm, releasing any partial
        // sessions rather than building another detector in the same heap.
        post({ t: 'error', stage: 'warm', message: String(e.message || e) });
    });
    return warming;
}

// ppocr class space = blank + keys + ' '
function decodeKeys(buf) {
    const k = new TextDecoder().decode(buf)
        .split('\n').map(s => s.replace(/\r$/, ''));
    if (k.length && k[k.length - 1] === '') k.pop();
    return k;
}

// PP-OCRv4 rec — downloaded only when a weak line first needs a second
// opinion, so the common case never pays the extra ~11 MB.
function ensureRec4() {
    rec4Loading ??= (async () => {
        keys4 = decodeKeys(await fetchBuf(ASSETS.keys4));
        const buf = await fetchBuf(ASSETS.rec4);
        // Download off the critical path, but session creation still shares
        // ORT's WASM/GPU runtime and must use the same queue as inference.
        await enqueue(async () => {
            rec4Sess = await ort.InferenceSession.create(buf, {
                executionProviders: ['webgpu', 'wasm'],
                graphOptimizationLevel: 'all',
            });
        });
    })().catch(() => { rec4Loading = null; });
    return rec4Loading;
}

async function recRun(sess, keyset, inp) {
    const t = new ort.Tensor('float32', inp.data, [1, 3, 48, inp.W]);
    let out;
    try {
        out = await sess.run({ [sess.inputNames[0]]: t });
        const lg = out[sess.outputNames[0]];
        return PP.ctcDecode(lg.data, lg.dims[1], lg.dims[2], keyset);
    } finally {
        t.dispose();
        for (const tensor of Object.values(out || {})) tensor.dispose();
    }
}

// One text box → best-effort text. Weak reads (<0.55) retry inverted
// (light-on-dark labels) and then ask the v4 recognizer — it handles
// italic/decorative latin that v5 drops.
const WEAK = 0.55;
const RETRY_BUDGET_MS = 300;
async function readBox(rgba, w, h, b, best = null, canRetry = () => false) {
    const { pts } = b;
    const wTop = Math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]);
    const hSide = Math.hypot(pts[3][0] - pts[0][0], pts[3][1] - pts[0][1]);
    const pw = Math.max(8, Math.round(wTop)), ph = Math.max(8, Math.round(hSide));
    const patch = PP.warpQuad(rgba, w, h, pts, pw, ph);

    if (!best) return recRun(recSess, keys, PP.recInput(patch, pw, ph));
    if (best.score < WEAK && canRetry()) {
        const r = await recRun(recSess, keys,
            PP.recInput(PP.invertPatch(patch), pw, ph));
        if (r.score > best.score) best = r;
    }
    if (best.score < WEAK) {
        ensureRec4(); // optional model download must never hold up this scan
        if (rec4Sess && canRetry()) {
            try {
                for (const p of [patch, PP.invertPatch(patch)]) {
                    if (best.score >= WEAK || !canRetry()) break;
                    const r = await recRun(rec4Sess, keys4,
                        PP.recInput(p, pw, ph));
                    if (r.score > best.score) best = r;
                }
            } catch { /* optional second opinion must retain the primary read */ }
        }
    }
    return best;
}

async function runDet(w, h, rgba) {
    const inp = PP.detInput(rgba, w, h);
    const t = new ort.Tensor('float32', inp.data, [1, 3, inp.H, inp.W]);
    let out;
    try {
        out = await detSess.run({ [detSess.inputNames[0]]: t });
        const prob = out[detSess.outputNames[0]];
        const boxes = PP.findTextBoxes(prob.data, inp.W, inp.H)
            .map(b => ({ pts: PP.scaleQuad(b.pts, 1 / inp.sx, 1 / inp.sy),
                         score: b.score }));
        return boxes;
    } finally {
        t.dispose();
        for (const tensor of Object.values(out || {})) tensor.dispose();
    }
}

// reading order: cluster by vertical centre, left→right inside a line
function readOrder(boxes) {
    const hs = boxes.map(b => Math.abs(b.pts[2][1] - b.pts[0][1]));
    const med = hs.sort((a, b) => a - b)[hs.length >> 1] || 20;
    return boxes.map(b => ({ b, cy: (b.pts[0][1] + b.pts[2][1]) / 2,
                             cx: (b.pts[0][0] + b.pts[2][0]) / 2 }))
        .sort((a, b2) =>
            Math.abs(a.cy - b2.cy) > med * 0.6 ? a.cy - b2.cy : a.cx - b2.cx)
        .map(o => o.b);
}

async function handle(m, current) {
    try {
        if (current !== epoch && m.t !== 'warm') return;
        if (m.t === 'warm') { await warm(); return; }
        if (!recSess) await warm();
        if (!recSess || current !== epoch) return;
        const rgba = new Uint8Array(m.rgba);
        if (m.t === 'detect') {
            const t0 = performance.now();
            const boxes = await runDet(m.w, m.h, rgba);
            if (current !== epoch) return;
            post({ t: 'boxes', id: m.id, boxes, ms: performance.now() - t0 });
        } else if (m.t === 'read') {
            const t0 = performance.now();
            const boxes = readOrder(await runDet(m.w, m.h, rgba)).slice(0, 24);
            const results = [];
            for (const b of boxes) {
                if (current !== epoch) return;
                results.push(await readBox(rgba, m.w, m.h, b));
            }
            // One pass over ALL lines first; retries share one time budget
            // for the frame, rather than multiplying latency by four per box.
            const retryStart = performance.now();
            const canRetry = () => current === epoch &&
                performance.now() - retryStart < RETRY_BUDGET_MS;
            const weak = results.map((r, i) => ({ r, i }))
                .filter(({ r }) => r.score < WEAK).sort((a, b) => a.r.score - b.r.score);
            for (const { r, i } of weak) {
                if (!canRetry()) break;
                results[i] = await readBox(rgba, m.w, m.h, boxes[i], r, canRetry);
            }
            if (current !== epoch) return;
            const lines = [];
            for (let i = 0; i < results.length; i++) {
                const { text, score } = results[i], b = boxes[i];
                if (text.trim()) lines.push({ text, score: +score.toFixed(3),
                    quad: b.pts.map(p => p.map(v => +v.toFixed(1))) });
            }
            post({ t: 'lines', id: m.id, lines,
                   ms: Math.round(performance.now() - t0) });
        }
    } catch (err) {
        post({ t: 'error', id: m.id, message: String(err.message || err) });
    }
}

// Async message handlers otherwise overlap whenever session.run() yields.
// Cancellation invalidates old queued work immediately; the one active ORT
// call finishes before the next session can touch the shared runtime.
self.onmessage = e => {
    const m = e.data;
    if (m.t === 'cancel') { ++epoch; return; }
    const current = epoch;
    return enqueue(() => handle(m, current));
};
