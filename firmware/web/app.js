// coffee-scale web app — mirror client for the on-device UI.
//   BLE notifications to the scale: 20-byte state frames @20 Hz out,
//   tiny command writes in (scale_proto/proto.hpp is the wire ABI; WASM
//   decodes it). The canvas runs the device's real LVGL UI
//   (scale_screen.wasm), so the mirror is pixel-identical to the ST7789.
//   Chart is vendored uPlot.

import ScaleScreenFactory from './dist/scale_screen.mjs';
import { ScaleChart, brewPlot } from './chart.js';
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

$('btn-clear').onclick = () => chart.clear();

// --- drawer + pages ------------------------------------------------------------

const drawer = $('drawer'), backdrop = $('backdrop');
function setDrawer(open) {
    drawer.classList.toggle('open', open);
    backdrop.classList.toggle('on', open);
}
$('btn-menu').onclick = () => setDrawer(!drawer.classList.contains('open'));
backdrop.onclick = () => setDrawer(false);

const PAGES = ['weigh', 'beans', 'cal', 'data'];
function switchPage(name) {
    if (!PAGES.includes(name)) name = 'weigh';
    for (const p of PAGES) $(`page-${p}`).classList.toggle('active', p === name);
    document.querySelectorAll('#drawer nav a').forEach(a =>
        a.classList.toggle('active', a.dataset.page === name));
    if (location.hash !== `#${name}`) location.hash = name;
    setDrawer(false);
}
document.querySelectorAll('#drawer nav a').forEach(a =>
    a.onclick = () => switchPage(a.dataset.page));
addEventListener('hashchange', () => switchPage(location.hash.slice(1)));
switchPage(location.hash.slice(1) || 'weigh');

// touch: swipe right starting near the left edge opens the drawer;
// swipe left while it's open closes it. Vertical scroll unaffected.
let touchX0 = null, touchY0 = null;
addEventListener('touchstart', e => {
    touchX0 = e.touches[0].clientX; touchY0 = e.touches[0].clientY;
}, { passive: true });
addEventListener('touchend', e => {
    if (touchX0 === null) return;
    const dx = e.changedTouches[0].clientX - touchX0;
    const dy = e.changedTouches[0].clientY - touchY0;
    const sx = touchX0;
    touchX0 = null;
    if (Math.abs(dy) > Math.abs(dx) || Math.abs(dx) < 50) return;
    if (dx > 0 && sx < 40) setDrawer(true);
    else if (dx < 0 && drawer.classList.contains('open')) setDrawer(false);
}, { passive: true });

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
    renderBeans();
}

function fmtDur(s) {
    return `${Math.floor(s / 60)}:${String(Math.round(s) % 60).padStart(2, '0')}`;
}

let beanCache = [], brewCache = [];
const openBeans = new Set(), openBrews = new Set();

const esc = s => String(s ?? '').replace(/[&<>"]/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

function beanName(id) {
    const b = beanCache.find(b => b.id === id);
    return b ? b.name : 'unassigned';
}

function stars(rating, id) {
    let h = '<span class="stars">';
    for (let i = 1; i <= 5; i++) {
        h += `<span data-rate="${id}:${i}" class="${i <= rating ? 'on' : ''}">★</span>`;
    }
    return h + '</span>';
}

function brewMeta(w) {
    const ratio = w.dose ? ` · 1:${(w.liquid / w.dose).toFixed(1)}` : '';
    return `${w.dose || '?'}→${w.liquid} g${ratio} · ${fmtDur(w.durationS)}`;
}

function beanCard(b, bs) {
    const id = b ? b.id : 0;
    const open = openBeans.has(id);
    const meta = b ? `
      <div class="bean-meta">
        <label>name<input data-bf="name" data-bid="${b.id}" value="${esc(b.name)}"></label>
        <label>brand<input data-bf="brand" data-bid="${b.id}" value="${esc(b.brand)}"></label>
        <label>variety<input data-bf="variety" data-bid="${b.id}" value="${esc(b.variety)}"></label>
        <label>dose g<input type="number" step="0.1" min="0" data-bf="dose" data-bid="${b.id}" value="${b.dose || ''}"></label>
        <label class="wide">note<input data-bf="note" data-bid="${b.id}" value="${esc(b.note)}"></label>
        <button class="danger" data-delbean="${b.id}">delete bean</button>
      </div>` : '';
    const rows = bs.map(w => `
      <div class="brew ${openBrews.has(w.id) ? 'open' : ''}">
        <div class="brew-head" data-brewhead="${w.id}">
          <span class="tw">▸</span>
          <span class="meta">${w.date.slice(5, 16).replace('T', ' ')}</span>
          <span class="meta">${brewMeta(w)}</span>
          <span style="flex:1"></span>
          ${stars(w.rating, w.id)}
          <button class="fav ${w.fav ? 'on' : ''}" data-fav="${w.id}">♥</button>
        </div>
        <div class="brew-detail" data-bd="${w.id}" ${openBrews.has(w.id) ? '' : 'hidden'}></div>
      </div>`).join('')
        || '<div class="hint" style="padding:4px 2px">还没有冲煮记录。</div>';
    const sub = b ? esc([b.brand, b.variety].filter(Boolean).join(' · ')) : '';
    return `
    <div class="bean ${open ? 'open' : ''}">
      <div class="bean-head" data-beanhead="${id}">
        <span class="tw">▸</span><b>${b ? esc(b.name) : 'unassigned'}</b>
        <span class="meta">${sub}</span>
        <span style="flex:1"></span>
        <span class="meta">${bs.length} brews</span>
      </div>
      <div class="bean-body" ${open ? '' : 'hidden'}>${meta}${rows}</div>
    </div>`;
}

async function renderBeans() {
    beanCache = await DB.beans.list();
    brewCache = await DB.brews.list();
    brewCache.sort((a, b) => (a.date < b.date ? 1 : -1));   // newest first

    // weigh-page bean select keeps its value across re-renders
    const sel = beanSel.value;
    beanSel.innerHTML = '<option value="">no bean</option>' +
        beanCache.map(b => `<option value="${b.id}">${esc(b.name)}</option>`).join('');
    beanSel.value = sel;

    const groups = beanCache.map(b => ({
        b, bs: brewCache.filter(w => w.beanId === b.id),
    }));
    const loose = brewCache.filter(w =>
        w.beanId === null || !beanCache.some(b => b.id === w.beanId));
    if (loose.length) groups.push({ b: null, bs: loose });

    const acc = $('bean-accordion');
    acc.innerHTML = groups.length === 0
        ? '<div class="hint" style="padding:10px 2px">先加一支豆子；冲煮记录会自动归档到豆子下面。</div>'
        : groups.map(({ b, bs }) => beanCard(b, bs)).join('');

    // open brew details need a sized box — mount plots after the DOM exists
    for (const w of brewCache) {
        if (openBrews.has(w.id) && openBeans.has(w.beanId ?? 0)) mountBrewDetail(w);
    }
}

function mountBrewDetail(w) {
    const bd = document.querySelector(`.brew-detail[data-bd="${w.id}"]`);
    if (!bd || bd.dataset.mounted) return;
    bd.dataset.mounted = '1';
    bd.innerHTML = `
      <div class="bd-curve"></div>
      <div class="bd-data">
        <div><span class="v">${w.date.slice(0, 16).replace('T', ' ')}</span></div>
        <div>${w.dose || '?'} g → <span class="v">${w.liquid} g</span>` +
        `${w.dose ? ` · <span class="v">1:${(w.liquid / w.dose).toFixed(1)}</span>` : ''}</div>
        <div>${fmtDur(w.durationS)} · ${w.t.length} pts</div>
        <input class="note" data-note="${w.id}" placeholder="备注：风味、改进…" value="${esc(w.note || '')}">
        <div class="row" style="margin-top:2px">
          <button data-expbrew="${w.id}">export</button>
          <button class="danger" data-delbrew="${w.id}">delete</button>
        </div>
      </div>`;
    brewPlot(bd.querySelector('.bd-curve'), w.t, w.w, w.f);
}

function exportBrew(w) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(
        new Blob([JSON.stringify(w)], { type: 'application/json' }));
    a.download = `brew-${w.date.slice(0, 10)}-${beanName(w.beanId)}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
}

$('bean-accordion').onclick = async e => {
    const rate = e.target.closest('[data-rate]');
    if (rate) {
        const [id, n] = rate.dataset.rate.split(':').map(Number);
        const w = brewCache.find(w => w.id === id);
        w.rating = (n === w.rating) ? 0 : n;
        await DB.brews.update(w);
        renderBeans();
        return;
    }
    const fav = e.target.closest('[data-fav]');
    if (fav) {
        const w = brewCache.find(w => w.id === +fav.dataset.fav);
        w.fav = !w.fav;
        await DB.brews.update(w);
        renderBeans();
        return;
    }
    const exp = e.target.closest('[data-expbrew]');
    if (exp) {
        exportBrew(brewCache.find(w => w.id === +exp.dataset.expbrew));
        return;
    }
    const delB = e.target.closest('[data-delbrew]');
    if (delB) {
        const id = +delB.dataset.delbrew;
        openBrews.delete(id);
        await DB.brews.del(id);
        renderBeans();
        return;
    }
    const delBean = e.target.closest('[data-delbean]');
    if (delBean) {
        const id = +delBean.dataset.delbean;
        if (!confirm('删除这支豆子？它的冲煮记录会归入 unassigned。')) return;
        for (const w of brewCache.filter(w => w.beanId === id)) {
            w.beanId = null;
            await DB.brews.update(w);
        }
        openBeans.delete(id);
        await DB.beans.del(id);
        renderBeans();
        return;
    }
    const bh = e.target.closest('[data-brewhead]');
    if (bh) {
        const id = +bh.dataset.brewhead;
        openBrews.has(id) ? openBrews.delete(id) : openBrews.add(id);
        renderBeans();
        return;
    }
    const bnh = e.target.closest('[data-beanhead]');
    if (bnh) {
        const id = +bnh.dataset.beanhead;
        openBeans.has(id) ? openBeans.delete(id) : openBeans.add(id);
        renderBeans();
    }
};

// bean meta fields + per-brew note — saved on blur/enter
$('bean-accordion').onchange = async e => {
    const bf = e.target.closest('[data-bf]');
    if (bf) {
        const b = beanCache.find(b => b.id === +bf.dataset.bid);
        b[bf.dataset.bf] = bf.dataset.bf === 'dose'
            ? (parseFloat(bf.value) || 0) : bf.value;
        await DB.beans.update(b);
        renderBeans();                       // name/brand may be in the header
        return;
    }
    const nt = e.target.closest('[data-note]');
    if (nt) {
        const w = brewCache.find(w => w.id === +nt.dataset.note);
        w.note = nt.value;
        await DB.brews.update(w);
    }
};

$('btn-addbean').onclick = async () => {
    const name = $('bean-name').value.trim();
    if (!name) return;
    $('bean-name').value = '';
    const id = await DB.beans.add({ name, dose: doseVal() });
    beanSel.value = String(id);
    openBeans.add(id);
    renderBeans();
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
            renderBeans();
        } catch (e) {
            $('rec-info').textContent = `import failed: ${e.message}`;
        }
    };
    inp.click();
};

$('btn-clearbrews').onclick = async () => {
    if (confirm('清空全部冲煮记录？（豆子保留）')) {
        await DB.brews.clear();
        renderBeans();
    }
};

renderBeans().catch(() => {});

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
