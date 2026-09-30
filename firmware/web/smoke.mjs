// Smoke test for the WASM builds — run after build.sh:
//   node smoke.mjs
// Verifies scale_core logic end-to-end and that the LVGL screen module
// actually renders pixels into the staging framebuffer.

import ScaleCoreFactory from './dist/scale_core.mjs';
import ScaleScreenFactory from './dist/scale_screen.mjs';

let failures = 0;
function check(name, cond, extra = '') {
    console.log(`${cond ? 'ok  ' : 'FAIL'}  ${name} ${extra}`);
    if (!cond) failures++;
}

const Core = await ScaleCoreFactory();
const app = new Core.ScaleApp();
app.loadCalibration(80000, 1600);

// 200 g on the pan, 80 SPS for 3 s of virtual time
for (let i = 0; i < 240; i++) {
    app.feed(Math.round(80000 + 200 * 1600), i * 12.5);
    app.feedAccel(0, 0, 1000);
}
const snap = app.snapshot();
check('grams ≈ 200', Math.abs(snap.grams - 200) < 1, `(${snap.grams})`);
check('calibrated', snap.calibrated === true);
check('mode=weigh', snap.mode === 0);
app.tare();
const s2 = app.snapshot();
check('tare → ~0 g', Math.abs(s2.grams) < 1, `(${s2.grams})`);
check('tared flag', s2.tared === true);
app.nextMode();
check('mode=brew', app.snapshot().mode === 1);
app.tareLong(); // starts brew timer
for (let i = 0; i < 160; i++) app.feed(80000 + i, 3000 + i * 12.5);
const s3 = app.snapshot();
check('timer running', s3.timerState === 1, `(state ${s3.timerState})`);
check('timer ~2 s', Math.abs(s3.timerMs - 2000) < 200, `(${s3.timerMs})`);

// display deadband: ±0.04 g noise straddling the tared level reads exactly 0
for (let i = 0; i < 100; i++) {
    app.feed(Math.round(400000 + (i % 2 ? 64 : -64)), 6000 + i * 12.5);
}
check('deadband → exact 0', app.snapshot().grams === 0,
      `(${app.snapshot().grams})`);

const Screen = await ScaleScreenFactory();
Screen.init();
Screen.update({
    snap: app.snapshot(),
    displayValue: app.displayValue(),
    batteryPct: 82,
    charging: false,
});
let pumped = false;
for (let i = 0; i < 50 && !pumped; i++) pumped = Screen.pump();
check('screen flushed', pumped);

const px = Screen.framebuffer(); // Uint32Array, RGBA little-endian
const W = Screen.width(), H = Screen.height();
check('fb size', px.length === W * H, `(${W}x${H})`);
let lit = 0;
for (let i = 0; i < px.length; i++) if ((px[i] & 0xffffff) !== 0x0b0b0b) lit++;
check('pixels drawn', lit > 500, `(${lit} lit px)`);

// changing the model must re-render — LVGL's refresh timer gates repaints to
// ~33 ms of (real) tick time, so spin briefly like the rAF loop does
Screen.update({ snap: app.snapshot(), displayValue: 123.4, batteryPct: -1, charging: true });
let again = false;
for (let i = 0; i < 30 && !again; i++) {
    await new Promise(r => setTimeout(r, 10));
    again = Screen.pump();
}
check('re-render dirty', again);

// --- BLE wire protocol bindings (scale_proto/proto.hpp layout) --------------

const uuids = Screen.bleUuids();
check('uuid service', uuids.service === 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c01', `(${uuids.service})`);
check('uuid state', uuids.state === 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c02', `(${uuids.state})`);
check('uuid command', uuids.command === 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c03', `(${uuids.command})`);

// hand-built 20-byte v1 frame: flags tared+calibrated+unit-oz, batt 82,
// seq 9, grams 200.50, display 7.074 oz, flow 1.25 g/s, pitch -2.0,
// roll 1.5, timer 61.4 s
const f = new Uint8Array(20);
const dv = new DataView(f.buffer);
f[0] = 1;
f[1] = 0b01010110; // stable=0 tared=1 calibrated=1 charging=0 unit=1 mode=0 tstate=01
f[2] = 82;
f[3] = 9;
dv.setInt32(4, 20050, true);
dv.setInt32(8, 7074, true);
dv.setInt16(12, 125, true);
dv.setInt16(14, -20, true);
dv.setInt16(16, 15, true);
dv.setUint16(18, 614, true);
const m = Screen.decodeFrame(f);
check('decodeFrame non-null', m !== null);
if (m) {
    check('frame grams', Math.abs(m.snap.grams - 200.5) < 1e-6, `(${m.snap.grams})`);
    check('frame display', Math.abs(m.displayValue - 7.074) < 1e-6, `(${m.displayValue})`);
    check('frame flow', Math.abs(m.snap.flowGps - 1.25) < 1e-6, `(${m.snap.flowGps})`);
    check('frame pitch', Math.abs(m.snap.pitchDeg - -2.0) < 1e-6, `(${m.snap.pitchDeg})`);
    check('frame roll', Math.abs(m.snap.rollDeg - 1.5) < 1e-6, `(${m.snap.rollDeg})`);
    check('frame timer', m.snap.timerMs === 61400, `(${m.snap.timerMs})`);
    check('frame batt', m.batteryPct === 82);
    check('frame flags', m.snap.tared && m.snap.calibrated && !m.snap.stable && !m.charging);
    check('frame unit/mode', m.snap.unit === 1 && m.snap.mode === 0 && m.snap.timerState === 1);
}
check('decodeFrame(short)=null', Screen.decodeFrame(f.subarray(0, 5)) === null);
const badVer = new Uint8Array(f); badVer[0] = 9;
check('decodeFrame(badver)=null', Screen.decodeFrame(badVer) === null);

const enc = Screen.encodeCommand(7, 12345); // cal_span 123.45 g
check('encodeCommand calspan', enc.length === 5 &&
      enc[0] === 7 && enc[1] === 0x39 && enc[2] === 0x30 && enc[3] === 0 && enc[4] === 0,
      `(${Array.from(enc)})`);
const encU = Screen.encodeCommand(5, 1);
check('encodeCommand unit', encU.length === 2 && encU[0] === 5 && encU[1] === 1);
const encT = Screen.encodeCommand(1, 0);
check('encodeCommand tare', encT.length === 1 && encT[0] === 1);

if (failures) { console.error(`${failures} FAILED`); process.exit(1); }
console.log('all checks passed');
