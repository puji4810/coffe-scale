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
import { initScan } from './scan.js';

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

// Same-value DOM writes still mark nodes dirty — at 20 Hz that adds up,
// so hot-path nodes are only touched when the value actually changes.
const setTxt = (el, s) => { if (el._t !== s) { el._t = s; el.textContent = s; } };
const setCss = (el, p, s) => { if (el['_' + p] !== s) { el['_' + p] = s; el.style[p] = s; } };
const setCls = (el, s) => { if (el._c !== s) { el._c = s; el.className = s; } };

let toastTimer = null, toastAct = null;
function toast(msg, action) {
    const el = $('toast'), act = $('toast-act');
    $('toast-msg').textContent = msg;
    toastAct = action || null;
    if (action) {
        act.hidden = false;
        act.textContent = action.label;
    } else {
        act.hidden = true;
    }
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'),
                            action ? 6000 : 3000);
}
$('toast-act').onclick = () => {
    $('toast').classList.remove('show');
    toastAct?.fn();
};

// haptic nudge — Android only; iOS ignores navigator.vibrate entirely
const buzz = p => navigator.vibrate?.(p);

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
let targetHit = false;
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
    if (!v && brewOn) { brewOn = false; applyFocus(); }
    keepScreen(v);
}

// keep the phone awake while connected — hands are wet mid-brew.
// The lock auto-releases when the tab hides; re-acquire on return.
let wakeLock = null;
async function keepScreen(on) {
    if (!('wakeLock' in navigator)) return;
    if (on && !wakeLock && !document.hidden) {
        try {
            wakeLock = await navigator.wakeLock.request('screen');
            wakeLock.onrelease = () => { wakeLock = null; };
        } catch { /* unsupported / denied */ }
    } else if (!on && wakeLock) {
        wakeLock.release();
        wakeLock = null;
    }
}

// --- snapshot -> UI ----------------------------------------------------------

function segSet(seg, v) {
    if (seg.dataset.active === String(v)) return;
    seg.dataset.active = String(v);
    seg.querySelectorAll('button').forEach(b =>
        b.setAttribute('aria-checked', String(+b.dataset.v === v)));
}

function showFrame(m, tMs) {
    Screen.update(m);                     // canvas mirror
    const s = m.snap;
    // timer (re)start anchors the ghost curve's t=0 on the live x axis
    const timerRestarted = s.timerState === 1 &&
        (lastTimerState !== 1 || s.timerMs < lastTimerMs);
    lastGrams = s.grams;
    lastTimerMs = s.timerMs;
    lastTimerState = s.timerState;
    chart.push(tMs, s.grams, s.flowGps);
    // project t=0 backwards — covers reconnecting mid-brew too
    if (timerRestarted)
        chart.ghostBase = chart.secOf(tMs) - s.timerMs / 1000;

    const dose = doseVal(), tgt = targetG();
    // the buzz is the point of this alert — keep it ahead of the
    // hidden-tab early return so it still fires off-screen
    if (s.timerState === 1 && !targetHit && tgt > 0 && s.grams >= tgt) {
        targetHit = true;
        buzz([40, 60, 40]);
        toast(`达到目标 ${Math.round(tgt)} g`);
    }
    if (s.timerState === 0) targetHit = false;

    recorder.onFrame(m, tMs);             // recording never pauses
    autoTimer(s, tMs);                    // nor does the pour auto-timer
    // Everything below is DOM work — pointless while hidden. The chart
    // keeps buffering (its own redraw is gated); one flush on return.
    if (document.hidden) return;

    // instrument strip
    $('stable-dot').classList.toggle('on', s.stable);
    setTxt($('v-flags'), s.stable ? '稳定' : '变动中');
    const bf = $('batt-fill');
    if (m.batteryPct < 0) {
        setTxt($('v-batt'), '–');
        setCss(bf, 'width', '0%');
    } else {
        setTxt($('v-batt'), m.charging ? '充电中' : `${m.batteryPct}%`);
        const pct = Math.min(m.batteryPct, 100);
        setCss(bf, 'width', `${pct}%`);
        setCss(bf, 'background', m.charging ? 'var(--amber)'
            : pct > 50 ? 'var(--green)' : pct > 20 ? 'var(--amber)'
            : 'var(--red)');
    }
    const dx = Math.max(-8, Math.min(8, s.rollDeg * 0.8));
    const dy = Math.max(-8, Math.min(8, s.pitchDeg * 0.8));
    const tilt = Math.max(Math.abs(s.pitchDeg), Math.abs(s.rollDeg));
    const dot = $('bubble-dot');
    setCss(dot, 'transform', `translate(${dx}px, ${dy}px)`);
    setCss(dot, 'background', !s.stable ? 'var(--dim)'
        : tilt < 2 ? 'var(--green)' : 'var(--amber)');
    setTxt($('v-tilt'), s.stable ? `${tilt.toFixed(1)}°` : '变动中');
    setTxt($('cal-state'), s.calibrated ? '已校准' : '未校准');
    $('cal-state').classList.toggle('warn', !s.calibrated);
    setTxt($('cal-chip'), s.calibrated ? '已校准' : '未校准');
    setCls($('cal-chip'), `chip ${s.calibrated ? 'ok' : 'warn'}`);

    // keys / segments reflect the frame — the device is the source of truth
    setTxt($('btn-long'), TIMER_KEY_LABEL[s.timerState] || '开始计时');
    $('btn-reset').disabled = !(live && s.timerState !== 0);
    segSet($('seg-mode'), s.mode);
    segSet($('seg-unit'), s.unit);

    setTxt($('v-ratio'),
           dose > 0 ? `1 : ${(s.grams / dose).toFixed(1)}` : '–');

    // brew focus view — the big numbers mirror what the LCD shows
    const brewing = live && (s.timerState === 1 || s.timerState === 2);
    if (brewing !== brewOn) {
        brewOn = brewing;
        if (!brewing) { focusDismissed = false; focusManual = false; }
        applyFocus();
    }
    if (brewing) {
        setTxt($('bb-time'), fmtDur(s.timerMs / 1000));
        setTxt($('bb-w'), s.grams.toFixed(1));
        setTxt($('bb-f'), s.flowGps.toFixed(1));
        setTxt($('bb-r'), dose > 0 ? `1:${(s.grams / dose).toFixed(1)}` : '–');
        const prog = $('bb-bar');
        if (tgt <= 0) {
            setTxt($('bb-target'), '');
            setCss(prog, 'width', '0%');
            prog.classList.remove('full');
        } else {
            setTxt($('bb-target'), `目标 ${Math.round(tgt)} g`);
            setCss(prog, 'width',
                `${Math.min(100, s.grams / tgt * 100).toFixed(1)}%`);
            prog.classList.toggle('full', s.grams >= tgt);
        }
    }

    updateChartHint();
    recorder.updateStatus(s);
}

// --- brew focus mode ------------------------------------------------------------
// The weigh page drops the chrome for a large readout: automatically while
// the brew timer runs (pref in 数据, dismissible per brew), or manually via
// a double-tap on the screen mirror.
let brewOn = false, focusDismissed = false, focusManual = false;
const focusPrefEl = $('focus-toggle');
focusPrefEl.checked = localStorage.getItem('focus-view') !== '0';
focusPrefEl.onchange = () =>
    localStorage.setItem('focus-view', focusPrefEl.checked ? '1' : '0');

// double-tap / double-click the bezel toggles focus view
let lastBezelTap = 0;
$('bezel').addEventListener('pointerdown', e => {
    if (e.target.closest('button')) return;         // 连接秤 button inside
    const dt = e.timeStamp - lastBezelTap;
    lastBezelTap = dt < 350 ? 0 : e.timeStamp;
    if (dt >= 350) return;
    focusManual = !focusManual;
    focusDismissed = false;
    if (focusManual) buzz(10);
    applyFocus();
});

function applyFocus() {
    const on = (focusManual ||
                (brewOn && focusPrefEl.checked && !focusDismissed))
               && curPage === 'weigh';
    if (document.body.classList.contains('focus') === on) return;
    document.body.classList.toggle('focus', on);
    // the layout changed — re-measure the plot
    requestAnimationFrame(() => {
        const el = $('chart');
        if (chart.visible && el.clientWidth && el.clientHeight)
            chart.resize(el.clientWidth, el.clientHeight);
    });
}
$('bb-exit').onclick = () => {
    focusDismissed = true;
    focusManual = false;
    applyFocus();
};

// auto-timer: pour evidence (sustained positive flow) while the timer is
// idle starts it — brewing without the timer records nothing. Re-arms
// only after ~2 s of quiet flow so post-reset slosh can't re-fire.
let pourOnAt = null, pourArmed = true, pourQuietAt = null;
function autoTimer(s, tMs) {
    if (!pourArmed) {
        if (Math.abs(s.flowGps) < 0.5) {
            pourQuietAt ??= tMs;
            if (tMs - pourQuietAt > 2000) pourArmed = true;
        } else pourQuietAt = null;
    }
    if (!live || s.timerState !== 0 || !autoTimerEl.checked) {
        pourOnAt = null;
        return;
    }
    if (s.flowGps > 1.5) {
        pourOnAt ??= tMs;
        if (pourArmed && tMs - pourOnAt > 800) {
            pourArmed = false;
            cmd(OP.timerToggle);
            toast('检测到注水，已自动开始计时');
        }
    } else if (s.flowGps < 0.5) {
        pourOnAt = null;
    }
}

// empty-chart hint — offline invite, or "waiting" while live with no data
function updateChartHint() {
    const el = $('chart-empty');
    if (chart.x.length) { el.hidden = true; return; }
    el.hidden = false;
    setTxt(el, live ? '等待数据…' : '连接秤后显示实时曲线');
}
function clearChart() { chart.clear(); updateChartHint(); }

// screen-reader summary, at most once per second
setInterval(() => {
    const el = $('sr-live');
    if (!live || document.hidden) { setTxt(el, ''); return; }
    const ms = lastTimerMs;
    setTxt(el,
        `重量 ${lastGrams.toFixed(1)} g，计时 ` +
        `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`);
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

$('btn-tare').onclick = () => { buzz(12); cmd(OP.tare); };
$('btn-long').onclick = () => { buzz(20); cmd(OP.timerToggle); };
$('btn-reset').onclick = () => { buzz([10, 40, 10]); cmd(OP.timerReset); };

// disabled controls say nothing — a tap while offline points at the header's
// connect button instead of feeling broken (pointer-events are off on the
// buttons themselves, so the container is the hit target)
$('page-weigh').addEventListener('pointerdown', e => {
    if (live) return;
    if (!e.target.closest('.keys, .segrow, #btn-weigh')) return;
    toast('先连接秤 — 右上角');
    const b = $('btn-ble');
    b.classList.remove('nudge'); void b.offsetWidth; b.classList.add('nudge');
});
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
    if (beanSheet.open) return;
    if (!$('scan-ov').hidden) return;         // scanner has the keyboard
    const t = e.target;
    if (t.closest('input, select, textarea') || t.isContentEditable) return;
    if (!live) return;
    if (e.key === 't') cmd(OP.tare);
    else if (e.key === 'l') cmd(OP.timerToggle);
    else if (e.key === 'r') cmd(OP.timerReset);
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
// re-arm the reconnect when we come back. Frames kept streaming while
// hidden, so the chart needs one flush to show the buffered points.
document.addEventListener('visibilitychange', () => {
    if (document.hidden) return;
    link.resume();
    keepScreen(live);
    if (chart.visible) chart.redraw();
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
            setCls(recStatus, '');
            setTxt(recTxt, '计时开始后自动录制');
            return;
        }
        const ms = s.timerMs;
        const dur = `${Math.floor(ms / 60000)}:${String(Math.floor(ms / 1000) % 60).padStart(2, '0')}`;
        if (s.timerState === 2) {
            setCls(recStatus, 'paused');
            setTxt(recTxt, `已暂停 ${dur}`);
        } else {
            setCls(recStatus, 'on');
            setTxt(recTxt, `录制中 ${dur}`);
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
let curPage = 'weigh';
const scrollMem = {};               // per-page scroll offset
function switchPage(name) {
    if (!PAGES.includes(name)) name = 'weigh';
    scrollMem[curPage] = scrollY;
    curPage = name;
    for (const p of PAGES) $(`page-${p}`).classList.toggle('active', p === name);
    document.querySelectorAll('#tabbar a').forEach(a => {
        const on = a.dataset.page === name;
        a.classList.toggle('active', on);
        if (on) a.setAttribute('aria-current', 'page');
        else a.removeAttribute('aria-current');
    });
    requestAnimationFrame(() => scrollTo(0, scrollMem[name] ?? 0));
    applyFocus();
    if (location.hash !== `#${name}`) location.hash = name;
}
addEventListener('hashchange', () => switchPage(location.hash.slice(1)));
switchPage(location.hash.slice(1) || 'weigh');

// --- brew library ----------------------------------------------------------------

const doseIn = $('dose');
const ratioIn = $('ratio');

function doseVal() { return parseFloat(doseIn.value) || 0; }
function ratioVal() { return parseFloat(ratioIn.value) || 0; }
function targetG() { return doseVal() * ratioVal(); }

// bean selection persists across reloads (a stray "" = 不选豆子)
let curBean = localStorage.getItem('curBean');
curBean = curBean ? +curBean : null;
const curBeanId = () => curBean;

function syncBeanPick() {
    const b = beanCache.find(b => b.id === curBean);
    if (curBean !== null && !b) curBean = null;      // bean was deleted
    $('bean-pick').classList.toggle('empty', !b);
    $('bean-pick-name').textContent = b ? b.name : '选择豆子';
    $('bean-pick-sub').textContent =
        b ? [b.brand, b.process].filter(Boolean).join(' · ') : '';
}

function doseChanged() {
    localStorage.setItem('dose', doseIn.value);
    updateTarget();
}
function ratioChanged() {
    localStorage.setItem('ratio', ratioIn.value);
    updateTarget();
}
function updateTarget() {
    const t = targetG();
    $('v-target').textContent = t > 0 ? String(Math.round(t)) : '–';
    chart.setTarget(t > 0 ? t : null);
}

// a bean's own defaults ride along with the selection
function applyBeanDefaults() {
    const b = beanCache.find(b => b.id === curBean);
    if (b?.dose) { doseIn.value = b.dose; doseChanged(); }
    if (b?.ratio) { ratioIn.value = b.ratio; ratioChanged(); }
}

function selectBean(id) {
    curBean = id;
    localStorage.setItem('curBean', id === null ? '' : String(id));
    syncBeanPick();
    applyBeanDefaults();
    pickGhost();
}

doseIn.value = localStorage.getItem('dose') || '18';
ratioIn.value = localStorage.getItem('ratio') || '15';
doseIn.addEventListener('change', doseChanged);
ratioIn.addEventListener('change', ratioChanged);
updateTarget();

$('btn-weigh').onclick = () => {
    doseIn.value = lastGrams.toFixed(1);
    doseChanged();
    buzz(10);
};

// −/+ steppers: tap for one step, press-and-hold repeats. Pointer events
// only; Enter/Space on a focused button still fires click → one step.
addEventListener('pointerdown', e => {
    const b = e.target.closest('.stepper button[data-step]');
    if (!b) return;
    e.preventDefault();
    const input = b.parentElement.querySelector('input');
    const step = +b.dataset.step;
    const min = input.min === '' ? -Infinity : +input.min;
    const max = input.max === '' ? Infinity : +input.max;
    const tick = () => {
        const v = Math.round(((parseFloat(input.value) || 0) + step) * 100) / 100;
        input.value = Math.min(max, Math.max(min, v));
        input.dispatchEvent(new Event('change', { bubbles: true }));
    };
    tick(); buzz(6);
    let iv = 0, n = 0;
    // hold: 400 ms pause → ~11 steps/s, accelerating to ~29/s past ~1.3 s —
    // cal-mass 100→200 g goes from ~8 s to ~4 s while taps stay precise
    const t = setTimeout(() => {
        iv = setInterval(() => {
            tick();
            if (++n === 10) { clearInterval(iv); iv = setInterval(tick, 35); }
        }, 90);
    }, 400);
    const up = () => { clearTimeout(t); clearInterval(iv); };
    addEventListener('pointerup', up, { once: true });
    addEventListener('pointercancel', up, { once: true });
});

// typed (not stepped) values can sit outside min/max — clamp on commit.
// Capture phase so the clamp lands before the field's own change handler
// reads the value.
addEventListener('change', e => {
    const inp = e.target;
    if (!inp.matches?.('input[type=number]')) return;
    const v = parseFloat(inp.value);
    if (isNaN(v)) return;
    const lo = inp.min === '' ? -Infinity : +inp.min;
    const hi = inp.max === '' ? Infinity : +inp.max;
    const c = Math.min(hi, Math.max(lo, v));
    if (c !== v) inp.value = String(c);
}, true);

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
            if (b.dose !== brew.dose || b.ratio !== ratioVal()) {
                b.dose = brew.dose;
                if (ratioVal()) b.ratio = ratioVal();
                await DB.beans.update(b);
            }
        }
    }
    $('rec-info').textContent =
        `已保存 ${brew.liquid} g，用时 ${fmtDur(brew.durationS)}${beanTxt}`;
    toast('冲煮记录已保存');
    renderBeans();
}

function fmtDur(s) {
    // round first — splitting floor/round across the minute boundary shows
    // "1:00" for ~0.5 s at 119.5–120 s
    const r = Math.round(s);
    return `${Math.floor(r / 60)}:${String(r % 60).padStart(2, '0')}`;
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

// tiny weight-curve silhouette — the pour's shape at a glance, capped at
// ~60 svg points so a long brew doesn't bloat the accordion's innerHTML
function sparkline(w) {
    const ws = w.w || [], n = ws.length;
    if (n < 4) return '';
    const mx = Math.max(...ws) || 1, stride = Math.max(1, Math.floor(n / 60));
    const pts = [];
    for (let i = 0; i < n; i += stride)
        pts.push(`${(i / (n - 1) * 44).toFixed(1)},${(19 - ws[i] / mx * 17).toFixed(1)}`);
    return `<svg class="spark" viewBox="0 0 44 20" aria-hidden="true"><polyline points="${pts.join(' ')}"/></svg>`;
}

function beanCard(b, bs) {
    const id = b ? b.id : 0;
    const open = openBeans.has(id);
    const meta = b ? `
      <div class="bean-meta">
        <label>名称<input data-bf="name" data-bid="${b.id}" value="${esc(b.name)}"></label>
        <label>品牌<input data-bf="brand" data-bid="${b.id}" data-sugg="brand" value="${esc(b.brand)}"></label>
        <label>处理法<input data-bf="process" data-bid="${b.id}" data-sugg="process" value="${esc(b.process)}"></label>
        <label>豆种<input data-bf="variety" data-bid="${b.id}" data-sugg="variety" value="${esc(b.variety)}"></label>
        <label>庄园<input data-bf="estate" data-bid="${b.id}" data-sugg="estate" value="${esc(b.estate)}"></label>
        <label>默认粉量 g<input type="number" step="0.1" min="0" data-bf="dose" data-bid="${b.id}" value="${b.dose || ''}"></label>
        <label class="wide">备注<textarea rows="2" data-bf="note" data-bid="${b.id}">${esc(b.note)}</textarea></label>
        <button class="textbtn danger-text" data-delbean="${b.id}">删除豆子</button>
      </div>` : '';
    const rows = bs.map(w => `
      <div class="brew ${openBrews.has(w.id) ? 'open' : ''}">
        <div class="brew-head ${selBrews.has(w.id) ? 'sel' : ''}" role="button" tabindex="0"
             aria-expanded="${openBrews.has(w.id)}" data-brewhead="${w.id}">
          <span class="tw">▸</span>
          <span class="brew-meta">
            <span class="meta">${w.date.slice(5, 16).replace('T', ' ')}</span>
            <span class="meta">${w.dose || '?'} → ${w.liquid} g</span>
            ${w.dose ? `<span class="meta">1:${(w.liquid / w.dose).toFixed(1)}</span>` : ''}
            <span class="meta">${fmtDur(w.durationS)}</span>
          </span>
          ${sparkline(w)}
          <span class="brew-side">
            ${stars(w.rating, w.id)}
            <button class="fav ${w.fav ? 'on' : ''}" data-fav="${w.id}"
                    aria-pressed="${w.fav ? 'true' : 'false'}" aria-label="收藏">♥</button>
          </span>
        </div>
        <div class="fold" ${openBrews.has(w.id) ? '' : 'inert'}><div>
          <div class="brew-detail" data-bd="${w.id}"></div>
        </div></div>
      </div>`).join('')
        || '<div class="empty">这支豆子还没有冲煮记录。</div>';
    const sub = b
        ? [b.brand && `<span class="meta"><i class="bdot" style="--bh:${
                brandHue(b.brand)}"></i>${esc(b.brand)}</span>`,
           b.process && `<span class="meta">${esc(b.process)}</span>`,
           b.variety && `<span class="meta">${esc(b.variety)}</span>`,
           (b.dose || b.ratio) && `<span class="meta">${
                b.dose ? `${b.dose}g` : ''}${b.dose && b.ratio ? ' ' : ''}${
                b.ratio ? `1:${b.ratio}` : ''}</span>`]
            .filter(Boolean).join('')
        : '';
    const last = bs.length ? bs[0].date.slice(5, 10) : '';
    const rated = bs.filter(w => w.rating);
    const avg = rated.length
        ? (rated.reduce((s, w) => s + w.rating, 0) / rated.length).toFixed(1) : '';
    return `
    <div class="bean ${b ? '' : 'loose'} ${open ? 'open' : ''}">
      <div class="bean-head ${b && selBeans.has(id) ? 'sel' : ''}" role="button" tabindex="0"
           aria-expanded="${open}" data-beanhead="${id}">
        <span class="tw">▸</span>
        <span class="bh-main">
          <span class="bname">${b ? esc(b.name) : '未归档'}</span>
          <span class="bsub">${sub}</span>
        </span>
        <span class="bh-stat"><b>${bs.length}</b><span>次冲煮</span>${
          avg ? `<span class="bh-avg">★${avg}</span>` : ''}${
          last ? `<span>${last}</span>` : ''}</span>
      </div>
      <div class="fold" ${open ? '' : 'inert'}><div>
        <div class="bean-body">${meta}${rows}</div>
      </div></div>
    </div>`;
}

let beansLoaded = false;

async function renderBeans() {
    beanCache = await DB.beans.list();
    beanCache.sort((a, b) => {
        const ai = beanOrder.indexOf(a.id), bi = beanOrder.indexOf(b.id);
        return (ai < 0 ? 1e9 : ai) - (bi < 0 ? 1e9 : bi) || a.id - b.id;
    });
    brewCache = await DB.brews.list();
    brewCache.sort((a, b) => (a.date < b.date ? 1 : -1));   // newest first
    beansLoaded = true;
    paintBeans();
}

// DOM pass over the cached lists — callers that only changed in-memory
// state (selection, search, a cached field) skip the IndexedDB round-trip.
function paintBeans() {
    if (!beansLoaded) return;    // the in-flight first load paints itself
    syncBeanPick();
    $('bar-move').innerHTML = '<option value="-1">归档到…</option>' +
        '<option value="">未归档</option>' +
        beanCache.map(b => `<option value="${b.id}">${esc(b.name)}</option>`).join('');

    // hide the search box for tiny libraries — but keep it visible while
    // it holds an active filter (e.g. a brand-chip tap set it)
    $('bean-q').hidden = beanCache.length < 4 && !$('bean-q').value;
    const q = $('bean-q').value.trim().toLowerCase();
    const hit = b => [b.name, b.brand, b.process, b.variety, b.estate]
        .some(v => v && v.toLowerCase().includes(q));
    let groups = beanCache.map(b => ({
        b, bs: brewCache.filter(w => w.beanId === b.id),
    }));
    if (q) groups = groups.filter(g =>
        hit(g.b) || g.bs.some(w => w.note?.toLowerCase().includes(q)));
    const loose = brewCache.filter(w =>
        w.beanId === null || !beanCache.some(b => b.id === w.beanId));
    if (loose.length && !q) groups.push({ b: null, bs: loose });

    const acc = $('bean-accordion');
    acc.innerHTML = groups.length === 0
        ? (q ? '<div class="empty">没有匹配的豆子。</div>'
             : '<div class="empty">还没有豆子。添加第一支豆子后，冲煮记录会自动归到它名下。</div>')
        : groups.map(({ b, bs }) => beanCard(b, bs)).join('');
    renderBrandChips();
    pickGhost();
    setTxt($('lib-stats'),
        `${beanCache.length} 支豆子 · ${brewCache.length} 条冲煮 · 约 ${
            (new Blob([JSON.stringify({ beans: beanCache, brews: brewCache })])
                .size / 1024).toFixed(0)} KB`);
    if (suggFor && !suggFor.isConnected) hideSugg();

    // open brew details need a sized box — mount plots after the DOM exists
    for (const w of brewCache) {
        if (openBrews.has(w.id) && openBeans.has(w.beanId ?? 0)) mountBrewDetail(w);
    }
}

// comparison overlay: brew id -> other brew id, survives re-renders
const cmpSel = new Map();

function plotBrew(bd, w) {
    const other = brewCache.find(x => x.id === cmpSel.get(w.id));
    return brewPlot(bd.querySelector('.bd-curve'), w.t, w.w, w.f,
                    other || null);
}

function mountBrewDetail(w) {
    const bd = document.querySelector(`.brew-detail[data-bd="${w.id}"]`);
    if (!bd || bd.dataset.mounted) return;
    bd.dataset.mounted = '1';
    const others = brewCache.filter(x => x.id !== w.id);
    bd.innerHTML = `
      <div class="bd-curve"></div>
      <div class="bd-data">
        <div><span class="v">${w.date.slice(0, 16).replace('T', ' ')}</span></div>
        <div>${w.dose || '?'} g → <span class="v">${w.liquid} g</span>` +
        `${w.dose ? ` <span class="v">1:${(w.liquid / w.dose).toFixed(1)}</span>` : ''}</div>
        <div>${fmtDur(w.durationS)} · ${w.t.length} 个点</div>
        <textarea class="note" rows="2" data-note="${w.id}"
                  placeholder="备注：风味、改进…">${esc(w.note || '')}</textarea>
        ${others.length ? `<select class="cmp" data-cmp="${w.id}">
          <option value="">叠加对比…</option>
          ${others.map(x => `<option value="${x.id}">${
            esc(x.date.slice(5, 16).replace('T', ' '))} · ${
            esc(beanName(x.beanId))} · ${x.liquid} g</option>`).join('')}
        </select>` : ''}
        <select class="cmp" data-assign="${w.id}">
          <option value="">未归档</option>
          ${beanCache.map(b => `<option value="${b.id}"${
            w.beanId === b.id ? ' selected' : ''}>${esc(b.name)}</option>`).join('')}
        </select>
        <div class="row" style="margin-top:2px">
          <button data-expbrew="${w.id}">导出</button>
          <button class="danger-text" data-delbrew="${w.id}">删除</button>
        </div>
      </div>`;
    const cmp = bd.querySelector('[data-cmp]');
    if (cmp) cmp.value = cmpSel.get(w.id) || '';
    bd._u = plotBrew(bd, w);
}

function exportBrew(w) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(
        new Blob([JSON.stringify(w)], { type: 'application/json' }));
    a.download = `brew-${w.date.slice(0, 10)}-${beanName(w.beanId)}.json`;
    a.click();
    URL.revokeObjectURL(a.href);
}

// a tap pops the star/heart for a beat so the change registers visually
function pop(el) {
    el.classList.remove('pop'); void el.offsetWidth; el.classList.add('pop');
}

// fold toggles stay in the DOM (no re-render) so the 0fr→1fr transition animates
function toggleFold(card, open, set, id) {
    card.classList.toggle('open', open);
    open ? set.add(id) : set.delete(id);
    card.querySelector(':scope > .bean-head, :scope > .brew-head')
        ?.setAttribute('aria-expanded', String(open));
    card.querySelector(':scope > .fold')?.toggleAttribute('inert', !open);
}

$('bean-accordion').addEventListener('click', async e => {
    const rate = e.target.closest('[data-rate]');
    if (rate) {
        const [id, n] = rate.dataset.rate.split(':').map(Number);
        const w = brewCache.find(w => w.id === id);
        w.rating = (n === w.rating) ? 0 : n;
        rate.parentElement.querySelectorAll('[data-rate]').forEach(s =>
            s.classList.toggle('on', +s.dataset.rate.split(':')[1] <= w.rating));
        pop(rate); buzz(6);
        await DB.brews.update(w);
        return;
    }
    const fav = e.target.closest('[data-fav]');
    if (fav) {
        const w = brewCache.find(w => w.id === +fav.dataset.fav);
        w.fav = !w.fav;
        fav.classList.toggle('on', w.fav);
        fav.setAttribute('aria-pressed', String(w.fav));
        pop(fav); buzz(6);
        await DB.brews.update(w);
        pickGhost();            // the ♥ brew becomes the ghost reference
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
        const w = brewCache.find(x => x.id === id);
        openBrews.delete(id);
        await DB.brews.del(id);
        renderBeans();
        toast('已删除冲煮记录', { label: '撤销', fn: async () => {
            await DB.brews.update(w);
            renderBeans();
        } });
        return;
    }
    const delBean = e.target.closest('[data-delbean]');
    if (delBean) {
        const id = +delBean.dataset.delbean;
        armConfirm(delBean, '确认删除？冲煮记录会移到未归档', async () => {
            const bean = beanCache.find(b => b.id === id);
            const hit = brewCache.filter(w => w.beanId === id);
            for (const w of hit) {
                w.beanId = null;
                await DB.brews.update(w);
            }
            openBeans.delete(id);
            if (curBean === id) selectBean(null);
            await DB.beans.del(id);
            renderBeans();
            toast('已删除豆子', { label: '撤销', fn: async () => {
                if (bean) await DB.beans.update(bean);
                for (const w of hit) {
                    w.beanId = id;
                    await DB.brews.update(w);
                }
                openBeans.add(id);
                renderBeans();
            } });
        });
        return;
    }
    if (suppressHeadClick) { suppressHeadClick = false; return; }
    const head = e.target.closest('[data-brewhead], [data-beanhead]');
    if (head && editMode) { toggleSel(head); return; }
    const bh = e.target.closest('[data-brewhead]');
    if (bh) {
        const id = +bh.dataset.brewhead;
        const row = bh.closest('.brew');
        toggleFold(row, !row.classList.contains('open'), openBrews, id);
        if (openBrews.has(id)) {
            const w = brewCache.find(x => x.id === id);
            if (w) mountBrewDetail(w);
        }
        return;
    }
    const bnh = e.target.closest('[data-beanhead]');
    if (bnh) {
        const id = +bnh.dataset.beanhead;
        const card = bnh.closest('.bean');
        toggleFold(card, !card.classList.contains('open'), openBeans, id);
        if (openBeans.has(id)) {
            for (const w of brewCache) {
                if (w.beanId === id && openBrews.has(w.id)) mountBrewDetail(w);
            }
        }
    }
});

// --- selection / reorder mode --------------------------------------------------
// Long-press any bean/brew row → selection mode: circles appear, taps toggle,
// the toolbar offers select-all / batch assign / batch delete. Long-pressing a
// bean and keeping hold lets you drag it to a new position (order persists in
// localStorage; the 未归档 group always stays last and can't be reordered).

let editMode = false;
const selBeans = new Set(), selBrews = new Set();
let beanOrder = [];
try { beanOrder = JSON.parse(localStorage.getItem('beanOrder') || '[]'); } catch {}

let lpTimer = 0, lpPt = null, suppressHeadClick = false, dragBean = null;
const beanAcc = $('bean-accordion');

function toggleSel(head) {
    const isBrew = head.dataset.brewhead !== undefined;
    if (!isBrew && head.dataset.beanhead === '0') return;  // 未归档 card isn't an item
    const id = +(isBrew ? head.dataset.brewhead : head.dataset.beanhead);
    const set = isBrew ? selBrews : selBeans;
    set.has(id) ? set.delete(id) : set.add(id);
    head.classList.toggle('sel', set.has(id));
}
function enterEdit() {
    if (editMode) return;
    editMode = true;
    beanAcc.classList.add('editing');
    $('bean-bar').hidden = false;
}
function exitEdit() {
    editMode = false;
    selBeans.clear(); selBrews.clear();
    beanAcc.classList.remove('editing');
    $('bean-bar').hidden = true;
    for (const h of beanAcc.querySelectorAll('.sel')) h.classList.remove('sel');
}

beanAcc.addEventListener('pointerdown', e => {
    const head = e.target.closest('[data-brewhead], [data-beanhead]');
    if (!head) return;
    lpPt = { x: e.clientX, y: e.clientY };
    lpTimer = setTimeout(() => {
        lpTimer = 0;
        enterEdit();
        toggleSel(head);
        suppressHeadClick = true;          // the click on release would re-toggle
        const id = head.dataset.beanhead;
        if (id !== undefined && id !== '0') dragBean = { id, armed: true };
    }, 450);
});
beanAcc.addEventListener('pointermove', e => {
    if (lpTimer && Math.hypot(e.clientX - lpPt.x, e.clientY - lpPt.y) > 10) {
        clearTimeout(lpTimer); lpTimer = 0;                  // it's a scroll
    }
    if (!dragBean) return;
    if (dragBean.armed) {
        if (Math.abs(e.clientY - lpPt.y) < 6) return;
        dragBean.armed = false;
        beanAcc.classList.add('dragging');
        beanAcc.querySelector(`.bean-head[data-beanhead="${dragBean.id}"]`)
            ?.closest('.bean').classList.add('dragging');
    }
    e.preventDefault();
    const cards = [...beanAcc.querySelectorAll('.bean:not(.loose)')];
    const card = cards.find(c =>
        +c.querySelector('.bean-head').dataset.beanhead === dragBean.id);
    for (const c of cards) {
        if (c === card) continue;
        const r = c.getBoundingClientRect();
        if (e.clientY < r.top + r.height / 2) {
            beanAcc.insertBefore(card, c);
            return;
        }
    }
    beanAcc.insertBefore(card, beanAcc.querySelector('.bean.loose'));
});
const endPress = () => {
    if (lpTimer) { clearTimeout(lpTimer); lpTimer = 0; }
    if (dragBean && !dragBean.armed) {
        beanOrder = [...beanAcc.querySelectorAll('.bean:not(.loose) .bean-head')]
            .map(h => +h.dataset.beanhead);
        localStorage.setItem('beanOrder', JSON.stringify(beanOrder));
    }
    if (dragBean) {
        beanAcc.classList.remove('dragging');
        beanAcc.querySelector('.bean.dragging')?.classList.remove('dragging');
        dragBean = null;
    }
};
beanAcc.addEventListener('pointerup', endPress);
beanAcc.addEventListener('pointercancel', endPress);
beanAcc.addEventListener('contextmenu', e => {
    if (editMode || suppressHeadClick) e.preventDefault();
});

$('bar-all').onclick = () => {
    const allB = beanCache.map(b => b.id), allW = brewCache.map(w => w.id);
    const full = allB.every(i => selBeans.has(i)) && allW.every(i => selBrews.has(i));
    selBeans.clear(); selBrews.clear();
    if (!full) { allB.forEach(i => selBeans.add(i)); allW.forEach(i => selBrews.add(i)); }
    paintBeans();
};
$('bar-del').onclick = () => {
    if (!selBeans.size && !selBrews.size) { toast('先选中要删的项'); return; }
    armConfirm($('bar-del'), `确认删除 ${selBeans.size + selBrews.size} 项？`, async () => {
        for (const id of selBeans) {
            for (const w of brewCache.filter(w => w.beanId === id)) {
                w.beanId = null; await DB.brews.update(w);
            }
            openBeans.delete(id);
            await DB.beans.del(id);
        }
        for (const id of selBrews) { openBrews.delete(id); await DB.brews.del(id); }
        exitEdit(); renderBeans(); toast('已删除');
    });
};
$('bar-move').onchange = async () => {
    const v = $('bar-move').value;
    $('bar-move').value = '-1';
    if (v === '-1') return;
    const beanId = v === '' ? null : +v;
    let n = 0;
    for (const id of selBrews) {
        const w = brewCache.find(w => w.id === id);
        if (w && w.beanId !== beanId) { w.beanId = beanId; await DB.brews.update(w); n++; }
    }
    toast(n ? `已归档 ${n} 条记录` : '没有选中冲煮记录');
    exitEdit(); paintBeans();
};
$('bar-done').onclick = () => { exitEdit(); paintBeans(); };

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
    const asg = e.target.closest('[data-assign]');
    if (asg) {
        const w = brewCache.find(w => w.id === +asg.dataset.assign);
        w.beanId = asg.value === '' ? null : +asg.value;
        await DB.brews.update(w);
        toast(w.beanId === null ? '已移到未归档' : `已归入 ${beanName(w.beanId)}`);
        paintBeans();
        return;
    }
    const cmp = e.target.closest('[data-cmp]');
    if (cmp) {
        const wid = +cmp.dataset.cmp;
        if (cmp.value) cmpSel.set(wid, +cmp.value); else cmpSel.delete(wid);
        const bd = cmp.closest('.brew-detail');
        const w = brewCache.find(x => x.id === wid);
        if (bd && w) { bd._u?.destroy(); bd._u = plotBrew(bd, w); }
        return;
    }
    const bf = e.target.closest('[data-bf]');
    if (bf) {
        const b = beanCache.find(b => b.id === +bf.dataset.bid);
        b[bf.dataset.bf] = bf.dataset.bf === 'dose'
            ? (parseFloat(bf.value) || 0) : bf.value;
        await DB.beans.update(b);
        if (bf.dataset.bf === 'brand') rememberBrand(b.brand);
        paintBeans();                        // name/brand may be in the header
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

// --- bean field suggestions --------------------------------------------------
// One shared chip popover hangs under the focused input[data-sugg]. Values
// come from the presets plus whatever the library already uses — the same
// vocabulary the OCR matcher draws from.

const BRAND_KEY = 'bean-brands';
const getBrands = () => {
    try { return JSON.parse(localStorage.getItem(BRAND_KEY)) || []; }
    catch { return []; }
};

// deterministic hue per brand name — a stable "roaster color" for the
// chips and the bean-head dot, no stored state needed
function brandHue(n) {
    let h = 0;
    for (const c of String(n)) h = (h * 31 + c.codePointAt(0)) >>> 0;
    return h % 360;
}

// a brand that landed on a bean (typed or scanned) joins the preset list
// so it autocompletes everywhere afterwards
function rememberBrand(n) {
    n = String(n || '').trim();
    if (!n) return;
    const list = getBrands();
    if (list.includes(n)) return;
    list.push(n);
    localStorage.setItem(BRAND_KEY, JSON.stringify(list));
    renderBrandChips();
}
const PROCESS_PRESET = ['水洗', '日晒', '蜜处理', '厌氧日晒', '水洗厌氧', '酒桶发酵', '湿刨'];
const VARIETY_PRESET = ['瑰夏', '铁皮卡', '波旁', '卡杜拉', '卡蒂姆', 'SL28', 'SL34', '原生种', '帕卡马拉'];
const SUGG_PRESETS = { process: PROCESS_PRESET, variety: VARIETY_PRESET };

const suggEl = $('sugg');
let suggFor = null;                 // the input the popover belongs to

function suggVals(key) {
    const used = f => [...new Set(beanCache.map(b => b[f]).filter(Boolean))];
    const preset = key === 'brand' ? getBrands() : (SUGG_PRESETS[key] || []);
    return [...new Set([...preset, ...used(key)])];
}
function hideSugg() { suggEl.hidden = true; suggFor = null; }
function placeSugg() {
    if (!suggFor || !suggFor.isConnected) { hideSugg(); return; }
    const r = suggFor.getBoundingClientRect();
    suggEl.style.left = `${Math.max(8,
        Math.min(r.left, innerWidth - suggEl.offsetWidth - 8))}px`;
    const vh = visualViewport ? visualViewport.offsetTop + visualViewport.height
                              : innerHeight;
    let top = r.bottom + 6;
    if (top + suggEl.offsetHeight > vh - 8)
        top = Math.max(8, r.top - suggEl.offsetHeight - 6);
    suggEl.style.top = `${top}px`;
}
function showSugg() {
    if (!suggFor || !suggFor.isConnected) { hideSugg(); return; }
    const cur = suggFor.value.trim(), q = cur.toLowerCase();
    const all = suggVals(suggFor.dataset.sugg).filter(v => v !== cur);
    // typed text filters; when it matches nothing (e.g. a complete value
    // already filled) fall back to the full list so the chips still help
    let vals = q ? all.filter(v => v.toLowerCase().includes(q)) : all;
    if (!vals.length) vals = all;
    vals = vals.slice(0, 12);
    if (!vals.length) { suggEl.hidden = true; return; }
    suggEl.innerHTML = vals.map(v =>
        `<button type="button" class="sg-chip" data-v="${esc(v)}">${esc(v)}</button>`
    ).join('');
    suggEl.hidden = false;
    placeSugg();
}
document.addEventListener('focusin', e => {
    const inp = e.target.closest?.('input[data-sugg]');
    if (inp) { suggFor = inp; showSugg(); return; }
    if (suggFor && !(e.relatedTarget && suggEl.contains(e.relatedTarget)))
        hideSugg();
});
document.addEventListener('input', e => {
    if (e.target === suggFor) showSugg();
});
// pointerdown so the input keeps focus; preventDefault keeps the keyboard up
suggEl.addEventListener('pointerdown', e => {
    const c = e.target.closest('.sg-chip');
    if (!c || !suggFor) return;
    e.preventDefault();
    suggFor.value = c.dataset.v;
    suggFor.dispatchEvent(new Event('change', { bubbles: true }));
    suggFor.focus();
    hideSugg();
});
addEventListener('scroll', () => { if (suggFor) placeSugg(); },
               { capture: true, passive: true });
visualViewport?.addEventListener('resize', () => { if (suggFor) placeSugg(); });

function renderBrandChips() {
    const box = $('brand-chips');
    if (!box) return;
    const beanN = new Map();
    for (const b of beanCache)
        if (b.brand) beanN.set(b.brand, (beanN.get(b.brand) || 0) + 1);
    const cur = $('bean-q').value.trim();
    box.innerHTML = getBrands().map(n => `
        <span class="bchip${cur === n ? ' on' : ''}" style="--bh:${brandHue(n)}">
          <button type="button" class="bf" data-brand="${esc(n)}"
                  aria-pressed="${cur === n}" title="按品牌筛选">${esc(n)}${
            beanN.has(n) ? `<i>${beanN.get(n)}</i>` : ''}</button>
          <button type="button" class="bx" data-branddel="${esc(n)}"
                  aria-label="删除品牌 ${esc(n)}" title="删除">×</button>
        </span>`).join('') || '<span class="dim">还没有</span>';
}

// brand add: Enter or the appearing button; several at once split by 、,空格
$('brand-form').addEventListener('submit', e => {
    e.preventDefault();
    const inp = $('brand-new');
    const names = inp.value.split(/[,，、\s]+/).map(s => s.trim()).filter(Boolean);
    if (!names.length) return;
    const list = getBrands();
    let added = 0;
    for (const n of names) if (!list.includes(n)) { list.push(n); added++; }
    localStorage.setItem(BRAND_KEY, JSON.stringify(list));
    inp.value = '';
    renderBrandChips();
    toast(added ? `已添加 ${added} 个品牌` : '品牌已存在');
});
$('brand-chips').addEventListener('click', e => {
    const del = e.target.closest('[data-branddel]');
    if (del) {
        const n = del.dataset.branddel;
        const prev = getBrands();
        localStorage.setItem(BRAND_KEY,
            JSON.stringify(prev.filter(x => x !== n)));
        renderBrandChips();
        toast(`已删除品牌「${n}」`, { label: '撤销', fn: () => {
            localStorage.setItem(BRAND_KEY, JSON.stringify(prev));
            renderBrandChips();
        } });
        return;
    }
    // tap a chip to filter the list to that brand; tap again to clear.
    // The search box becomes visible so the active filter is clearable.
    const chip = e.target.closest('[data-brand]');
    if (!chip) return;
    const q = $('bean-q');
    q.value = q.value.trim() === chip.dataset.brand ? '' : chip.dataset.brand;
    q.hidden = false;
    paintBeans();
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
    const id = await DB.beans.add({ name, dose: doseVal(), ratio: ratioVal() });
    openBeans.add(id);                 // renders already-open in one pass
    await renderBeans();
    selectBean(id);
});

// bean-label scanner: camera + on-device OCR (scan.js), models lazy-load on
// first open; nothing here touches the BLE link or the scale's MCU.
initScan({
    toast,
    getBrands,
    listBeans: () => beanCache,
    selectBean: id => {
        selectBean(id);
        toast(`已选择「${beanName(id)}」`);
    },
    addBean: async f => {
        const id = await DB.beans.add({ ...f, dose: doseVal(), ratio: ratioVal() });
        if (f.brand) rememberBrand(f.brand);
        openBeans.add(id);
        await renderBeans();
        selectBean(id);
    },
});

// --- bean picker sheet -------------------------------------------------------------
// 称重页的豆子按钮打开这个底部面板：最近冲煮的排前面，可搜索，
// 输入不存在的名字直接新建。

const beanSheet = $('bean-sheet'), bsQ = $('bs-q'), bsList = $('bs-list');

function renderBsList() {
    const q = bsQ.value.trim().toLowerCase();
    const brewN = new Map(), lastD = new Map(), rateSum = new Map(), rateN = new Map();
    for (const w of brewCache) {
        if (w.beanId === null || !beanCache.some(b => b.id === w.beanId)) continue;
        brewN.set(w.beanId, (brewN.get(w.beanId) || 0) + 1);
        if (w.rating) {
            rateSum.set(w.beanId, (rateSum.get(w.beanId) || 0) + w.rating);
            rateN.set(w.beanId, (rateN.get(w.beanId) || 0) + 1);
        }
        if ((lastD.get(w.beanId) || '') < w.date) lastD.set(w.beanId, w.date);
    }
    const items = beanCache
        .filter(b => !q || [b.name, b.brand, b.process, b.variety, b.estate]
            .some(v => v && v.toLowerCase().includes(q)))
        .sort((x, y) => (lastD.get(y.id) || '').localeCompare(lastD.get(x.id) || '')
                        || x.name.localeCompare(y.name, 'zh'));
    const name = bsQ.value.trim();
    bsList.innerHTML =
        `<button class="bs-row${curBean === null ? ' sel' : ''}" data-id=""
           role="option" aria-selected="${curBean === null}">
           <span class="bh-main"><span class="bname dim">不选豆子</span></span></button>` +
        items.map(b => `
        <button class="bs-row${b.id === curBean ? ' sel' : ''}" data-id="${b.id}"
                role="option" aria-selected="${b.id === curBean}">
          <span class="bh-main">
            <span class="bname">${esc(b.name)}</span>
            <span class="bsub">${[b.brand && `<span class="meta"><i class="bdot"
                    style="--bh:${brandHue(b.brand)}"></i>${esc(b.brand)}</span>`,
                b.process && `<span class="meta">${esc(b.process)}</span>`,
                b.variety && `<span class="meta">${esc(b.variety)}</span>`]
                .filter(Boolean).join('')}</span>
          </span>
          <span class="bh-stat"><b>${brewN.get(b.id) || 0}</b><span>次冲煮</span>${
            rateN.has(b.id) ? `<span class="bh-avg">★${
                (rateSum.get(b.id) / rateN.get(b.id)).toFixed(1)}</span>` : ''}${
            lastD.has(b.id) ? `<span>${lastD.get(b.id).slice(5, 10)}</span>` : ''}</span>
        </button>`).join('') +
        (name && !beanCache.some(b => b.name === name)
            ? `<button class="bs-row bs-new" data-new="1">＋ 新建「${esc(name)}」</button>`
            : '') +
        (!items.length && !name ? '<div class="empty">没有匹配的豆子</div>' : '');
    bsList.scrollTop = 0;           // filtering/reopen shouldn't keep old scroll
}

$('bean-pick').onclick = () => {
    bsQ.value = '';
    renderBsList();
    beanSheet.showModal();
};
$('bs-close').onclick = () => beanSheet.close();
bsQ.addEventListener('input', renderBsList);
// Enter = pick the first match; only when nothing matches does it create
bsQ.addEventListener('keydown', e => {
    if (e.key !== 'Enter') return;
    e.preventDefault();
    if (!bsQ.value.trim()) return;          // empty query — don't surprise-pick
    const row = bsList.querySelector('.bs-row[data-id]:not([data-id=""])')
        || bsList.querySelector('.bs-new');
    row?.click();
});
beanSheet.addEventListener('click', e => {        // backdrop tap closes
    if (e.target === beanSheet) beanSheet.close();
});
bsList.addEventListener('click', async e => {
    const row = e.target.closest('.bs-row');
    if (!row) return;
    beanSheet.close();
    if (row.dataset.new) {
        const name = bsQ.value.trim();
        if (!name) return;
        const id = await DB.beans.add(
            { name, dose: doseVal(), ratio: ratioVal() });
        openBeans.add(id);
        await renderBeans();
        selectBean(id);
        toast(`已添加「${name}」`);
        buzz(15);
        return;
    }
    selectBean(row.dataset.id === '' ? null : +row.dataset.id);
    buzz(8);
});

// bean search on the 豆子 page — debounced, paints from the cached
// lists instead of re-reading IndexedDB on every keystroke
let beanQT = 0;
$('bean-q').addEventListener('input', () => {
    if (!beansLoaded) return;
    clearTimeout(beanQT);
    beanQT = setTimeout(paintBeans, 150);
});

// ghost reference on the live chart: the selected bean's ♥ brew,
// else its newest. Anchored to the brew timer, not wall time.
function pickGhost() {
    const b = curBeanId();
    const list = b === null ? [] : brewCache.filter(w => w.beanId === b);
    const ref = list.find(w => w.fav) || list[0];
    chart.setGhost(ref || null);
    $('chip-ref').hidden = !ref;
}

// auto-timer toggle persists; default on — a pour without the timer
// records nothing.
const autoTimerEl = $('autotimer-toggle');
autoTimerEl.checked = localStorage.getItem('auto-timer') !== '0';
autoTimerEl.onchange = () =>
    localStorage.setItem('auto-timer', autoTimerEl.checked ? '1' : '0');

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

new ResizeObserver(() => {
    const el = $('chart');
    if (chart.visible && el.clientWidth && el.clientHeight)
        chart.resize(el.clientWidth, el.clientHeight);
}).observe($('chart'));

// theme change — rebuild the uPlot instances with the new palette.
// A manual override (数据页) pins the palette, so OS flips are ignored then.
matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    if (document.documentElement.dataset.theme) return;
    chart.retheme();
    paintBeans();
});

// '' = follow the OS; the early inline script applies the saved value
// before first paint, this only needs to reflect it in the picker
const themeSel = $('theme-sel');
{
    const t = localStorage.getItem('theme');
    themeSel.value = t === 'light' || t === 'dark' ? t : '';
}
themeSel.onchange = () => {
    const v = themeSel.value;
    if (v) document.documentElement.dataset.theme = v;
    else delete document.documentElement.dataset.theme;
    localStorage.setItem('theme', v);
    // the theme-color metas are media-keyed — force the matching one
    $('tc-l').media = v === 'dark' ? 'not all'
        : v === 'light' ? 'all' : '(prefers-color-scheme: light)';
    $('tc-d').media = v === 'dark' ? 'all'
        : v === 'light' ? 'not all' : '(prefers-color-scheme: dark)';
    chart.retheme();
    paintBeans();
};

// --- main loop -----------------------------------------------------------------

function frame() {
    if (Screen.pump()) blit();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
