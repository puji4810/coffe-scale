// Rolling weight/flow chart on uPlot (vendored, global `uPlot`).
// X axis is seconds since first pushed frame; left axis = weight, right = flow.
// Plotted values come from flow.js cleanSeries — raw weight is kept so
// the disturbance rejection can re-run on the tail every push.
// Colors come from the app's CSS custom properties so both themes work;
// call retheme() after a prefers-color-scheme change to rebuild.

import { cleanSeries } from './flow.js';

const TAIL_S = 15;  // only the newest tail is re-cleaned per push

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

/// Shade each maximal run of flagged (bridged) samples over the plot
/// height — users can see where the curve was interpolated.
function drawBad(u, bad) {
    if (!bad || !bad.length) return;
    const ctx = u.ctx;
    const top = u.bbox.top, height = u.bbox.height;
    ctx.save();
    ctx.fillStyle = theme().grid;
    ctx.globalAlpha = 0.35;
    const n = bad.length;
    let i = 0;
    while (i < n) {
        if (!bad[i]) { i++; continue; }
        let j = i;
        while (j < n && bad[j]) j++;
        const x0 = u.valToPos(u.data[0][i], 'x', true);
        const x1 = u.valToPos(u.data[0][j - 1], 'x', true);
        ctx.fillRect(x0, top, Math.max(1, x1 - x0), height);
        i = j;
    }
    ctx.restore();
}

function chartOpts(el, c, hooks) {
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
        hooks,
    };
}

export class ScaleChart {
    constructor(el, windowS = 600) {
        this.windowS = windowS;
        this.t0 = null;                 // ms of first frame
        this.lastT = -Infinity;
        this.el = el;
        // x = seconds, w = raw grams; wc/fc/bad = cleaned + flags
        this.x = []; this.w = [];
        this.wc = []; this.fc = []; this.bad = [];

        const opts = chartOpts(el, theme(), {
            setCursor: [u => this.updateTip(u)],
            draw: [u => drawBad(u, this.bad)],
        });
        this.u = new uPlot(opts, [[], [], []], el);

        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);

        this.visible = true;            // hidden = collect, don't redraw
    }

    /// Rebuild the uPlot instance with the current theme, keeping the
    /// rolling buffers (prefers-color-scheme changed).
    retheme() {
        this.u.destroy();
        const opts = chartOpts(this.el, theme(), {
            setCursor: [u => this.updateTip(u)],
            draw: [u => drawBad(u, this.bad)],
        });
        this.u = new uPlot(opts, [this.x, this.wc, this.fc], this.el);
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
            this.u.setData([this.x, this.wc, this.fc]);
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
            `${fmtTime(this.x[i])}  ${this.wc[i].toFixed(1)} g  ${this.fc[i].toFixed(1)} g/s`;
        const bw = u.bbox.width / devicePixelRatio;
        const left = Math.min(u.cursor.left + 12, bw - this.tip.offsetWidth - 4);
        this.tip.style.left = `${Math.max(0, left)}px`;
        this.tip.style.top = '6px';
    }

    /// Re-clean from the tail start backwards to the start of any bad
    /// run containing it (a flag's window can reach ~DILATE_S further),
    /// then splice the recomputed tail into wc/fc/bad.
    reclean() {
        const n = this.x.length;
        if (!n) return;
        let start = n - 1;
        const cut = this.x[n - 1] - TAIL_S;
        while (start > 0 && this.x[start] > cut) start--;
        while (start > 0 && this.bad[start]) start--;
        const res = cleanSeries(this.x.slice(start), this.w.slice(start));
        this.wc.length = start;
        this.fc.length = start;
        this.bad.length = start;
        for (let i = 0; i < res.w.length; i++) {
            this.wc.push(res.w[i]);
            this.fc.push(res.f[i]);
            this.bad.push(res.bad[i]);
        }
    }

    /// t in ms (any monotone clock — first call anchors t0). `flow` is
    /// accepted for call-site compat; plotted flow is derived from weight.
    push(t, grams, _flow) {
        // the caller's clock domain can switch (demo vt vs performance.now)
        if (this.t0 !== null && t < this.lastT) this.clear();
        this.lastT = t;
        if (this.t0 === null) this.t0 = t;
        const s = (t - this.t0) / 1000;
        const x = this.x;
        x.push(s); this.w.push(grams);
        // trim to the rolling window — derived arrays in lockstep
        const cut = s - this.windowS;
        let i = 0;
        while (i < x.length && x[i] < cut) i++;
        if (i) {
            x.splice(0, i); this.w.splice(0, i);
            this.wc.splice(0, i); this.fc.splice(0, i); this.bad.splice(0, i);
        }
        this.reclean();
        if (this.visible) this.u.setData([x, this.wc, this.fc]);
    }

    clear() {
        this.t0 = null;
        this.lastT = -Infinity;
        this.x.length = this.w.length = 0;
        this.wc.length = this.fc.length = this.bad.length = 0;
        this.u.setData([[], [], []]);
        this.tip.style.display = 'none';
    }

    resize(w, h) { this.u.setSize({ width: w, height: h }); }
}

/// Static weight/flow curve for a saved brew — stored weight is cleaned
/// on the fly and the flow trace is derived (stored f is ignored).
export function brewPlot(el, t, w, _f) {
    const c = theme();
    const res = cleanSeries(t, w);
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
        hooks: { draw: [u => drawBad(u, res.bad)] },
    }, [t, res.w, res.f], el);
}
