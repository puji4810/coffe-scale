// Saved-brew flow is the slope of a centered robust fit to weight, independent
// of the device's causal estimator. Times are seconds, rates are g/s. Retain
// signed slopes: clipping negative noise would invent net incoming mass.
const VERSION = 1, WINDOW_S = 3, RADIUS = WINDOW_S / 2;
const cache = new WeakMap();

function median(a) {
    const s = a.slice().sort((x, y) => x - y), n = s.length;
    return n % 2 ? s[n >> 1] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

function slope(points, from, to, center) {
    if (to - from < 4 || points[to - 1].t - points[from].t < .3) return null;
    const rows = points.slice(from, to).map((p, k) => {
        const x = p.t - center, d = Math.abs(x) / RADIUS;
        // Time-cell widths prevent a burst of notifications dominating a fit.
        const prev = points[Math.max(from, from + k - 1)].t;
        const next = points[Math.min(to - 1, from + k + 1)].t;
        const weight = (next - prev) / 2 * (1 - d ** 3) ** 3;
        return { x, y: p.w, weight, robust: 1 };
    });
    let a = 0, b = 0;
    for (let pass = 0; pass < 3; pass++) {
        let sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const r of rows) {
            const v = r.weight * r.robust;
            sw += v; sx += v * r.x; sy += v * r.y;
            sxx += v * r.x * r.x; sxy += v * r.x * r.y;
        }
        const denom = sw * sxx - sx * sx;
        if (denom < 1e-12) return null;
        b = (sw * sxy - sx * sy) / denom;
        a = (sy - b * sx) / sw;
        // Huber reweighting rejects isolated force spikes without flattening a
        // sustained change in pour rate. No pour-specific state machine.
        const residuals = rows.map(r => Math.abs(r.y - a - b * r.x));
        const limit = Math.max(.03, median(residuals) * 1.4826 * 1.5);
        rows.forEach((r, i) => { r.robust = Math.min(1, limit / (residuals[i] || 1e-12)); });
    }
    return +b.toFixed(3);
}

export function fitFlow(t, w, breaks = []) {
    const out = new Array(t.length).fill(null), cut = new Set(breaks);
    let segment = [], splitStart = false;
    function flush(splitEnd) {
        let from = 0, to = 0;
        for (let i = 0; i < segment.length; i++) {
            const p = segment[i];
            while (segment[from].t < p.t - RADIUS) from++;
            while (to < segment.length && segment[to].t <= p.t + RADIUS) to++;
            const f = slope(segment, from, to, p.t);
            for (const index of p.indices) out[index] = f;
        }
        // Explicit nulls break the plotted line at a discontinuity. They are
        // missing evidence, never an assertion of zero flow.
        if (splitStart && segment.length)
            segment[0].indices.forEach(i => { out[i] = null; });
        if (splitEnd && segment.length)
            segment.at(-1).indices.forEach(i => { out[i] = null; });
        segment = [];
    }
    for (let i = 0; i < t.length; i++) {
        if (!Number.isFinite(t[i]) || !Number.isFinite(w[i])) {
            flush(true); splitStart = true; continue;
        }
        let prev = segment.at(-1);
        const dt = prev ? t[i] - prev.t : 0;
        // Gross load changes are not water. Be conservative: ordinary fast
        // pours and small disturbances remain the robust fit's responsibility.
        if (prev && (cut.has(i) || dt < 0 || dt > .5 ||
            (Math.abs(w[i] - prev.w) > 8 && Math.abs(w[i] - prev.w) > 30 * dt))) {
            flush(true); splitStart = true; prev = null;
        }
        if (prev && dt === 0) {
            prev.indices.push(i);
            prev.w += (w[i] - prev.w) / prev.indices.length;
        } else {
            segment.push({ t: t[i], w: w[i], indices: [i] });
        }
    }
    flush(false);
    return out;
}

export function brewFlow(brew) {
    const saved = brew.flowFit;
    if (saved?.version === VERSION && saved.windowS === WINDOW_S &&
        Array.isArray(saved.f) && saved.f.length === brew.t?.length &&
        saved.f.every(v => v === null || Number.isFinite(v))) return saved.f;
    if (!cache.has(brew)) cache.set(brew, fitFlow(brew.t || [], brew.w || [], brew.breaks));
    return cache.get(brew);
}

// Called once when the timer resets. f[] always stays the original device
// reading; the derived curve has its own versioned field and survives export.
export function recordedCurves(frames) {
    const t0 = frames[0]?.snap.timerMs || 0;
    const t = [], w = [], f = [], breaks = [];
    frames.forEach((m, i) => {
        t.push(+((m.snap.timerMs - t0) / 1000).toFixed(2));
        w.push(+m.snap.grams.toFixed(2));
        f.push(+m.snap.flowGps.toFixed(2));
        if (i && (m.breakBefore || (Number.isFinite(m.t) &&
            Number.isFinite(frames[i - 1].t) && m.t - frames[i - 1].t > 500))) breaks.push(i);
    });
    return { t, w, f, breaks,
        flowFit: { version: VERSION, windowS: WINDOW_S, f: fitFlow(t, w, breaks) } };
}

export function flowStats(t, f) {
    let peak = 0, area = 0, duration = 0;
    for (let i = 0; i < t.length; i++) {
        if (Number.isFinite(f[i])) peak = Math.max(peak, f[i]);
        const dt = t[i] - t[i - 1];
        if (i && dt > 0 && dt <= .5 && Number.isFinite(f[i]) && Number.isFinite(f[i - 1])) {
            const rate = (f[i] + f[i - 1]) / 2;
            if (rate > .5) { area += rate * dt; duration += dt; }
        }
    }
    return { peak: +peak.toFixed(1), avg: duration ? +(area / duration).toFixed(1) : null };
}
