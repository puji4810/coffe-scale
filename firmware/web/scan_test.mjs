// Scanner lifecycle regression checks, with deterministic camera/worker timing.
// Run: node web/scan_test.mjs (also included by ocr_test.mjs).
import { readFile } from 'node:fs/promises';
import { createContext, runInContext } from 'node:vm';
import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseBeanLabel } from './bean_parse.js';

const deferred = () => {
    let resolve;
    const promise = new Promise(r => resolve = r);
    return { promise, resolve };
};
const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };

async function scanner(hooks = {}) {
    const elements = new Map(), cameras = [], workers = [], timers = new Map();
    const inputs = [], images = [];
    let timerId = 0;
    const element = id => {
        if (!elements.has(id)) elements.set(id, {
            hidden: true, disabled: false, value: '', textContent: '', innerHTML: '',
            videoWidth: 640, videoHeight: 480, clientWidth: 320, clientHeight: 240,
            addEventListener() {}, play: async () => {},
            classList: { remove() {} },
            getContext: () => ({
                drawImage() {}, clearRect() {}, beginPath() {}, moveTo() {},
                lineTo() {}, closePath() {}, stroke() {}, fill() {},
                setTransform() {},
                getImageData: () => ({ data: new Uint8ClampedArray(640 * 480 * 4) }),
            }),
        });
        return elements.get(id);
    };
    class Worker {
        constructor() { this.sent = []; workers.push(this); }
        postMessage(m) { this.sent.push(m); }
        terminate() { this.terminated = true; }
        emit(m) { this.onmessage({ data: m }); }
    }
    const context = createContext({
        parseBeanLabel,
        document: { getElementById: element, addEventListener() {}, createElement() {
            const input = { files: [{}], click() {} }; inputs.push(input); return input;
        } },
        createImageBitmap() { const image = deferred(); images.push(image); return image.promise; },
        navigator: { mediaDevices: { getUserMedia() {
            const camera = deferred(); cameras.push(camera); return camera.promise;
        } } }, Worker, addEventListener() {},
        setTimeout(fn, ms) { timers.set(++timerId, { fn, ms }); return timerId; },
        clearTimeout(id) { timers.delete(id); },
    });
    // Only replace the module's import/export syntax; run the actual handlers.
    const source = (await readFile(new URL('./scan.js', import.meta.url), 'utf8'))
        .replace("import { parseBeanLabel } from './bean_parse.js';", '')
        .replace('export function initScan', 'function initScan');
    runInContext(source, context);
    context.initScan({ getBrands: () => ['小满咖啡', 'Torch'], listBeans: () => [], ...hooks });
    function media() {
        const track = { stopped: false, stop() { this.stopped = true; } };
        return { track, getTracks: () => [track], getVideoTracks: () => [track] };
    }
    return { element, cameras, workers, timers, media, flush, images,
        open: () => element('btn-scan').onclick(),
        close: () => element('scan-close').onclick(),
        read: () => element('scan-read').onclick(),
        album() { element('scan-file').onclick(); inputs.at(-1).onchange(); },
        status: () => element('scan-status').textContent,
        tick(ms) {
            const found = [...timers].find(([, t]) => t.ms === ms);
            assert.ok(found, `expected timer ${ms}ms`);
            timers.delete(found[0]); found[1].fn();
        },
    };
}

const label = texts => texts.map((text, i) => ({ text, score: .98,
    quad: [[10, 10 + i * 30], [200, 10 + i * 30], [200, 30 + i * 30], [10, 30 + i * 30]],
}));
const LABEL_A = label(['小满咖啡', '埃塞俄比亚 耶加雪菲 水洗 瑰夏', '净含量 100g']);
const LABEL_B = label(['Torch', '肯尼亚 日晒 SL28', '烘焙日期 2026-10-02']);
async function readyScanner(hooks = {}) {
    const s = await scanner(hooks); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false });
    s.result = lines => {
        const request = s.workers[0].sent.filter(m => m.t === 'read').at(-1);
        s.workers[0].emit({ t: 'lines', id: request.id, lines, ms: 600 });
    };
    return s;
}

await test('opening another label clears the previous label fields and chips', async () => {
    const s = await readyScanner(); s.read(); s.result(LABEL_A);
    assert.equal(s.element('scan-f-brand').value, '小满咖啡');
    s.close(); s.open(); s.cameras[1].resolve(s.media()); await s.flush();
    assert.equal(s.element('scan-f-name').value, '');
    assert.equal(s.element('scan-lines').innerHTML, '');
    s.read(); s.result(LABEL_B);
    assert.match(s.element('scan-f-name').value, /肯尼亚/);
    assert.equal(s.element('scan-f-brand').value, 'Torch');
    assert.equal(s.element('scan-f-process').value, '日晒');
    assert.equal(s.element('scan-f-variety').value, 'SL28');
    assert.doesNotMatch(s.element('scan-f-note').value, /100g/);
});

await test('consecutive camera scans show only the current label', async () => {
    const s = await readyScanner(); s.read(); s.result(LABEL_A);
    s.read(); s.result(LABEL_B);
    assert.match(s.element('scan-f-name').value, /肯尼亚/);
    assert.equal(s.element('scan-f-brand').value, 'Torch');
    assert.doesNotMatch(s.element('scan-lines').innerHTML, /耶加雪菲|小满咖啡|100g/);
    assert.doesNotMatch(s.element('scan-f-note').value, /100g/);
});

await test('a new scan hides old results immediately, including a blank result', async () => {
    const s = await readyScanner(); s.read(); s.result(LABEL_A);
    assert.equal(s.element('scan-panel').hidden, false);
    s.read();
    assert.equal(s.element('scan-panel').hidden, true);
    assert.equal(s.element('scan-f-name').value, '');
    s.result([]);
    assert.equal(s.element('scan-panel').hidden, true);
    assert.equal(s.element('scan-lines').innerHTML, '');
});

await test('retake cancels an unfinished read instead of reviving its old label', async () => {
    const s = await readyScanner(); s.read(); s.result(LABEL_A);
    s.read();
    const old = s.workers[0].sent.filter(m => m.t === 'read').at(-1);
    s.element('scan-again').onclick();
    s.read();
    const current = s.workers[0].sent.filter(m => m.t === 'read').at(-1);
    assert.notEqual(current.id, old.id);
    s.workers[0].emit({ t: 'lines', id: old.id, lines: LABEL_A, ms: 600 });
    assert.equal(s.element('scan-panel').hidden, true);
    s.result(LABEL_B);
    assert.match(s.element('scan-f-name').value, /肯尼亚/);
    assert.doesNotMatch(s.element('scan-lines').innerHTML, /耶加雪菲/);
});

await test('consecutive album scans replace the label fields and matching beans', async () => {
    const s = await readyScanner({ listBeans: () => [
        { id: 1, name: '耶加雪菲 水洗', brand: '小满咖啡', process: '水洗', variety: '瑰夏' },
        { id: 2, name: '肯尼亚 日晒', brand: 'Torch', process: '日晒', variety: 'SL28' },
    ] });
    s.album(); s.images[0].resolve({ width: 640, height: 480, close() {} }); await s.flush();
    s.result(LABEL_A);
    assert.match(s.element('scan-match').innerHTML, /耶加雪菲/);
    s.album();
    assert.equal(s.element('scan-match').innerHTML, '');
    s.images[1].resolve({ width: 640, height: 480, close() {} }); await s.flush();
    s.result(LABEL_B);
    assert.match(s.element('scan-match').innerHTML, /肯尼亚/);
    assert.doesNotMatch(s.element('scan-match').innerHTML, /耶加雪菲/);
    assert.equal(s.element('scan-f-brand').value, 'Torch');
    assert.equal(s.element('scan-f-estate').value, '肯尼亚');
    assert.doesNotMatch(s.element('scan-f-note').value, /100g/);
});

await test('retake discards a late album decode without unlocking the new scan', async () => {
    const s = await readyScanner(); s.album();
    s.element('scan-again').onclick(); s.read();
    let released = false;
    s.images[0].resolve({ width: 640, height: 480, close() { released = true; } }); await s.flush();
    assert.equal(released, true);
    assert.equal(s.element('scan-read').disabled, true);
    assert.equal(s.workers[0].sent.filter(m => m.t === 'read').length, 1);
    s.result(LABEL_B);
    assert.match(s.element('scan-f-name').value, /肯尼亚/);
});

await test('saving after scanning two labels stores only the second bean', async () => {
    const saved = [];
    const s = await readyScanner({ addBean: async bean => saved.push(bean), toast() {} });
    s.read(); s.result(LABEL_A); s.close();
    s.open(); s.cameras[1].resolve(s.media()); await s.flush();
    s.read(); s.result(LABEL_B);
    await s.element('scan-save').onclick();
    assert.equal(saved.length, 1);
    assert.match(saved[0].name, /肯尼亚/);
    assert.equal(saved[0].brand, 'Torch');
    assert.equal(saved[0].process, '日晒');
    assert.equal(saved[0].estate, '肯尼亚');
    assert.doesNotMatch(saved[0].note, /100g/);
});

await test('a warm scanner reopened after a scan leaves camera-start status', async () => {
    const s = await scanner(); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false });
    s.read();
    const read = s.workers[0].sent.find(m => m.t === 'read');
    s.workers[0].emit({ t: 'lines', id: read.id, lines: [], ms: 600 });
    s.close(); s.open();
    s.cameras[1].resolve(s.media()); await s.flush();
    assert.match(s.status(), /就绪|对准/);
});

await test('camera permission resolving after close cannot replace reopened camera', async () => {
    const s = await scanner(); s.open(); s.close(); s.open();
    const current = s.media(), stale = s.media();
    s.cameras[1].resolve(current); await s.flush();
    s.cameras[0].resolve(stale); await s.flush();
    assert.equal(s.element('scan-video').srcObject, current);
    assert.equal(stale.track.stopped, true);
    assert.equal(current.track.stopped, false);
});

await test('closing during a read ignores its late result after reopening', async () => {
    const s = await scanner(); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false }); s.read();
    const read = s.workers[0].sent.find(m => m.t === 'read');
    s.close(); s.open(); s.cameras[1].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'lines', id: read.id, lines: [], ms: 3000 });
    assert.doesNotMatch(s.status(), /没识别到/);
});

await test('warm-up has a watchdog, so opening cannot hang forever', async () => {
    const s = await scanner(); s.open();
    assert.ok([...s.timers.values()].some(t => t.ms >= 10000), 'no warm-up watchdog');
    assert.equal(s.workers[0].sent.filter(m => m.t === 'warm').length, 1);
});

await test('a timed-out read recovers once, then offers an explicit retry', async () => {
    const s = await scanner(); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false }); s.read(); s.tick(15000);
    assert.equal(s.workers[0].terminated, true);
    assert.equal(s.workers.length, 2);
    s.workers[0].emit({ t: 'ready', gpu: true });
    assert.match(s.status(), /重启/);
    s.workers[1].emit({ t: 'ready', gpu: false });
    s.read(); s.tick(15000);
    assert.equal(s.workers.length, 2, 'no infinite restart loop');
    assert.match(s.status(), /点击识别重试/);
    s.read(); assert.equal(s.workers.length, 3);
});

await test('an album read stays busy until its worker result, blocking extra reads', async () => {
    const s = await scanner(); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false });
    s.album(); s.read(); s.album();
    assert.equal(s.images.length, 1);
    s.images[0].resolve({ width: 640, height: 480, close() {} }); await s.flush();
    s.read(); s.album();
    assert.equal(s.workers[0].sent.filter(m => m.t === 'read').length, 1);
    const read = s.workers[0].sent.find(m => m.t === 'read');
    s.workers[0].emit({ t: 'lines', id: read.id, lines: [], ms: 600 });
    s.read(); assert.equal(s.workers[0].sent.filter(m => m.t === 'read').length, 2);
});

await test('a failed preview cannot unlock a pending camera read', async () => {
    const s = await scanner(); s.open();
    s.cameras[0].resolve(s.media()); await s.flush();
    s.workers[0].emit({ t: 'ready', gpu: false }); s.tick(0);
    const preview = s.workers[0].sent.find(m => m.t === 'detect');
    s.read();
    s.workers[0].emit({ t: 'error', id: preview.id, message: 'preview failed' });
    s.read();
    assert.equal(s.workers[0].sent.filter(m => m.t === 'read').length, 1);
    assert.equal(s.element('scan-read').disabled, true);
    assert.equal(s.status(), '识别中…');
});
