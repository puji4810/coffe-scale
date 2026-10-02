// Label scanner — phone camera → live text-box overlay → tap to OCR a frame
// → parsed bean fields. All inference runs in ocr_worker.js (WebGPU/WASM,
// lazy-loaded); this file is DOM + orchestration only.

import { parseBeanLabel } from './bean_parse.js';

const $ = id => document.getElementById(id);

let hooks = {};
let worker = null, workerReady = false, workerErr = '';
let stream = null, track = null, wakeLock = null;
let detTimer = 0, detBusy = false, readBusy = false, reqId = 0;
let open_ = false, focusedField = 'scan-f-name';
const reqDims = new Map();   // request id → grabbed frame {w,h}

export function initScan(h) {
    hooks = h;
    $('btn-scan').onclick = open;
    $('scan-close').onclick = close;
    $('scan-read').onclick = () => read();
    $('scan-again').onclick = () => { hidePanel(); scheduleDet(0); };
    $('scan-save').onclick = save;
    $('scan-torch').onclick = toggleTorch;
    $('scan-file').onclick = () => {
        const inp = document.createElement('input');
        inp.type = 'file'; inp.accept = 'image/*';
        inp.onchange = () => inp.files[0] && readFile(inp.files[0]);
        inp.click();
    };
    // field focus tracking — tapping an OCR line fills the focused input
    for (const f of ['name', 'brand', 'process', 'variety', 'estate', 'note'])
        $(`scan-f-${f}`).addEventListener('focus', () => focusedField = `scan-f-${f}`);
    // raw OCR line → tap to fill focused field; matched bean → select it
    $('scan-lines').addEventListener('click', e => {
        const b = e.target.closest('[data-txt]');
        if (b) $(focusedField).value = b.dataset.txt;
    });
    $('scan-match').addEventListener('click', e => {
        const b = e.target.closest('[data-bean]');
        if (b) { hooks.selectBean(+b.dataset.bean); close(); }
    });
    document.addEventListener('keydown', e => {
        if (open_ && e.key === 'Escape') close();
    });
    addEventListener('resize', () => { if (open_) sizeFx(); });
}

// --- lifecycle ---------------------------------------------------------------

function open() {
    open_ = true;
    $('scan-ov').hidden = false;
    $('scan-read').disabled = false;
    $('scan-torch').hidden = true;
    hidePanel();
    setStatus('启动相机…');
    startCamera();
    warmWorker();
    navigator.wakeLock?.request('screen').then(l => wakeLock = l).catch(() => {});
}

function close() {
    open_ = false;
    clearTimeout(detTimer); detBusy = false;
    stream?.getTracks().forEach(t => t.stop());
    stream = track = null;
    $('scan-video').srcObject = null;
    $('scan-ov').hidden = true;
    wakeLock?.release().catch(() => {}); wakeLock = null;
}

async function startCamera() {
    try {
        stream = await navigator.mediaDevices.getUserMedia({
            audio: false,
            video: {
                facingMode: { ideal: 'environment' },
                width: { ideal: 1920 }, height: { ideal: 1080 },
            },
        });
    } catch (e) {
        setStatus(e.name === 'NotAllowedError'
            ? '相机被拒绝授权 — 可用下方「相册」选照片识别'
            : `相机不可用(${e.name}) — 可用「相册」选照片`);
        $('scan-read').disabled = true;
        return;
    }
    const video = $('scan-video');
    video.srcObject = stream;
    await video.play().catch(() => {});
    track = stream.getVideoTracks()[0];
    if (track?.getCapabilities?.().torch) $('scan-torch').hidden = false;
    sizeFx();
    scheduleDet(0);
}

async function toggleTorch() {
    const on = $('scan-torch').classList.toggle('on');
    try { await track.applyConstraints({ advanced: [{ torch: on }] }); }
    catch { $('scan-torch').classList.toggle('on', !on); }
}

function warmWorker() {
    if (worker) return;
    worker = new Worker('./ocr_worker.js', { type: 'module' });
    worker.onmessage = e => {
        const m = e.data;
        if (m.t === 'progress') {
            setStatus(m.building ? `加载模型…(${m.building})`
                : `下载模型 ${(m.got / 1048576).toFixed(1)} MB` +
                  (m.total ? ` / ${(m.total / 1048576).toFixed(0)} MB` : ''));
        } else if (m.t === 'ready') {
            workerReady = true;
            setStatus(`就绪 (${m.gpu ? 'WebGPU' : 'WASM'}) — 对准标签会自动框出文字`);
        } else if (m.t === 'boxes') {
            detBusy = false;
            const d = reqDims.get(m.id); reqDims.delete(m.id);
            if (d) drawQuads(m.boxes.map(b => b.pts), d, 'rgba(232,163,61,.9)');
            scheduleDet();
        } else if (m.t === 'lines') {
            readBusy = false;
            showResult(m.lines, m.ms, reqDims.get(m.id));
            reqDims.delete(m.id);
        } else if (m.t === 'error') {
            detBusy = readBusy = false;
            if (m.stage === 'warm') workerErr = m.message;
            setStatus(`出错：${m.message}`);
        }
    };
    worker.postMessage({ t: 'warm' });
}

// --- live detection -----------------------------------------------------------

function sizeFx() {
    const v = $('scan-video'), fx = $('scan-fx');
    fx.width = v.clientWidth; fx.height = v.clientHeight;
}

// cover-fit: frame px (of the grabbed frame dims) → overlay css px
function toFx(pts, dims) {
    const v = $('scan-video');
    const cw = v.clientWidth, ch = v.clientHeight;
    const s = Math.max(cw / dims.w, ch / dims.h);
    const ox = (cw - dims.w * s) / 2, oy = (ch - dims.h * s) / 2;
    return pts.map(([x, y]) => [x * s + ox, y * s + oy]);
}

function drawQuads(quads, dims, color) {
    const fx = $('scan-fx'), ctx = fx.getContext('2d');
    ctx.clearRect(0, 0, fx.width, fx.height);
    if (!dims) return;
    ctx.lineWidth = 2; ctx.strokeStyle = color;
    ctx.fillStyle = 'rgba(232,163,61,.12)';
    for (const q of quads) {
        const p = toFx(q, dims);
        ctx.beginPath();
        ctx.moveTo(p[0][0], p[0][1]);
        for (let i = 1; i < 4; i++) ctx.lineTo(p[i][0], p[i][1]);
        ctx.closePath(); ctx.stroke(); ctx.fill();
    }
}

function scheduleDet(ms = 650) {
    clearTimeout(detTimer);
    detTimer = setTimeout(detTick, ms);
}

// grab one video frame, longest side ≤ `limit`
function grab(limit) {
    const v = $('scan-video');
    const vw = v.videoWidth, vh = v.videoHeight;
    if (!vw || !vh) return null;
    const r = Math.min(1, limit / Math.max(vw, vh));
    const w = Math.round(vw * r), h = Math.round(vh * r);
    const c = $('scan-cap');
    c.width = w; c.height = h;
    const ctx = c.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(v, 0, 0, w, h);
    return { img: ctx.getImageData(0, 0, w, h), w, h };
}

function detTick() {
    if (!open_) return;
    if (!workerReady || detBusy || readBusy || !track) { scheduleDet(700); return; }
    const f = grab(960);
    if (!f) { scheduleDet(700); return; }
    detBusy = true;
    const id = ++reqId;
    reqDims.set(id, { w: f.w, h: f.h });
    worker.postMessage({ t: 'detect', id, w: f.w, h: f.h,
                         rgba: f.img.data.buffer }, [f.img.data.buffer]);
}

// --- recognize ----------------------------------------------------------------

async function read() {
    if (readBusy) return;
    if (!workerReady) { setStatus(workerErr || '模型还在加载，稍等…'); return; }
    const f = grab(1600);
    if (!f) { setStatus('相机还没出画面'); return; }
    readBusy = true;
    setStatus('识别中…');
    clearTimeout(detTimer);
    const id = ++reqId;
    reqDims.set(id, { w: f.w, h: f.h });
    worker.postMessage({ t: 'read', id, w: f.w, h: f.h,
                         rgba: f.img.data.buffer }, [f.img.data.buffer]);
}

async function readFile(file) {
    try {
        const bmp = await createImageBitmap(file);
        const r = Math.min(1, 1600 / Math.max(bmp.width, bmp.height));
        const w = Math.round(bmp.width * r), h = Math.round(bmp.height * r);
        const c = $('scan-cap');
        c.width = w; c.height = h;
        const ctx = c.getContext('2d', { willReadFrequently: true });
        ctx.drawImage(bmp, 0, 0, w, h);
        bmp.close();
        if (!workerReady) { setStatus('模型还在加载…'); return; }
        readBusy = true; setStatus('识别中…');
        clearTimeout(detTimer);
        const id = ++reqId;
        reqDims.set(id, { w, h });
        const img = ctx.getImageData(0, 0, w, h);
        worker.postMessage({ t: 'read', id, w, h,
                             rgba: img.data.buffer }, [img.data.buffer]);
    } catch (e) { setStatus(`图片读取失败：${e.message}`); }
}

// --- result panel ---------------------------------------------------------------

const FIELDS = ['name', 'brand', 'process', 'variety', 'estate', 'note'];

function showResult(lines, ms, dims) {
    setStatus(lines.length ? `识别到 ${lines.length} 行文字 (${ms} ms)` :
                             '没识别到文字，凑近一点或改善光线');
    if (!lines.length) { scheduleDet(400); return; }
    const { fields, matches, used } =
        parseBeanLabel(lines, { brands: hooks.getBrands(), beans: hooks.listBeans() });
    for (const f of FIELDS) $(`scan-f-${f}`).value = fields[f] || '';

    const tagOf = new Map(used.map(u => [u.line, u.as]));
    $('scan-lines').innerHTML = lines.map(l =>
        `<button type="button" class="scan-line ${tagOf.get(l) ? 'tagged' : ''}"
                 data-txt="${esc(l.text)}">${esc(l.text)}
           ${tagOf.get(l) ? `<i>${asLabel(tagOf.get(l))}</i>` : ''}</button>`).join('');
    $('scan-match').innerHTML = matches.map(m =>
        `<button type="button" class="scan-line match" data-bean="${m.bean.id}">
           已有：${esc(m.bean.name)}${m.bean.brand ? ' · ' + esc(m.bean.brand) : ''}</button>`
    ).join('');
    drawQuads(lines.map(l => l.quad), dims, 'rgba(61,214,140,.9)');
    $('scan-panel').hidden = false;
}

const asLabel = a => ({ name: '名', brand: '牌', process: '处', variety: '种',
                        estate: '园', note: '注' }[a] || '');
const esc = s => String(s ?? '').replace(/[&<>"]/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function hidePanel() { $('scan-panel').hidden = true; }
function setStatus(s) { $('scan-status').textContent = s; }

async function save() {
    const f = {};
    for (const k of FIELDS) f[k] = $(`scan-f-${k}`).value.trim();
    if (!f.name) { setStatus('名称不能为空 — 点下面识别出的行可以快速填入'); return; }
    await hooks.addBean(f);
    hooks.toast(`已存豆：${f.name}`);
    close();
}
