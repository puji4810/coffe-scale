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

// floating readout pinned to the cursor — time + each series' value.
// Shared by the live chart and saved-brew plots; on touch screens a
// horizontal drag drives it (drag-zoom is disabled, see cursor.drag).
function chartTip(u, tip, data) {
    const i = u.cursor.idx;
    if (i == null || i >= data[0].length) {
        tip.style.display = 'none';
        return;
    }
    tip.style.display = 'block';
    const v = (x, un) => x == null ? '' : `  ${x.toFixed(1)} ${un}`;
    tip.textContent = fmtTime(data[0][i]) + v(data[1][i], 'g')
        + v(data[2][i], 'g/s')
        + (data[3]?.[i] == null ? '' : `  参考 ${data[3][i].toFixed(1)} g`);
    const bw = u.bbox.width / devicePixelRatio;
    const left = Math.min(u.cursor.left + 12, bw - tip.offsetWidth - 4);
    tip.style.left = `${Math.max(0, left)}px`;
    tip.style.top = '6px';
}

function mountTip(u, data) {
    const tip = document.createElement('div');
    tip.className = 'chart-tip';
    tip.style.display = 'none';
    u.over.appendChild(tip);
    return u.hooks.setCursor.push(u => chartTip(u, tip, data));
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

function chartOpts(el, c, getTarget) {
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
            // max must clear the target line too, not just the data.
            w: { range: (u, lo, hi) => {
                     const mx = Math.max(hi ?? 0, getTarget?.() || 0);
                     return [0, Math.ceil(mx * 1.06) || 50];
                 } },
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

        this.c = theme();
        const opts = chartOpts(el, this.c, () => this.target);
        opts.hooks = { setCursor: [u => this.updateTip(u)],
                       draw: [u => this.drawTarget(u)] };
        this.u = new uPlot(opts, [[], [], [], []], el);

        this.ghost = null;              // brew {t[],w[]} to overlay
        this._ghostBase = null;         // live-x seconds where ghost t=0 lands
        this.g = [];                    // ghost column, kept parallel to x/w/f
        this.gi = 0;                    // amortized segment cursor into ghost.t
        this.lastData = [[], [], [], []];

        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);

        this.target = null;             // target liquid weight, g (dashed line)
        this.visible = true;            // hidden = collect, don't redraw
    }

    /// Horizontal target line on the weight axis (dose × ratio). Null hides.
    setTarget(v) {
        this.target = v;
        if (this.visible) this.redraw();
    }

    drawTarget(u) {
        const t = this.target;
        if (!t || t <= 0) return;
        const y = u.valToPos(t, 'w', true);
        const ctx = u.ctx;
        const dim = this.c.dim;
        ctx.save();
        ctx.strokeStyle = dim;
        ctx.setLineDash([3, 5]);
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(u.bbox.left, y);
        ctx.lineTo(u.bbox.left + u.bbox.width, y);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.font = this.c.font;
        ctx.fillStyle = dim;
        ctx.textAlign = 'right';
        ctx.fillText(`目标 ${Math.round(t)} g`,
                     u.bbox.left + u.bbox.width - 4, y - 5);
        ctx.restore();
    }

    /// Reference brew overlay — pass a saved brew {t,w} or null. The
    /// ghost is drawn once the brew timer anchors its t=0 via ghostBase;
    /// before that it stays hidden (there is no meaningful alignment).
    setGhost(brew) {
        this.ghost = brew;
        this.ghostBase = null;          // setter refills the column + redraws
    }

    get ghostBase() { return this._ghostBase; }
    set ghostBase(v) {
        this._ghostBase = v;
        this.gi = 0;
        this.refillGhost();
        if (this.visible && !document.hidden) this.redraw();
    }

    /// Rebuild the whole ghost column — runs only when the brew or its
    /// anchor changes; the per-frame path appends through ghostAt().
    refillGhost() {
        this.g = (this.ghost && this._ghostBase !== null)
            ? resample(this.x, this.ghost, this._ghostBase)
            : new Array(this.x.length).fill(null);
    }

    /// Interpolated ghost weight at live-x second s — x is monotone, so
    /// the segment cursor amortizes to O(1) per push.
    ghostAt(s) {
        const g = this.ghost;
        if (!g || this._ghostBase === null) return null;
        const t = g.t, n = t.length, gt = s - this._ghostBase;
        if (n < 2 || gt < t[0] || gt > t[n - 1]) return null;
        let j = this.gi;
        while (j < n - 2 && t[j + 1] < gt) j++;
        this.gi = j;
        const t0 = t[j], t1 = t[j + 1];
        return t1 === t0 ? g.w[j]
             : g.w[j] + (g.w[j + 1] - g.w[j]) * (gt - t0) / (t1 - t0);
    }

    /// Live-chart seconds for a raw frame timestamp (null until t0).
    secOf(t) { return this.t0 === null ? null : (t - this.t0) / 1000; }

    data4() { return [this.x, this.w, this.f, this.g]; }

    redraw() {
        const d = this.data4();
        this.lastData = d;
        this.u.setData(d);
    }

    /// Rebuild the uPlot instance with the current theme, keeping the
    /// rolling x/w/f buffers (prefers-color-scheme changed).
    retheme() {
        this.u.destroy();
        this.c = theme();
        const opts = chartOpts(this.el, this.c, () => this.target);
        opts.hooks = { setCursor: [u => this.updateTip(u)],
                       draw: [u => this.drawTarget(u)] };
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
        if (!this.tip) return;            // hook can fire during construction
        chartTip(u, this.tip, this.lastData);
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
        this.g.push(this.ghostAt(s));
        // trim to the rolling window
        const cut = s - this.windowS;
        let i = 0;
        while (i < x.length && x[i] < cut) i++;
        if (i) {
            x.splice(0, i); this.w.splice(0, i);
            this.f.splice(0, i); this.g.splice(0, i);
        }
        // hidden tabs still buffer — the caller flushes on visibilitychange
        if (this.visible && !document.hidden) this.redraw();
    }

    clear() {
        this.t0 = null;
        this.lastT = -Infinity;
        this.x.length = this.w.length = this.f.length = this.g.length = 0;
        this._ghostBase = null;
        this.gi = 0;
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
    const u = new uPlot({
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
        hooks: { setCursor: [] },        // mountTip fills it after creation
    }, data, el);
    mountTip(u, data);
    return u;
}
