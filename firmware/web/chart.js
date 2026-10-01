// Rolling weight/flow chart on uPlot (vendored, global `uPlot`).
// X axis is seconds since first pushed frame; left axis = weight, right = flow.
// Plotted values are the device's own numbers — firmware flow is already
// Kalman-filtered with impact rejection.
// Colors come from the app's CSS custom properties so both themes work;
// call retheme() after a prefers-color-scheme change to rebuild.

function theme() {
    const cs = getComputedStyle(document.documentElement);
    const v = n => cs.getPropertyValue(n).trim();
    return {
        ink: v('--ink') || '#1F2326',
        amber: v('--amber-fill') || '#E8A33D',
        grid: v('--rule') || '#B7BCB5',
        dim: v('--dim') || '#5E6660',
        ref: v('--ref') || '#4E86B8',
        font: '12px "Barlow Semi Condensed", "PingFang SC", system-ui, sans-serif',
    };
}

function fmtTime(v) {
    const s = Math.round(v);
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

/// uPlot series share one x array — a second brew's points have their
/// own timestamps. Interleaving them into the x array would punch null
/// gaps into the weight/flow columns at every ghost point and make the
/// solid lines render as dots, so resample the ghost onto the existing
/// x grid instead (linear interpolation, null outside its range).
function resample(x, g, base = 0) {
    const t = g.t, w = g.w, n = t.length;
    const G = new Array(x.length).fill(null);
    let j = 0;
    for (let i = 0; i < x.length; i++) {
        const gt = x[i] - base;
        if (gt < t[0] || gt > t[n - 1]) continue;
        while (j < n - 2 && t[j + 1] < gt) j++;
        const t0 = t[j], t1 = t[j + 1];
        G[i] = gt === t0 || t1 === t0 ? w[j]
             : w[j] + (w[j + 1] - w[j]) * (gt - t0) / (t1 - t0);
    }
    return G;
}

function chartOpts(el, c) {
    return {
        width: el.clientWidth,
        height: el.clientHeight,
        padding: [8, 8, 0, 0],
        font: c.font,
        scales: {
            x: { time: false },
            // 0-line is a hard floor: a negative excursion (lifting a
            // tared cup) clips at the plot edge instead of re-ranging the
            // axis — the fixed-position flow curve would otherwise land
            // inside negative axis labels and read as "flow below zero".
            w: { range: { min: { hard: 0, soft: 0, mode: 1, pad: 0.05 },
                          max: { soft: 50, mode: 1, pad: 0.08 } } },
            // flow is physically bounded (clipped at ±30 g/s), so a fixed
            // axis keeps every value at a stable position.
            f: { range: [-10, 40] },
        },
        series: [
            {},
            { label: 'g', scale: 'w', stroke: c.ink, width: 2,
              points: { show: false } },
            { label: 'g/s', scale: 'f', stroke: c.amber, width: 1.5,
              points: { show: false } },
            { label: '参考 g', scale: 'w', stroke: c.ref, width: 1.5,
              dash: [6, 4], points: { show: false } },
        ],
        axes: [
            { stroke: c.dim, grid: { stroke: c.grid }, font: c.font,
              values: (u, s) => s.map(fmtTime) },
            // axis label color matches its series: ink = weight (left),
            // amber = flow (right)
            { scale: 'w', stroke: c.ink, grid: { stroke: c.grid }, font: c.font,
              size: 56 },
            { scale: 'f', side: 1, stroke: c.amber, grid: { show: false },
              font: c.font, size: 48 },
        ],
        legend: { show: true, live: true },
        cursor: { show: true, drag: { x: false, y: false } },
    };
}

export class ScaleChart {
    constructor(el, windowS = 600) {
        this.windowS = windowS;
        this.t0 = null;                 // ms of first frame
        this.lastT = -Infinity;
        this.el = el;
        this.x = []; this.w = []; this.f = [];

        const opts = chartOpts(el, theme());
        opts.hooks = { setCursor: [u => this.updateTip(u)] };
        this.u = new uPlot(opts, [[], [], [], []], el);

        this.ghost = null;              // brew {t[],w[]} to overlay
        this.ghostBase = null;          // live-x seconds where ghost t=0 lands
        this.lastData = [[], [], [], []];

        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);

        this.visible = true;            // hidden = collect, don't redraw
    }

    /// Reference brew overlay — pass a saved brew {t,w} or null. The
    /// ghost is drawn once the brew timer anchors its t=0 via ghostBase;
    /// before that it stays hidden (there is no meaningful alignment).
    setGhost(brew) {
        this.ghost = brew;
        this.ghostBase = null;
        if (this.visible) this.redraw();
    }

    /// Live-chart seconds for a raw frame timestamp (null until t0).
    secOf(t) { return this.t0 === null ? null : (t - this.t0) / 1000; }

    data4() {
        if (this.ghost && this.ghostBase !== null)
            return [this.x, this.w, this.f,
                    resample(this.x, this.ghost, this.ghostBase)];
        return [this.x, this.w, this.f, this.x.map(() => null)];
    }

    redraw() {
        const d = this.data4();
        this.lastData = d;
        this.u.setData(d);
    }

    /// Rebuild the uPlot instance with the current theme, keeping the
    /// rolling x/w/f buffers (prefers-color-scheme changed).
    retheme() {
        this.u.destroy();
        const opts = chartOpts(this.el, theme());
        opts.hooks = { setCursor: [u => this.updateTip(u)] };
        this.u = new uPlot(opts, this.data4(), this.el);
        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);
        this.u.root.style.visibility = this.visible ? '' : 'hidden';
    }

    /// Show/hide. While hidden push() still records the rolling window but
    /// skips setData — no redraw cost. Re-showing flushes the backlog in
    /// one pass so the full history reappears.
    setVisible(v) {
        this.visible = v;
        if (v) {
            this.u.root.style.visibility = '';
            const el = this.u.root.parentElement;
            this.u.setSize({ width: el.clientWidth,
                             height: el.clientHeight });
            this.redraw();
        } else {
            this.u.root.style.visibility = 'hidden';
        }
    }

    updateTip(u) {
        const i = u.cursor.idx;
        if (!this.tip) return;            // hook can fire during construction
        const d = this.lastData;
        if (i == null || i >= d[0].length) {
            this.tip.style.display = 'none';
            return;
        }
        this.tip.style.display = 'block';
        const v = (x, u) => x == null ? '' : `  ${x.toFixed(1)} ${u}`;
        this.tip.textContent = fmtTime(d[0][i]) + v(d[1][i], 'g') +
            v(d[2][i], 'g/s') +
            (d[3][i] == null ? '' : `  参考 ${d[3][i].toFixed(1)} g`);
        const bw = u.bbox.width / devicePixelRatio;
        const left = Math.min(u.cursor.left + 12, bw - this.tip.offsetWidth - 4);
        this.tip.style.left = `${Math.max(0, left)}px`;
        this.tip.style.top = '6px';
    }

    /// t in ms (any monotone clock — first call anchors t0).
    push(t, grams, flow) {
        // the caller's clock domain can switch (demo vt vs performance.now)
        if (this.t0 !== null && t < this.lastT) this.clear();
        this.lastT = t;
        if (this.t0 === null) this.t0 = t;
        const s = (t - this.t0) / 1000;
        const x = this.x;
        x.push(s); this.w.push(grams); this.f.push(flow);
        // trim to the rolling window
        const cut = s - this.windowS;
        let i = 0;
        while (i < x.length && x[i] < cut) i++;
        if (i) { x.splice(0, i); this.w.splice(0, i); this.f.splice(0, i); }
        if (this.visible) this.redraw();
    }

    clear() {
        this.t0 = null;
        this.lastT = -Infinity;
        this.x.length = this.w.length = this.f.length = 0;
        this.ghostBase = null;
        this.u.setData([[], [], [], []]);
        this.tip.style.display = 'none';
    }

    resize(w, h) { this.u.setSize({ width: w, height: h }); }
}

/// Static weight/flow curve for a saved brew — same axes/series language
/// as the live chart, but a plain one-shot plot for the bean library.
/// Plots the stored raw weight and stored device flow verbatim. Pass a
/// second brew as `other` to overlay its weight curve for comparison.
export function brewPlot(el, t, w, f, other = null) {
    const c = theme();
    // union x so a longer comparison brew keeps its tail; the main
    // curve just ends early (trailing nulls = line stops, not dots)
    const X = other && other.t[other.t.length - 1] > t[t.length - 1]
        ? t.concat(other.t.filter(v => v > t[t.length - 1]))
        : t;
    const pad = a => a.length === X.length ? a
        : a.concat(new Array(X.length - a.length).fill(null));
    const data = [X, pad(w), pad(f),
                  other ? resample(X, other) : X.map(() => null)];
    return new uPlot({
        width: el.clientWidth,
        height: 190,
        padding: [8, 8, 0, 0],
        font: c.font,
        scales: {
            x: { time: false },
            w: { range: { min: { hard: 0, soft: 0, mode: 1, pad: 0.05 },
                          max: { soft: 50, mode: 1, pad: 0.08 } } },
            f: { range: [-10, 40] },
        },
        series: [
            {},
            { label: 'g', scale: 'w', stroke: c.ink, width: 2,
              points: { show: false } },
            { label: 'g/s', scale: 'f', stroke: c.amber, width: 1.5,
              points: { show: false } },
            { label: '参考 g', scale: 'w', stroke: c.ref, width: 1.5,
              dash: [6, 4], points: { show: false } },
        ],
        axes: [
            { stroke: c.dim, grid: { stroke: c.grid }, font: c.font,
              values: (u, s) => s.map(fmtTime) },
            { scale: 'w', stroke: c.ink, grid: { stroke: c.grid }, font: c.font,
              size: 50 },
            { scale: 'f', side: 1, stroke: c.amber, grid: { show: false },
              font: c.font, size: 42 },
        ],
        legend: { show: !!other },
        cursor: { show: true, drag: { x: false, y: false } },
    }, data, el);
}
