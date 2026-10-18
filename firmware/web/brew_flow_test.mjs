// Offline flow regression: node web/brew_flow_test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fitFlow, brewFlow, recordedCurves, flowStats } from './brew_flow.js';

const series = (seconds, weight) => {
    const t = Array.from({ length: seconds * 10 + 1 }, (_, i) => i / 10);
    return { t, w: t.map(weight) };
};
const near = (a, b, eps) => assert.ok(Math.abs(a - b) <= eps, `${a} ≉ ${b} ± ${eps}`);

test('constant ramps retain their rate, including the endpoints', () => {
    for (const rate of [0, 0.2, 3, 15]) {
        const { t, w } = series(10, x => 20 + rate * x);
        for (const f of fitFlow(t, w)) near(f, rate, 0.001);
    }
});

test('duplicate times and irregular sampling do not create spikes', () => {
    const t = [0, .1, .1, .2, .4, .5, .5, .6, .8, 1, 1.1, 1.3, 1.4, 1.5];
    const w = t.map(x => 3 * x);
    const f = fitFlow(t, w);
    f.forEach(v => near(v, 3, .001));
    assert.equal(f[1], f[2]);
});

test('bounded vibration and isolated outliers do not become pour peaks', () => {
    const { t, w } = series(15, x => 3 * x + .4 * Math.sin(x * 23));
    w[70] += 5;
    const f = fitFlow(t, w);
    for (let i = 20; i < f.length - 20; i++) near(f[i], 3, .3);
});

test('start and stop stay within the smoothing window; rest does not ramp', () => {
    const { t, w } = series(20, x => 3 * Math.max(0, Math.min(8, x - 4)));
    const f = fitFlow(t, w);
    t.forEach((x, i) => {
        if (x < 2 || x > 14) near(f[i], 0, .001);
        if (x > 6 && x < 10) near(f[i], 3, .001);
        assert.ok(f[i] >= -.001 && f[i] <= 3.001);
    });
    let area = 0;
    for (let i = 1; i < t.length; i++) area += (f[i] + f[i - 1]) / 2 * (t[i] - t[i - 1]);
    near(area, 24, .1);
});

test('load changes, gaps, pauses and invalid samples split the fit', () => {
    const { t, w } = series(12, x => 3 * x + (x >= 6 ? 320 : 0));
    let f = fitFlow(t, w);
    assert.equal(f[60], null);
    for (let i = 10; i < 50; i++) near(f[i], 3, .001);
    for (let i = 70; i < 110; i++) near(f[i], 3, .001);
    // A paused timer hides the wall-time gap: explicit breaks must still work.
    const afterPause = w.map((v, i) => v - (i >= 60 ? 318 : 0));
    f = fitFlow(t, afterPause, [60]);
    assert.equal(f[60], null);
    near(f[70], 3, .001);
    const gapped = t.map((v, i) => v + (i >= 60 ? 10 : 0));
    f = fitFlow(gapped, w);
    assert.equal(f[60], null);
    w[30] = null;
    f = fitFlow(t, w);
    assert.equal(f[30], null);
    assert.ok(f.every(x => x === null || Number.isFinite(x)));
    assert.deepEqual(fitFlow([], []), []);
    assert.deepEqual(fitFlow([0], [3]), [null]);
});

test('saving fits from weight, preserves raw flow, and marks pauses', () => {
    const frames = series(5, x => 3 * x).t.map((t, i) => ({
        t: t * 1000 + (i >= 25 ? 10000 : 0), breakBefore: i === 25,
        snap: { timerMs: t * 1000, grams: 3 * t, flowGps: i % 2 ? 0 : 20 },
    }));
    const curves = recordedCurves(frames);
    assert.equal(curves.f[0], 20);
    assert.deepEqual(curves.breaks, [25]);
    assert.equal(curves.flowFit.f[25], null);
    near(curves.flowFit.f[10], 3, .001);
    const original = JSON.stringify(curves);
    assert.equal(brewFlow(curves), curves.flowFit.f);
    assert.equal(JSON.stringify(curves), original);
    const legacy = { t: curves.t, w: curves.w, f: curves.f, breaks: curves.breaks };
    assert.deepEqual(brewFlow(legacy), curves.flowFit.f);
    assert.equal(legacy.flowFit, undefined);
    assert.equal(brewFlow(legacy), brewFlow(legacy)); // cached, never rewrites old data
    const stale = { ...legacy, flowFit: { version: -1, f: curves.f } };
    assert.deepEqual(brewFlow(stale), curves.flowFit.f);
});

test('statistics integrate time rather than overweighting repeated samples', () => {
    const stats = flowStats([0, 0, .1, .2], [1, 1, 3, 3]);
    near(stats.avg, 2.5, .001);
    assert.equal(stats.peak, 3);
});

for (const day of [15, 17]) {
    test(`real 2026-10-${day} brew has continuous flow through sustained growth`, async () => {
        const brew = JSON.parse(await readFile(new URL(`./testdata/brew-202610${day}.json`, import.meta.url)));
        const original = JSON.stringify(brew);
        const f = brewFlow(brew);
        const [lo, hi] = day === 15 ? [85, 115] : [45, 65];
        const idx = brew.t.map((t, i) => t >= lo && t <= hi ? i : -1).filter(i => i >= 0);
        const rates = idx.map(i => f[i]);
        assert.ok(rates.every(x => x !== null && x > 1.5 && x < 5), `${Math.min(...rates)}..${Math.max(...rates)}`);
        assert.ok(brew.f.some((v, i) => v === 0 && brew.t[i] >= 85 && brew.t[i] <= 115));
        let area = 0;
        for (let j = 1; j < idx.length; j++) {
            const i = idx[j], prev = idx[j - 1];
            area += (f[i] + f[prev]) / 2 * (brew.t[i] - brew.t[prev]);
        }
        const gain = brew.w[idx.at(-1)] - brew.w[idx[0]];
        near(area, gain, 2);
        assert.equal(JSON.stringify(brew), original);
        if (day === 15) {
            const moved = brew.t.map((t, i) => t >= 23 && t <= 40 ? f[i] : null).filter(x => x !== null);
            assert.ok(Math.max(...moved) < 2, 'moving the dripper must not become a giant pour');
        }
        console.log(`10/${day}: ${lo}–${hi}s fit ${Math.min(...rates).toFixed(2)}..${Math.max(...rates).toFixed(2)} g/s, integral ${area.toFixed(2)}g / gain ${gain.toFixed(2)}g`);
    });
}
