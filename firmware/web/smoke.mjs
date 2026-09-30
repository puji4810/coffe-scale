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

if (failures) { console.error(`${failures} FAILED`); process.exit(1); }
console.log('all checks passed');
