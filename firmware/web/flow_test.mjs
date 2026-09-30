// Node tests for flow.js — synthetic 20 Hz brew-like series.
// Run: node flow_test.mjs

import { cleanSeries } from './flow.js';

let failures = 0;
function check(name, cond, extra = '') {
    console.log(`${cond ? 'ok  ' : 'FAIL'}  ${name} ${extra}`);
    if (!cond) failures++;
}

// deterministic ±0.5 g pseudo-noise (LCG)
let seed = 0x5eed;
const noise = () => {
    seed = (seed * 1103515245 + 12345) % 0x80000000;
    return (seed / 0x80000000 - 0.5);
};

const DT = 0.05;
const t = [], w = [];
// 2 s rest at 0, 10 s pour at 4 g/s, 5 s rest at 40,
// 1.5 s disturbance alternating ±25 g around 40, 5 s rest
for (let i = 0; i < 440; i++) {
    const s = i * DT;
    t.push(s);
    let v;
    if (s < 2) v = 0;
    else if (s < 12) v = 4 * (s - 2);
    else if (s < 17) v = 40;
    else if (s < 18.5) v = 40 + (i % 2 ? 25 : -25);
    else v = 40;
    w.push(v + noise());
}

const r = cleanSeries(t, w);
const at = s => Math.round(s / DT);

// pour interior (1 s margin from each edge): clean, flow ≈ 4 g/s
let badInPour = 0, flowOk = true, flowMin = 99, flowMax = -99;
for (let i = at(3); i < at(11); i++) {
    if (r.bad[i]) badInPour++;
    flowMin = Math.min(flowMin, r.f[i]);
    flowMax = Math.max(flowMax, r.f[i]);
    if (Math.abs(r.f[i] - 4) > 0.4) flowOk = false;
}
check('no bad flags in pour interior', badInPour === 0, `(${badInPour})`);
check('pour flow ≈ 4 ± 0.4 g/s', flowOk, `[${flowMin.toFixed(2)},${flowMax.toFixed(2)}]`);

// disturbance span fully flagged
let badInDist = 0, distN = 0;
for (let i = at(17.1); i <= at(18.4); i++) { distN++; if (r.bad[i]) badInDist++; }
check('disturbance flagged', badInDist === distN, `(${badInDist}/${distN})`);

// cleaned weight inside the disturbance stays within ±1.5 g of 40
let worst = 0;
for (let i = at(17); i <= at(18.5); i++) {
    worst = Math.max(worst, Math.abs(r.w[i] - 40));
}
check('bridged w within ±1.5 g of rest', worst <= 1.5, `(max ${worst.toFixed(2)})`);

// flow reads 0 on rest + disturbance, except within 1.2 s of pour edges
let stray = 0, strayAt = -1;
for (let i = 0; i < t.length; i++) {
    const s = t[i];
    const inPour = s >= 2 && s < 12;
    const nearEdge = Math.abs(s - 2) <= 1.2 || Math.abs(s - 12) <= 1.2;
    if (!inPour && !nearEdge && r.f[i] !== 0) { stray++; strayAt = s; }
}
check('flow 0 outside pour (±1.2 s of edges)', stray === 0,
      `(${stray} stray, last at ${strayAt}s)`);

if (failures) { console.error(`${failures} FAILED`); process.exit(1); }
console.log('all flow tests passed');
