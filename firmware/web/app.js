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

// --- helpers ----------------------------------------------------------------

const $ = id => document.getElementById(id);

let toastTimer = null;
function toast(msg) {
    const el = $('toast');
    el.textContent = msg;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 3000);
}

// confirm-twice: first click arms the button (red, new label) for 3 s;
// a second click inside the window runs the action, else it reverts.
function armConfirm(btn, confirmLabel, action) {
    if (btn.dataset.armed) {
        clearTimeout(+btn.dataset.armT);
        delete btn.dataset.armed;
        btn.classList.remove('arming');
        btn.textContent = btn.dataset.origLabel;
        action();
        return;
    }
    btn.dataset.origLabel = btn.textContent;
    btn.dataset.armed = '1';
    btn.classList.add('arming');
    btn.textContent = confirmLabel;
    btn.dataset.armT = String(setTimeout(() => {
        delete btn.dataset.armed;
        btn.classList.remove('arming');
        btn.textContent = btn.dataset.origLabel;
    }, 3000));
}

// --- state ------------------------------------------------------------------

const UNIT_LABEL = ['g', 'oz'];
const TIMER_KEY_LABEL = ['开始计时', '暂停', '继续'];

let live = false;
let lastGrams = 0;
let lastTimerMs = 0;
let lastTimerState = 0;
let calDone = { zero: false, span: false };

const chart = new ScaleChart($('chart'));

// --- device-command disabled state -------------------------------------------

function setLive(v) {
    live = v;
    $('bezel').classList.toggle('live', v);
    for (const id of ['btn-tare', 'btn-long', 'btn-weigh', 'btn-sleep',
                      'btn-calzero', 'btn-calspan']) {
        $(id).disabled = !v;
    }
    document.querySelectorAll('.seg button').forEach(b => b.disabled = !v);
    $('btn-reset').disabled = !(v && lastTimerState !== 0);
    $('cal-off').hidden = v;
}

// --- snapshot -> UI ----------------------------------------------------------

function segSet(seg, v) {
    seg.dataset.active = String(v);
    seg.querySelectorAll('button').forEach(b =>
        b.setAttribute('aria-checked', String(+b.dataset.v === v)));
}

function showFrame(m, tMs) {
    Screen.update(m);                     // canvas mirror
    const s = m.snap;
    lastGrams = s.grams;
    lastTimerMs = s.timerMs;
    lastTimerState = s.timerState;
    chart.push(tMs, s.grams, s.flowGps);

    // instrument strip
    $('stable-dot').classList.toggle('on', s.stable);
    $('v-flags').textContent = s.stable ? '稳定' : '变动中';
    if (m.batteryPct < 0) {
        $('v-batt').textContent = '–';
        $('batt-fill').style.width = '0%';
    } else {
        $('v-batt').textContent = m.charging ? '充电中' : `${m.batteryPct}%`;
        const pct = Math.min(m.batteryPct, 100);
        const bf = $('batt-fill');
        bf.style.width = `${pct}%`;
        bf.style.background = m.charging ? 'var(--amber)'
            : pct > 50 ? 'var(--green)' : pct > 20 ? 'var(--amber)'
            : 'var(--red)';
    }
    const dx = Math.max(-8, Math.min(8, s.rollDeg * 0.8));
    const dy = Math.max(-8, Math.min(8, s.pitchDeg * 0.8));
    const tilt = Math.max(Math.abs(s.pitchDeg), Math.abs(s.rollDeg));
    const dot = $('bubble-dot');
    dot.style.transform = `translate(${dx}px, ${dy}px)`;
    dot.style.background = !s.stable ? 'var(--dim)'
        : tilt < 2 ? 'var(--green)' : 'var(--amber)';
    $('v-tilt').textContent = s.stable ? `${tilt.toFixed(1)}°` : '变动中';
    $('cal-state').textContent = s.calibrated ? '已校准' : '未校准';
    $('cal-state').classList.toggle('warn', !s.calibrated);
    $('cal-chip').textContent = s.calibrated ? '已校准' : '未校准';
    $('cal-chip').className = `chip ${s.calibrated ? 'ok' : 'warn'}`;

    // keys / segments reflect the frame — the device is the source of truth
    $('btn-long').textContent = TIMER_KEY_LABEL[s.timerState] || '开始计时';
    $('btn-reset').disabled = !(live && s.timerState !== 0);
    segSet($('seg-mode'), s.mode);
    segSet($('seg-unit'), s.unit);

    const dose = doseVal();
    $('v-ratio').textContent =
        dose > 0 ? `1 : ${(s.grams / dose).toFixed(1)}` : '–';

    updateChartHint();
    recorder.updateStatus(s);
    recorder.onFrame(m, tMs);
}

// empty-chart hint — offline invite, or "waiting" while live with no data
function updateChartHint() {
    const el = $('chart-empty');
    if (chart.x.length) { el.hidden = true; return; }
    el.hidden = false;
    el.textContent = live ? '等待数据…' : '连接秤后显示实时曲线';
}
function clearChart() { chart.clear(); updateChartHint(); }

// screen-reader summary, at most once per second
setInterval(() => {
    if (!live) { $('sr-live').textContent = ''; return; }
    const ms = lastTimerMs;
    $('sr-live').textContent =
        `重量 ${lastGrams.toFixed(1)} g，计时 ` +
        `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`;
}, 1000);

// --- commands ----------------------------------------------------------------

// proto opcodes — components/scale_proto/proto.hpp
const OP = {
    tare: 1, timerToggle: 2, timerReset: 3, mode: 4, unit: 5,
    calZero: 6, calSpan: 7, sleep: 8,
};

function cmd(op, arg = 0) {
    return link.send(Screen.encodeCommand(op, arg))
        .catch(() => toast('指令未送达，正在重连'));
}

$('btn-tare').onclick = () => cmd(OP.tare);
$('btn-long').onclick = () => cmd(OP.timerToggle);
$('btn-reset').onclick = () => cmd(OP.timerReset);
$('btn-sleep').onclick = () =>
    armConfirm($('btn-sleep'), '确认关机？', () => cmd(OP.sleep));

// segmented controls — mode toggles (OP.mode), unit takes arg 0/1
$('seg-mode').addEventListener('click', e => {
    const b = e.target.closest('button');
    if (!b || b.disabled) return;
    if (b.getAttribute('aria-checked') !== 'true') cmd(OP.mode);
});
$('seg-unit').addEventListener('click', e => {
    const b = e.target.closest('button');
    if (!b || b.disabled) return;
    if (b.getAttribute('aria-checked') !== 'true') cmd(OP.unit, +b.dataset.v);
});

// calibration wizard — step 1 then step 2, ✓ after each send resolves
function calStepDone() {
    const z = $('step-zero'), sp = $('step-span');
    z.classList.toggle('done', calDone.zero);
    z.classList.toggle('current', !calDone.zero);
    sp.classList.toggle('current', calDone.zero && !calDone.span);
    sp.classList.toggle('done', calDone.span);
}
$('btn-calzero').onclick = async () => {
    try {
        await link.send(Screen.encodeCommand(OP.calZero, 0));
        calDone.zero = true;
        calStepDone();
        toast('零点已记录');
    } catch { toast('指令未送达，正在重连'); }
};
$('btn-calspan').onclick = async () => {
    try {
        await link.send(Screen.encodeCommand(OP.calSpan,
            Math.round(parseFloat($('cal-mass').value || '100') * 100)));
        calDone.span = true;
        calStepDone();
        toast('校准完成');
    } catch { toast('指令未送达，正在重连'); }
};

addEventListener('keydown', e => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const t = e.target;
    if (t.closest('input, select, textarea') || t.isContentEditable) return;
    if (!live) return;
    if (e.key === 't') cmd(OP.tare);
    else if (e.key === 'l') cmd(OP.timerToggle);
    else if (e.key === 'm') cmd(OP.mode);
});

// --- bluetooth link -----------------------------------------------------------

const bleBtn = $('btn-ble');
const offMsg = $('off-msg');
const offBtn = $('btn-off-connect');

function setConn(kind) {
    bleBtn.className = `st-${kind}`;
    bleBtn.classList.remove('arming');
    delete bleBtn.dataset.armed;
    offBtn.hidden = true;
    switch (kind) {
    case 'live':
        bleBtn.textContent = '已连接';
        bleBtn.disabled = false;
        offMsg.textContent = '';
        setLive(true);
        clearChart();
        break;
    case 'connecting':
        bleBtn.textContent = '正在连接…';
        bleBtn.disabled = true;
        offMsg.textContent = '正在连接…';
        setLive(false);
        break;
    case 'reconnecting':
        bleBtn.textContent = '重连中…';
        bleBtn.disabled = false;
        offMsg.textContent = '连接中断，正在重连…';
        setLive(false);
        break;
    case 'unsupported':
        bleBtn.textContent = '浏览器不支持蓝牙';
        bleBtn.disabled = true;
        offMsg.textContent = '此浏览器不支持 Web 蓝牙，请用 Chrome 或 Edge 打开';
        setLive(false);
        break;
    default: // idle
        bleBtn.textContent = '连接秤';
        bleBtn.disabled = false;
        offMsg.textContent = '秤未连接';
        offBtn.hidden = false;
        setLive(false);
    }
    updateChartHint();
}

const link = new ScaleLink({
    bluetooth: navigator.bluetooth,
    uuids: Screen.bleUuids(),
    onFrame: (bytes, t) => {
        const m = Screen.decodeFrame(new Uint8Array(
            bytes.buffer, bytes.byteOffset, bytes.byteLength));
        if (m) showFrame(m, t);
    },
    onStatus: kind => setConn(kind),
});

async function connectOrDisconnect() {
    if (link.connected || link.status === 'reconnecting') {
        armConfirm(bleBtn, '断开连接？', () => link.disconnect());
        return;
    }
    try { await link.connect(); }
    catch { /* 用户取消或失败 — status 已回到 idle */ }
}
bleBtn.onclick = connectOrDisconnect;
offBtn.onclick = connectOrDisconnect;

// permitted devices reconnect on their own when supported
link.autoConnect();

// Chrome drops advertisement watches while the window is hidden —
// re-arm the reconnect when we come back.
document.addEventListener('visibilitychange', () => {
    if (!document.hidden) link.resume();
});
addEventListener('focus', () => link.resume());

// --- recording ------------------------------------------------------------------
// Bound to the brew timer (snap.timerState: 0 idle, 1 running, 2 paused),
// in any mode. frames[] keeps the raw decoded frames for the jsonl
// download; on stop() the data is folded into a compact brew row whose
// t[] is timer time — paused intervals never appear on the curve.

const recStatus = $('rec-status');
const recTxt = $('rec-txt');

const recorder = {
    frames: [], on: false, lastTimerMs: 0,
    onFrame(m, t) {
        const s = m.snap;
        if (!this.on) {
            // covers page open / reconnect mid-brew as well
            if (s.timerState === 1 || s.timerState === 2) this.start();
        } else if (s.timerState === 0) {
            this.stop();
        } else if (s.timerMs < this.lastTimerMs) {
            // missed the reset+restart — save the old one, start anew
            this.stop();
            this.start();
        }
        if (this.on && s.timerState === 1) {
            this.frames.push({ t: Math.round(t), ...m });
        }
        this.lastTimerMs = s.timerMs;
    },
    start() {
        this.on = true; this.frames = [];
        this.startedAt = Date.now();
        $('rec-info').textContent = '';
    },
    stop() {
        this.on = false;
        $('btn-dl').disabled = this.frames.length === 0;
        if (this.frames.length && this.frames.length < 10) {
            $('rec-info').textContent = '录制太短，未保存';
        } else if (!this.frames.length) {
            $('rec-info').textContent = '';
        }
        saveBrew(this.frames, this.startedAt)
            .catch(e => toast(`保存失败：${e.message || e}`));
    },
    // chart-head status — called on every frame
    updateStatus(s) {
        if (!this.on) {
            recStatus.className = '';
            recTxt.textContent = '计时开始后自动录制';
            return;
        }
        const ms = s.timerMs;
        const dur = `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`;
        if (s.timerState === 2) {
            recStatus.className = 'paused';
            recTxt.textContent = `已暂停 ${dur}`;
        } else {
            recStatus.className = 'on';
            recTxt.textContent = `录制中 ${dur}`;
        }
    },
};

$('btn-dl').onclick = () => {
    const body = recorder.frames.map(f => JSON.stringify(f)).join('\n');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([body], { type: 'application/jsonl' }));
    a.download = `coffee-${new Date().toISOString().replace(/[:.]/g, '-')}.jsonl`;
    a.click();
    URL.revokeObjectURL(a.href);
};

$('btn-clear').onclick = clearChart;

// --- pages (hash routing) -------------------------------------------------------

const PAGES = ['weigh', 'beans', 'cal', 'data'];
function switchPage(name) {
    if (!PAGES.includes(name)) name = 'weigh';
    for (const p of PAGES) $(`page-${p}`).classList.toggle('active', p === name);
    document.querySelectorAll('#tabbar a').forEach(a => {
        const on = a.dataset.page === name;
        a.classList.toggle('active', on);
        if (on) a.setAttribute('aria-current', 'page');
        else a.removeAttribute('aria-current');
    });
    if (location.hash !== `#${name}`) location.hash = name;
}
addEventListener('hashchange', () => switchPage(location.hash.slice(1)));
switchPage(location.hash.slice(1) || 'weigh');

// --- brew library ----------------------------------------------------------------

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
    const t0 = frames[0].snap.timerMs;
    const t = [], w = [], f = [];
    for (const m of frames) {
        // x = timer time — paused intervals are simply absent
        t.push(+((m.snap.timerMs - t0) / 1000).toFixed(2));
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
            beanTxt = `，归入 ${b.name}`;
            if (b.dose !== brew.dose) { b.dose = brew.dose; await DB.beans.update(b); }
        }
    }
    $('rec-info').textContent =
        `已保存 ${brew.liquid} g，用时 ${fmtDur(brew.durationS)}${beanTxt}`;
    toast('冲煮记录已保存');
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
    return b ? b.name : '未归档';
}

function stars(rating, id) {
    let h = '<span class="stars">';
    for (let i = 1; i <= 5; i++) {
        h += `<span data-rate="${id}:${i}" class="${i <= rating ? 'on' : ''}">★</span>`;
    }
    return h + '</span>';
}

function beanCard(b, bs) {
    const id = b ? b.id : 0;
    const open = openBeans.has(id);
    const meta = b ? `
      <div class="bean-meta">
        <label>名称<input data-bf="name" data-bid="${b.id}" value="${esc(b.name)}"></label>
        <label>品牌<input data-bf="brand" data-bid="${b.id}" value="${esc(b.brand)}"></label>
        <label>品种<input data-bf="variety" data-bid="${b.id}" value="${esc(b.variety)}"></label>
        <label>默认粉量 g<input type="number" step="0.1" min="0" data-bf="dose" data-bid="${b.id}" value="${b.dose || ''}"></label>
        <label class="wide">备注<input data-bf="note" data-bid="${b.id}" value="${esc(b.note)}"></label>
        <button class="textbtn danger-text" data-delbean="${b.id}">删除豆子</button>
      </div>` : '';
    const rows = bs.map(w => `
      <div class="brew ${openBrews.has(w.id) ? 'open' : ''}">
        <div class="brew-head" role="button" tabindex="0"
             aria-expanded="${openBrews.has(w.id)}" data-brewhead="${w.id}">
          <span class="tw">▸</span>
          <span class="brew-meta">
            <span class="meta">${w.date.slice(5, 16).replace('T', ' ')}</span>
            <span class="meta">${w.dose || '?'} → ${w.liquid} g</span>
            ${w.dose ? `<span class="meta">1:${(w.liquid / w.dose).toFixed(1)}</span>` : ''}
            <span class="meta">${fmtDur(w.durationS)}</span>
          </span>
          <span class="brew-side">
            ${stars(w.rating, w.id)}
            <button class="fav ${w.fav ? 'on' : ''}" data-fav="${w.id}"
                    aria-pressed="${w.fav ? 'true' : 'false'}" aria-label="收藏">♥</button>
          </span>
        </div>
        <div class="brew-detail" data-bd="${w.id}" ${openBrews.has(w.id) ? '' : 'hidden'}></div>
      </div>`).join('')
        || '<div class="empty">这支豆子还没有冲煮记录。</div>';
    const sub = b
        ? [b.brand, b.variety].filter(Boolean)
            .map(x => `<span class="meta">${esc(x)}</span>`).join('')
        : '';
    const last = bs.length ? bs[0].date.slice(5, 10) : '';
    return `
    <div class="bean ${open ? 'open' : ''}">
      <div class="bean-head" role="button" tabindex="0"
           aria-expanded="${open}" data-beanhead="${id}">
        <span class="tw">▸</span><span class="bname">${b ? esc(b.name) : '未归档'}</span>
        ${sub}
        <span class="spacer"></span>
        <span class="meta">${bs.length} 次冲煮</span>
        ${last ? `<span class="meta">${last}</span>` : ''}
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
    beanSel.innerHTML = '<option value="">不选豆子</option>' +
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
        ? '<div class="empty">还没有豆子。添加第一支豆子后，冲煮记录会自动归到它名下。</div>'
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
        `${w.dose ? ` <span class="v">1:${(w.liquid / w.dose).toFixed(1)}</span>` : ''}</div>
        <div>${fmtDur(w.durationS)} · ${w.t.length} 个点</div>
        <input class="note" data-note="${w.id}" placeholder="备注：风味、改进…" value="${esc(w.note || '')}">
        <div class="row" style="margin-top:2px">
          <button data-expbrew="${w.id}">导出</button>
          <button class="danger-text" data-delbrew="${w.id}">删除</button>
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

$('bean-accordion').addEventListener('click', async e => {
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
        armConfirm(delB, '确认删除？', async () => {
            openBrews.delete(id);
            await DB.brews.del(id);
            renderBeans();
        });
        return;
    }
    const delBean = e.target.closest('[data-delbean]');
    if (delBean) {
        const id = +delBean.dataset.delbean;
        armConfirm(delBean, '确认删除？冲煮记录会移到未归档', async () => {
            for (const w of brewCache.filter(w => w.beanId === id)) {
                w.beanId = null;
                await DB.brews.update(w);
            }
            openBeans.delete(id);
            await DB.beans.del(id);
            renderBeans();
        });
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
});

// rows are divs with role=button — Enter/Space toggles like a real button
$('bean-accordion').addEventListener('keydown', e => {
    if (e.key !== 'Enter' && e.key !== ' ') return;
    const head = e.target.closest('[data-brewhead], [data-beanhead]');
    if (!head || e.target !== head) return;
    e.preventDefault();
    head.click();
});

// bean meta fields + per-brew note — saved on blur/enter
$('bean-accordion').addEventListener('change', async e => {
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
        toast('备注已保存');
    }
});

// add-bean form — submit on Enter or the button; disabled while empty
const beanNameIn = $('bean-name');
const addBeanBtn = $('btn-addbean');
beanNameIn.addEventListener('input', () => {
    addBeanBtn.disabled = !beanNameIn.value.trim();
});
$('bean-form').addEventListener('submit', async e => {
    e.preventDefault();
    const name = beanNameIn.value.trim();
    if (!name) return;
    beanNameIn.value = '';
    addBeanBtn.disabled = true;
    const id = await DB.beans.add({ name, dose: doseVal() });
    beanSel.value = String(id);
    openBeans.add(id);
    renderBeans();
});

beanSel.onchange = async () => {
    const b = beanCache.find(b => b.id === curBeanId());
    if (b && b.dose) { doseIn.value = b.dose; doseIn.onchange(); }
};

// --- backup / clear ---------------------------------------------------------------

$('btn-export').onclick = async () => {
    const body = JSON.stringify(await DB.exportAll(), null, 1);
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([body], { type: 'application/json' }));
    a.download = `coffee-backup-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
    toast('备份已导出');
};

$('btn-import').onclick = () => {
    const inp = document.createElement('input');
    inp.type = 'file'; inp.accept = '.json';
    inp.onchange = async () => {
        try {
            const data = JSON.parse(await inp.files[0].text());
            const n = await DB.importAll(data);
            toast(`已导入 ${n.beans} 支豆子、${n.brews} 条记录`);
            renderBeans();
        } catch (e) {
            toast(`导入失败：${e.message}`);
        }
    };
    inp.click();
};

$('btn-clearbrews').onclick = () => {
    armConfirm($('btn-clearbrews'), '确认清空？豆子会保留', async () => {
        await DB.brews.clear();
        toast('冲煮记录已清空');
        renderBeans();
    });
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

// theme change — rebuild the uPlot instances with the new palette
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    chart.retheme();
    renderBeans();
});

// --- main loop -----------------------------------------------------------------

function frame() {
    if (Screen.pump()) blit();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
