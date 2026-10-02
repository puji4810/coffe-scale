// Exercise the actual worker handler and postprocess, with controllable ORT I/O.
// Run: node web/ocr_worker_test.mjs (also included by ocr_test.mjs).
import { readFile } from 'node:fs/promises';
import { createContext, runInContext } from 'node:vm';
import * as PP from './ocr_pp.js';
import assert from 'node:assert/strict';
import { test } from 'node:test';

const flush = async () => { for (let i = 0; i < 50; i++) await Promise.resolve(); };
async function engine({ failRun = false, weak = false, stalledFallback = false, boxCount = 1 } = {}) {
    const posts = [], tensors = [], runs = [], gates = [];
    let active = 0, maxActive = 0, now = 0;
    class Tensor {
        constructor(type, data, dims) {
            Object.assign(this, { data, dims, disposed: false }); tensors.push(this);
        }
        dispose() { this.disposed = true; }
    }
    const ort = {
        Tensor, env: { wasm: {} }, InferenceSession: { async create(buf) {
            const det = new Uint8Array(buf)[0] === 1;
            return {
                inputNames: ['x'], outputNames: ['y'], async release() {},
                async run({ x }) {
                    active++; maxActive = Math.max(maxActive, active);
                    runs.push(det ? 'det' : 'rec');
                    if (gates.length) await gates.shift();
                    active--;
                    if (failRun) throw new Error('inference failed');
                    now += det ? 100 : 100;
                    if (!det) return { y: new Tensor('float32', new Float32Array([0, weak ? .2 : .95, 0]), [1, 1, 3]) };
                    const [, , H, W] = x.dims, prob = new Float32Array(W * H);
                    for (let b = 0; b < boxCount; b++)
                        for (let y = 10 + b * 35; y < 25 + b * 35; y++)
                            for (let x = 10; x < W - 10; x++) prob[y * W + x] = .9;
                    return { y: new Tensor('float32', prob, [1, 1, H, W]) };
                },
            };
        } },
    };
    const context = createContext({
        PP, loadOrt: async () => ort,
        self: { postMessage: m => posts.push(m) }, navigator: {},
        performance: { now: () => now }, URL, TextDecoder, AbortController, AbortSignal,
        setTimeout, clearTimeout,
        async fetch(url) {
            if (stalledFallback && /keys4|rec4/.test(url)) return new Promise(() => {});
            const data = /keys/.test(url) ? new TextEncoder().encode('咖\n') : new Uint8Array([/det.onnx/.test(url) ? 1 : 2]);
            return { ok: true, json: async () => ({}), headers: { get: () => null },
                body: { getReader() {
                    let sent = false;
                    return { async read() { if (sent) return { done: true }; sent = true; return { value: data, done: false }; } };
                } },
            };
        },
    });
    const source = (await readFile(new URL('./ocr_worker.js', import.meta.url), 'utf8'))
        .replace("import * as PP from './ocr_pp.js';", '')
        .replace('import(ORT_MJS)', 'loadOrt(ORT_MJS)')
        .replaceAll('import.meta.url', JSON.stringify(new URL('./ocr_worker.js', import.meta.url).href));
    runInContext(source, context);
    const send = m => context.self.onmessage({ data: m });
    await send({ t: 'warm' });
    return { send, posts, tensors, runs, gates, maxActive: () => maxActive,
        frame: (t, id) => ({ t, id, w: 96, h: Math.max(64, boxCount * 35 + 10),
            rgba: new Uint8Array(96 * Math.max(64, boxCount * 35 + 10) * 4).fill(200).buffer }),
    };
}

await test('preview and repeated read requests never run inference concurrently', async () => {
    const e = await engine();
    let release; e.gates.push(new Promise(r => release = r));
    const preview = e.send(e.frame('detect', 1)); await flush();
    const read = e.send(e.frame('read', 2)); await flush();
    const rescan = e.send(e.frame('read', 3)); await flush();
    release(); await Promise.all([preview, read, rescan]); await flush();
    assert.equal(e.maxActive(), 1, 'overlapping ORT runs can wedge WASM/WebGPU');
    assert.equal(e.posts.filter(m => m.t === 'lines').length, 2);
});

await test('failed inference disposes its input before the next scan', async () => {
    const e = await engine({ failRun: true }); await e.send(e.frame('read', 1)); await flush();
    assert.ok(e.posts.some(m => m.t === 'error' && m.id === 1));
    assert.ok(e.tensors.every(t => t.disposed), 'failed runs retain WASM tensors');
});

await test('a weak line returns an initial result without waiting for v4 download', async () => {
    const e = await engine({ weak: true, stalledFallback: true });
    e.send(e.frame('read', 1)); await flush();
    assert.ok(e.posts.some(m => m.t === 'lines' && m.id === 1), 'scan blocked on optional model download');
});

await test('many weak lines share one retry budget instead of four runs per line', async () => {
    const e = await engine({ weak: true, stalledFallback: true, boxCount: 8 });
    await e.send(e.frame('read', 1)); await flush();
    const result = e.posts.find(m => m.t === 'lines' && m.id === 1);
    assert.equal(result.lines.length, 8);
    assert.ok(e.runs.filter(r => r === 'rec').length <= 11);
});

await test('close cancels an active scan and discards queued scans before reopening', async () => {
    const e = await engine();
    let release; e.gates.push(new Promise(r => release = r));
    const old = e.send(e.frame('read', 1)); await flush();
    const queued = e.send(e.frame('read', 2));
    e.send({ t: 'cancel' });
    const current = e.send(e.frame('read', 3));
    release(); await Promise.all([old, queued, current]); await flush();
    assert.deepEqual(e.posts.filter(m => m.t === 'lines').map(m => m.id), [3]);
    assert.equal(e.maxActive(), 1);
    assert.ok(e.tensors.every(t => t.disposed));
});

await test('thirty consecutive scans release all inference tensors', async () => {
    const e = await engine();
    for (let id = 1; id <= 30; id++) await e.send(e.frame('read', id));
    assert.equal(e.posts.filter(m => m.t === 'lines').length, 30);
    assert.ok(e.tensors.every(t => t.disposed));
});
