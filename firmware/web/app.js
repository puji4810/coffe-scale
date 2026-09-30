// coffee-scale web app — mirror client for the on-device UI.
//   WebSocket to the scale: {snap,displayValue,batteryPct,charging} @20 Hz
//   out, {"cmd":...} in. The canvas runs the device's real LVGL UI
//   (scale_screen.wasm), so the mirror is pixel-identical to the ST7789.
//   Chart is vendored uPlot.

import ScaleScreenFactory from './dist/scale_screen.mjs';
import { ScaleChart } from './chart.js';

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

let ws = null;

const $ = id => document.getElementById(id);
const connEl = $('conn');

// --- snapshot -> UI ----------------------------------------------------------

const chart = new ScaleChart($('chart'));
const UNIT_LABEL = ['g', 'oz'];
const MODE_LABEL = ['WEIGH', 'BREW'];

function showFrame(m, tMs) {
    Screen.update(m);                     // canvas mirror
    const s = m.snap;
    chart.push(tMs, s.grams, s.flowGps);
    $('v-weight').textContent = `${m.displayValue.toFixed(1)} ${UNIT_LABEL[s.unit]}`;
    $('v-flow').textContent = `${s.flowGps.toFixed(1)} g/s`;
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

function cmd(c) {
    if (!ws || ws.readyState !== WebSocket.OPEN) { setConn('offline — connect first'); return; }
    ws.send(JSON.stringify({ cmd: c }));
}

$('btn-tare').onclick = () => cmd('tare');
$('btn-long').onclick = () => cmd('long');
$('btn-mode').onclick = () => cmd('mode');
$('sel-unit').onchange = e => cmd(`unit${e.target.value}`);
$('btn-calzero').onclick = () => cmd('calzero');
$('btn-calspan').onclick = () => cmd(`calspan:${$('cal-mass').value || 100}`);
$('btn-sleep').onclick = () => {
    if (confirm('进入低功耗？秤会关屏、关 WiFi；双击秤体或按 MODE 键唤醒。')) {
        cmd('sleep');
    }
};
addEventListener('keydown', e => {
    if (e.target.tagName === 'INPUT') return;
    if (e.key === 't') cmd('tare');
    else if (e.key === 'l') cmd('long');
    else if (e.key === 'm') cmd('mode');
});

// --- websocket (mirror) -------------------------------------------------------

function setConn(txt, live) {
    connEl.textContent = txt;
    connEl.className = live ? 'live' : '';
}

function connect(url) {
    if (location.protocol === 'https:' && url.startsWith('ws:')) {
        setConn('https page can\'t open ws:// — use the device-hosted page or local http');
        return;
    }
    setConn('connecting…');
    ws = new WebSocket(url);
    ws.onopen = () => { chart.clear(); setConn('mirror · ' + url, true); };
    ws.onmessage = ev => {
        const m = JSON.parse(ev.data);
        showFrame(m, performance.now());
    };
    ws.onclose = () => { ws = null; $('btn-ws').textContent = 'Connect'; setConn('offline'); };
    ws.onerror = () => ws.close();
    $('btn-ws').textContent = 'Disconnect';
}

$('btn-ws').onclick = () => {
    if (ws) { ws.close(); return; }
    connect($('ws-url').value.trim() || `ws://${location.host || '192.168.4.1'}/ws`);
};

// the device-hosted page knows its own host — connect straight away
if (location.protocol === 'http:' && location.host) {
    const url = `ws://${location.host}/ws`;
    $('ws-url').value = url;
    connect(url);
}

// --- recording ---------------------------------------------------------------

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
        $('btn-rec').classList.add('rec-on');
        $('rec-info').textContent = auto ? 'auto-recording (brew timer)' : 'recording';
    },
    stop() {
        this.on = false; this.armed = false;
        $('btn-rec').classList.remove('rec-on');
        $('rec-info').textContent = `${this.frames.length} frames`;
        $('btn-dl').disabled = this.frames.length === 0;
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
