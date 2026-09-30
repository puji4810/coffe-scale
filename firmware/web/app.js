// coffee-scale web app — mirror client for the on-device UI.
//   BLE notifications to the scale: 20-byte state frames @20 Hz out,
//   tiny command writes in (scale_proto/proto.hpp is the wire ABI; WASM
//   decodes it). The canvas runs the device's real LVGL UI
//   (scale_screen.wasm), so the mirror is pixel-identical to the ST7789.
//   Chart is vendored uPlot.

import ScaleScreenFactory from './dist/scale_screen.mjs';
import { ScaleChart } from './chart.js';
import { ScaleLink } from './ble.js';
import * as DB from './store.js';

if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js');
}

const Screen = await ScaleScreenFactory();
Screen.init();

// --- canvas ----------------------------------------------------------------

const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const W = Screen.width(), H = Screen.height();

function blit() {
    const px = Screen.framebuffer();
    ctx.putImageData(new ImageData(
        new Uint8ClampedArray(px.buffer, px.byteOffset, px.byteLength),
        W, H), 0, 0);
}

// --- state ------------------------------------------------------------------

const $ = id => document.getElementById(id);
const connEl = $('conn');
const bleBtn = $('btn-ble');

// --- snapshot -> UI ----------------------------------------------------------

const chart = new ScaleChart($('chart'));
const UNIT_LABEL = ['g', 'oz'];
const MODE_LABEL = ['WEIGH', 'BREW'];

let lastGrams = 0;

function showFrame(m, tMs) {
    Screen.update(m);                     // canvas mirror
    const s = m.snap;
    lastGrams = s.grams;
    chart.push(tMs, s.grams, s.flowGps);
    $('v-weight').textContent = `${m.displayValue.toFixed(1)} ${UNIT_LABEL[s.unit]}`;
    $('v-flow').textContent = `${s.flowGps.toFixed(1)} g/s`;
    const dose = doseVal();
    $('v-ratio').textContent =
        dose > 0 ? `1 : ${(s.grams / dose).toFixed(1)}` : '–';
    const ms = s.timerMs;
    $('v-timer').textContent = `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}.${Math.floor(ms / 100) % 10}`;
    $('stable-dot').style.background = s.stable ? '#3dd68c' : '#3a3a3a';
    $('v-flags').textContent =
        `${MODE_LABEL[s.mode]}${s.tared ? ' · T' : ''}${s.calibrated ? '' : ' · UNCAL'}`;
    $('v-batt').textContent = m.batteryPct < 0 ? '–'
        : `${m.charging ? '+' : ''}${m.batteryPct}%`;
    const bf = $('batt-fill');
    if (m.batteryPct >= 0) {
        const pct = Math.min(m.batteryPct, 100);
        bf.style.width = `${pct}%`;
        bf.style.background = m.charging ? 'var(--amber)'
            : pct > 50 ? 'var(--green)' : pct > 20 ? 'var(--amber)'
            : 'var(--red)';
    } else {
        bf.style.width = '0%';
    }
    // level bubble — pitch/roll in deg, ~0.8 px/deg, clamped inside the ring
    const dx = Math.max(-8, Math.min(8, s.rollDeg * 0.8));
    const dy = Math.max(-8, Math.min(8, s.pitchDeg * 0.8));
    const tilt = Math.max(Math.abs(s.pitchDeg), Math.abs(s.rollDeg));
    const dot = $('bubble-dot');
    dot.style.transform = `translate(${dx}px, ${dy}px)`;
    dot.style.background = !s.stable ? 'var(--line2)'
        : tilt < 2 ? 'var(--green)' : 'var(--amber)';
    $('v-tilt').textContent = s.stable ? `${tilt.toFixed(1)}°` : 'moving';
    $('cal-state').textContent = s.calibrated ? 'calibrated' : 'uncalibrated';
    recorder.onFrame(m, tMs);
}

// --- commands ----------------------------------------------------------------

// proto opcodes — components/scale_proto/proto.hpp
const OP = {
    tare: 1, timerToggle: 2, timerReset: 3, mode: 4, unit: 5,
    calZero: 6, calSpan: 7, sleep: 8,
};

function cmd(op, arg = 0) {
    link.send(Screen.encodeCommand(op, arg))
        .catch(() => setConn('write failed — reconnecting?'));
}

$('btn-tare').onclick = () => cmd(OP.tare);
$('btn-long').onclick = () => cmd(OP.timerToggle);
$('btn-mode').onclick = () => cmd(OP.mode);
$('sel-unit').onchange = e => cmd(OP.unit, +e.target.value);
$('btn-calzero').onclick = () => cmd(OP.calZero);
$('btn-calspan').onclick = () =>
    cmd(OP.calSpan, Math.round(parseFloat($('cal-mass').value || '100') * 100));
$('btn-sleep').onclick = () => {
    if (confirm('进入低功耗？秤会关屏、关蓝牙；双击秤体或按 MODE 键唤醒。')) {
        cmd(OP.sleep);
    }
};
addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT') return;
    if (e.key === 't') cmd(OP.tare);
    else if (e.key === 'l') cmd(OP.timerToggle);
    else if (e.key === 'm') cmd(OP.mode);
});

// --- bluetooth link (mirror) --------------------------------------------------

function setConn(txt, live) {
    connEl.textContent = txt;
    connEl.className = live ? 'live' : '';
}

const link = new ScaleLink({
    bluetooth: navigator.bluetooth,
    uuids: Screen.bleUuids(),
    onFrame: (bytes, t) => {
        const m = Screen.decodeFrame(new Uint8Array(
            bytes.buffer, bytes.byteOffset, bytes.byteLength));
        if (m) showFrame(m, t);
    },
    onStatus: (kind, text) => {
        setConn(text, kind === 'live');
        bleBtn.textContent = (kind === 'live' || kind === 'reconnecting')
            ? 'Disconnect' : 'Connect scale';
        if (kind === 'live') chart.clear();
    },
});

bleBtn.onclick = async () => {
    if (link.connected || link.status === 'reconnecting') {
        await link.disconnect();
        return;
    }
    try { await link.connect(); }
    catch { setConn('connect failed / cancelled'); }
};

// permitted devices reconnect on their own when supported
link.autoConnect();

// Chrome drops advertisement watches while the window is hidden —
// re-arm the reconnect when we come back.
document.addEventListener('visibilitychange', () => {
    if (!document.hidden) link.resume();
});
addEventListener('focus', () => link.resume());

// --- recording ---------------------------------------------------------------
// frames[] holds the raw decoded frames for the jsonl download; on stop()
// the same data is folded into a compact brew row (t/w/f arrays) and
// saved into the library bound to the selected bean.

const recorder = {
    frames: [], on: false, auto: false, armed: true,
    onFrame(m, t) {
        const s = m.snap;
        // auto record while the brew timer runs; re-arms on idle
        if (s.timerState === 0) this.armed = true;
        if (!this.on && this.armed && s.mode === 1 && s.timerState === 1) {
            this.start(true);
        }
        if (this.on && this.auto && s.timerState === 0) this.stop();
        if (this.on) this.frames.push({ t: Math.round(t), ...m });
    },
    start(auto = false) {
        this.on = true; this.auto = auto; this.frames = [];
        this.startedAt = Date.now();
        $('btn-rec').classList.add('rec-on');
        $('rec-info').textContent = auto ? 'auto-recording (brew timer)' : 'recording';
    },
    stop() {
        this.on = false; this.armed = false;
        $('btn-rec').classList.remove('rec-on');
        $('rec-info').textContent = `${this.frames.length} frames`;
        $('btn-dl').disabled = this.frames.length === 0;
        saveBrew(this.frames, this.startedAt)
            .catch(e => { $('rec-info').textContent = `save failed: ${e}`; });
    },
};

$('btn-rec').onclick = () =>
    recorder.on ? recorder.stop() : (recorder.armed = true, recorder.start(false));

$('btn-dl').onclick = () => {
    const body = recorder.frames.map(f => JSON.stringify(f)).join('\n');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([body], { type: 'application/jsonl' }));
    a.download = `coffee-${new Date().toISOString().replace(/[:.]/g, '-')}.jsonl`;
    a.click();
    URL.revokeObjectURL(a.href);
};

$('btn-clear').onclick = () => { chart.clear(); $('btn-live').hidden = true; };

$('btn-live').onclick = () => { chart.resume(); $('btn-live').hidden = true; };

// --- brew library -----------------------------------------------------------

const beanSel = $('sel-bean');
const doseIn = $('dose');

function doseVal() { return parseFloat(doseIn.value) || 0; }
function curBeanId() {
    const v = beanSel.value;
    return v === '' ? null : +v;
}

// Persisted brew defaults — dose follows the selected bean's own default,
// otherwise the last-typed value sticks across reloads.
doseIn.value = localStorage.getItem('dose') || '18';
doseIn.onchange = () => localStorage.setItem('dose', doseIn.value);

$('btn-weigh').onclick = () => {
    doseIn.value = lastGrams.toFixed(1);
    doseIn.onchange();
};

async function saveBrew(frames, startedAt) {
    if (frames.length < 10) return;         // taps/blips aren't brews
    const t0 = frames[0].t;
    const t = [], w = [], f = [];
    for (const m of frames) {
        t.push(+((m.t - t0) / 1000).toFixed(2));
        w.push(+m.snap.grams.toFixed(2));
        f.push(+m.snap.flowGps.toFixed(2));
    }
    const brew = {
        beanId: curBeanId(),
        date: new Date(startedAt).toISOString(),
        durationS: +t[t.length - 1].toFixed(1),
        dose: doseVal(),
        liquid: +w[w.length - 1].toFixed(1),
        rating: 0, fav: false, t, w, f,
    };
    await DB.brews.add(brew);
    let beanTxt = '';
    if (brew.beanId !== null) {
        const b = (await DB.beans.list()).find(b => b.id === brew.beanId);
        if (b) {
            beanTxt = ` · ${b.name}`;
            if (b.dose !== brew.dose) { b.dose = brew.dose; await DB.beans.update(b); }
        }
    }
    $('rec-info').textContent =
        `saved · ${brew.liquid} g in ${fmtDur(brew.durationS)}${beanTxt}`;
    renderLibrary();
}

function fmtDur(s) {
    return `${Math.floor(s / 60)}:${String(Math.round(s) % 60).padStart(2, '0')}`;
}

let beanCache = [];
function beanName(id) {
    const b = beanCache.find(b => b.id === id);
    return b ? b.name : '?';
}

function stars(rating, id) {
    let h = '<span class="stars">';
    for (let i = 1; i <= 5; i++) {
        h += `<span data-brew="${id}" data-n="${i}" class="${i <= rating ? 'on' : ''}">★</span>`;
    }
    return h + '</span>';
}

async function renderLibrary() {
    beanCache = await DB.beans.list();
    const list = await DB.brews.list();
    list.sort((a, b) => b.date < a.date ? -1 : 1);   // newest first

    // bean select keeps its value across re-renders
    const sel = beanSel.value;
    beanSel.innerHTML = '<option value="">no bean</option>' +
        beanCache.map(b => `<option value="${b.id}">${b.name}</option>`).join('');
    beanSel.value = sel;

    $('bean-list').innerHTML = beanCache.map(b =>
        `<div class="brew-row"><span class="who">${b.name}</span>` +
        `<span class="meta">${b.dose || '–'} g</span>` +
        `<button data-delbean="${b.id}">✕</button></div>`).join('');

    $('brew-list').innerHTML = list.length === 0
        ? '<div class="hint" style="padding:8px 2px">冲一次 brew，曲线会自动存在这里。</div>'
        : list.map(b =>
            `<div class="brew-row">` +
            `<button data-view="${b.id}">view</button>` +
            `<span class="who"><b>${beanName(b.beanId)}</b></span>` +
            `<span class="meta">${b.date.slice(5, 16).replace('T', ' ')}</span>` +
            `<span class="meta">${b.dose || '?'}→${b.liquid} g` +
            `${b.dose ? ` · 1:${(b.liquid / b.dose).toFixed(1)}` : ''}</span>` +
            stars(b.rating, b.id) +
            `<button class="fav ${b.fav ? 'on' : ''}" data-fav="${b.id}">♥</button>` +
            `<button data-delbrew="${b.id}">✕</button></div>`).join('');
}

$('brew-list').onclick = async e => {
    const el = e.target.closest('[data-view],[data-fav],[data-delbrew],[data-brew]');
    if (!el) return;
    if (el.dataset.brew) {                  // star rating
        const b = await brewOf(+el.dataset.brew);
        b.rating = (+el.dataset.n === b.rating) ? 0 : +el.dataset.n;
        await DB.brews.update(b);
    } else if (el.dataset.fav) {
        const b = await brewOf(+el.dataset.fav);
        b.fav = !b.fav;
        await DB.brews.update(b);
    } else if (el.dataset.delbrew) {
        await DB.brews.del(+el.dataset.delbrew);
    } else if (el.dataset.view) {
        const b = await brewOf(+el.dataset.view);
        chart.showSaved(b.t, b.w, b.f);
        $('btn-live').hidden = false;
    }
    renderLibrary();
};

async function brewOf(id) {
    return (await DB.brews.list()).find(b => b.id === id);
}

$('bean-list').onclick = async e => {
    const el = e.target.closest('[data-delbean]');
    if (!el) return;
    await DB.beans.del(+el.dataset.delbean);
    renderLibrary();
};

$('btn-addbean').onclick = async () => {
    const name = $('bean-name').value.trim();
    if (!name) return;
    $('bean-name').value = '';
    const id = await DB.beans.add(name, doseVal());
    beanSel.value = String(id);
    renderLibrary();
};

beanSel.onchange = async () => {
    const b = beanCache.find(b => b.id === curBeanId());
    if (b && b.dose) { doseIn.value = b.dose; doseIn.onchange(); }
};

// --- backup / clear -----------------------------------------------------------

$('btn-export').onclick = async () => {
    const body = JSON.stringify(await DB.exportAll(), null, 1);
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([body], { type: 'application/json' }));
    a.download = `coffee-backup-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
};

$('btn-import').onclick = () => {
    const inp = document.createElement('input');
    inp.type = 'file'; inp.accept = '.json';
    inp.onchange = async () => {
        try {
            const data = JSON.parse(await inp.files[0].text());
            const n = await DB.importAll(data);
            $('rec-info').textContent = `imported ${n.beans} beans, ${n.brews} brews`;
            renderLibrary();
        } catch (e) {
            $('rec-info').textContent = `import failed: ${e.message}`;
        }
    };
    inp.click();
};

$('btn-clearbrews').onclick = async () => {
    if (confirm('清空全部冲煮记录？（豆子保留）')) {
        await DB.brews.clear();
        renderLibrary();
    }
};

renderLibrary().catch(() => {});

// curve on/off — hidden keeps collecting in the background, re-showing
// flushes the whole backlog in one setData. Choice persists on the device.
const chartPanel = document.querySelector('.chart-panel');
const chartToggle = $('chart-toggle');
{
    const saved = localStorage.getItem('chart-on');
    if (saved !== null) chartToggle.checked = saved === '1';
}
function applyChartVis() {
    const on = chartToggle.checked;
    chartPanel.classList.toggle('off', !on);
    chart.setVisible(on);
    localStorage.setItem('chart-on', on ? '1' : '0');
}
chartToggle.onchange = applyChartVis;
applyChartVis();

addEventListener('resize', () => {
    if (chart.visible)
        chart.resize($('chart').clientWidth, $('chart').clientHeight);
});

// --- main loop -----------------------------------------------------------------

function frame() {
    if (Screen.pump()) blit();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
