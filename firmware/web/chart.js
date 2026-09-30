// Rolling weight/flow chart on uPlot (vendored, global `uPlot`).
// X axis is seconds since first pushed frame; left axis = weight, right = flow.
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
        font: '12px "Barlow Semi Condensed", "PingFang SC", system-ui, sans-serif',
    };
}

function fmtTime(v) {
    const s = Math.round(v);
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

function chartOpts(el, c) {
    return {
        width: el.clientWidth,
        height: el.clientHeight,
        padding: [8, 8, 0, 0],
        font: c.font,
        scales: {
            x: { time: false },
            // 0-line stays visible, negatives still allowed through
            // (soft), resting view keeps a sensible 50 g window (soft max).
            w: { range: { min: { soft: 0, mode: 1, pad: 0.05 },
                          max: { soft: 50, mode: 1, pad: 0.08 } } },
            // flow is physically bounded (core clips the readout at
            // ±30 g/s), so a fixed axis keeps every value at a stable
            // position: resting 0 at 20%, a 15 g/s pour mid-chart, a
            // step-load spike pinned at ~80% — never hugging the top
            // edge where it would read as the weight axis' max.
            f: { range: [-10, 40] },
        },
        series: [
            {},
            { label: 'g', scale: 'w', stroke: c.ink, width: 2,
              points: { show: false } },
            { label: 'g/s', scale: 'f', stroke: c.amber, width: 1.5,
              points: { show: false } },
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
        this.x = []; this.w = []; this.f = [];
        this.el = el;

        const opts = chartOpts(el, theme());
        opts.hooks = { setCursor: [u => this.updateTip(u)] };
        this.u = new uPlot(opts, [[], [], []], el);

        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);

        this.visible = true;            // hidden = collect, don't redraw
    }

    /// Rebuild the uPlot instance with the current theme, keeping the
    /// rolling x/w/f buffers (prefers-color-scheme changed).
    retheme() {
        this.u.destroy();
        const opts = chartOpts(this.el, theme());
        opts.hooks = { setCursor: [u => this.updateTip(u)] };
        this.u = new uPlot(opts, [this.x, this.w, this.f], this.el);
        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);
        if (!this.visible) this.u.root.style.visibility = 'hidden';
        else this.u.root.style.visibility = '';
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
            this.u.setData([this.x, this.w, this.f]);
        } else {
            this.u.root.style.visibility = 'hidden';
        }
    }

    updateTip(u) {
        const i = u.cursor.idx;
        if (!this.tip) return;            // hook can fire during construction
        if (i == null || i >= this.x.length) {
            this.tip.style.display = 'none';
            return;
        }
        this.tip.style.display = 'block';
        this.tip.textContent =
            `${fmtTime(this.x[i])}  ${this.w[i].toFixed(1)} g  ${this.f[i].toFixed(1)} g/s`;
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
        if (this.visible) this.u.setData([x, this.w, this.f]);
    }

    clear() {
        this.t0 = null;
        this.lastT = -Infinity;
        this.x.length = this.w.length = this.f.length = 0;
        this.u.setData([[], [], []]);
        this.tip.style.display = 'none';
    }

    resize(w, h) { this.u.setSize({ width: w, height: h }); }
}

/// Static weight/flow curve for a saved brew — same axes/series language
/// as the live chart, but a plain one-shot plot for the bean library.
export function brewPlot(el, t, w, f) {
    const c = theme();
    return new uPlot({
        width: el.clientWidth,
        height: 190,
        padding: [8, 8, 0, 0],
        font: c.font,
        scales: {
            x: { time: false },
            w: { range: { min: { soft: 0, mode: 1, pad: 0.05 },
                          max: { soft: 50, mode: 1, pad: 0.08 } } },
            f: { range: [-10, 40] },
        },
        series: [
            {},
            { label: 'g', scale: 'w', stroke: c.ink, width: 2,
              points: { show: false } },
            { label: 'g/s', scale: 'f', stroke: c.amber, width: 1.5,
              points: { show: false } },
        ],
        axes: [
            { stroke: c.dim, grid: { stroke: c.grid }, font: c.font,
              values: (u, s) => s.map(fmtTime) },
            { scale: 'w', stroke: c.ink, grid: { stroke: c.grid }, font: c.font,
              size: 50 },
            { scale: 'f', side: 1, stroke: c.amber, grid: { show: false },
              font: c.font, size: 42 },
        ],
        legend: { show: false },
        cursor: { show: true, drag: { x: false, y: false } },
    }, [t, w, f], el);
}
