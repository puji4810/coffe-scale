// OCR worker — PaddleOCR (PP-OCRv4) det+rec on onnxruntime-web, WebGPU with
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
const ORT_BIN  = [ // prefetched for progress + SW cache warm; ort fetches again
    './vendor/ort/ort-wasm-simd-threaded.asyncify.mjs',
    './vendor/ort/ort-wasm-simd-threaded.asyncify.wasm',
];
const ASSETS = {
    det:  './vendor/ocr/det.onnx',
    rec:  './vendor/ocr/rec.onnx',
    keys: './vendor/ocr/keys.txt',
};

let ort = null, detSess = null, recSess = null, keys = null;
let warming = null;

const post = m => self.postMessage(m);

async function fetchBuf(url, onBytes) {
    const r = await fetch(url);
    if (!r.ok) throw new Error(`${url} → HTTP ${r.status}`);
    const total = +r.headers.get('content-length') || 0;
    const rd = r.body.getReader(), chunks = [];
    let got = 0;
    for (;;) {
        const { done, value } = await rd.read();
        if (done) break;
        chunks.push(value); got += value.length;
        onBytes?.(got, total);
    }
    const buf = new Uint8Array(got);
    let o = 0;
    for (const c of chunks) { buf.set(c, o); o += c.length; }
    return buf.buffer;
}

// One-time lazy load: ort runtime + models, with byte-level progress.
function warm() {
    if (warming) return warming;
    warming = (async () => {
        const files = [...ORT_BIN, ASSETS.det, ASSETS.rec, ASSETS.keys];
        const sizes = new Map(), got = new Map();
        const onBytes = (f, g, t) => {
            got.set(f, g); if (t) sizes.set(f, t);
            let G = 0, T = 0;
            for (const f2 of files) { G += got.get(f2) || 0; T += sizes.get(f2) || 0; }
            post({ t: 'progress', got: G, total: T });
        };
        // prefetch the wasm binaries through the SW cache so ort's own
        // internal fetch (which can't report progress) is a cache hit
        for (const f of ORT_BIN) await fetchBuf(f, (g, t) => onBytes(f, g, t));
        ort = await import(ORT_MJS);
        ort.env.logLevel = 'warning';
        ort.env.wasm.wasmPaths = new URL('./vendor/ort/', import.meta.url).href;

        const keysTxt = await fetchBuf(ASSETS.keys, (g, t) => onBytes(ASSETS.keys, g, t));
        keys = new TextDecoder().decode(keysTxt)
            .split('\n').map(s => s.replace(/\r$/, ''));
        if (keys.length && keys[keys.length - 1] === '') keys.pop();
        // ppocr class space = blank + keys + ' '
        for (const [name, url] of [['det', ASSETS.det], ['rec', ASSETS.rec]]) {
            const buf = await fetchBuf(url, (g, t) => onBytes(url, g, t));
            post({ t: 'progress', building: name });
            const sess = await ort.InferenceSession.create(buf, {
                executionProviders: ['webgpu', 'wasm'],
                graphOptimizationLevel: 'all',
            });
            if (name === 'det') detSess = sess; else recSess = sess;
        }
        post({ t: 'ready', gpu: !!navigator.gpu });
    })().catch(e => {
        warming = null;
        post({ t: 'error', stage: 'warm', message: String(e.message || e) });
    });
    return warming;
}

function runDet(w, h, rgba) {
    const inp = PP.detInput(rgba, w, h);
    const t = new ort.Tensor('float32', inp.data, [1, 3, inp.H, inp.W]);
    return detSess.run({ [detSess.inputNames[0]]: t }).then(out => {
        const prob = out[detSess.outputNames[0]];
        const boxes = PP.findTextBoxes(prob.data, inp.W, inp.H)
            .map(b => ({ pts: PP.scaleQuad(b.pts, 1 / inp.sx, 1 / inp.sy),
                         score: b.score }));
        return boxes;
    });
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

self.onmessage = async e => {
    const m = e.data;
    try {
        if (m.t === 'warm') { await warm(); return; }
        if (!detSess) await warm();
        if (!detSess) return; // warm error already reported
        const rgba = new Uint8Array(m.rgba);
        if (m.t === 'detect') {
            const t0 = performance.now();
            const boxes = await runDet(m.w, m.h, rgba);
            post({ t: 'boxes', id: m.id, boxes, ms: performance.now() - t0 });
        } else if (m.t === 'read') {
            const t0 = performance.now();
            const boxes = readOrder(await runDet(m.w, m.h, rgba)).slice(0, 24);
            const lines = [];
            for (const b of boxes) {
                const { pts } = b;
                const wTop = Math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]);
                const hSide = Math.hypot(pts[3][0] - pts[0][0], pts[3][1] - pts[0][1]);
                const pw = Math.max(8, Math.round(wTop)), ph = Math.max(8, Math.round(hSide));
                const patch = PP.warpQuad(rgba, m.w, m.h, pts, pw, ph);
                const inp = PP.recInput(patch, pw, ph);
                const t = new ort.Tensor('float32', inp.data, [1, 3, 48, inp.W]);
                const out = await recSess.run({ [recSess.inputNames[0]]: t });
                const logits = out[recSess.outputNames[0]];
                const T = logits.dims[1], C = logits.dims[2];
                const { text, score } = PP.ctcDecode(logits.data, T, C, keys);
                if (text.trim()) lines.push({ text, score: +score.toFixed(3),
                                              quad: pts.map(p => p.map(v => +v.toFixed(1))) });
            }
            post({ t: 'lines', id: m.id, lines,
                   ms: Math.round(performance.now() - t0) });
        }
    } catch (err) {
        post({ t: 'error', id: m.id, message: String(err.message || err) });
    }
};
