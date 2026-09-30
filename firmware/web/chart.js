// Rolling weight/flow chart on uPlot (vendored, global `uPlot`).
// X axis is seconds since first pushed frame; left axis = weight, right = flow.

const AMBER = '#e8a33d', FG = '#f5f5f5', GRID = '#2c2c2c', DIM = '#8a8a8a';

function fmtTime(v) {
    const s = Math.round(v);
    return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

export class ScaleChart {
    constructor(el, windowS = 600) {
        this.windowS = windowS;
        this.t0 = null;                 // ms of first frame
        this.lastT = -Infinity;
        this.x = []; this.w = []; this.f = [];

        this.u = new uPlot({
            width: el.clientWidth,
            height: el.clientHeight,
            padding: [8, 8, 0, 0],
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
                { label: 'g', scale: 'w', stroke: FG, width: 2,
                  points: { show: false } },
                { label: 'g/s', scale: 'f', stroke: AMBER, width: 1.5,
                  points: { show: false } },
            ],
            axes: [
                { stroke: DIM, grid: { stroke: GRID },
                  values: (u, s) => s.map(fmtTime) },
                // axis label color matches its series: white = weight (left),
                // amber = flow (right)
                { scale: 'w', stroke: FG, grid: { stroke: GRID },
                  size: 56 },
                { scale: 'f', side: 1, stroke: AMBER, grid: { show: false },
                  size: 48 },
            ],
            legend: { show: true, live: true },
            cursor: { show: true, drag: { x: false, y: false } },
            hooks: {
                setCursor: [u => this.updateTip(u)],
            },
        }, [[], [], []], el);

        this.tip = document.createElement('div');
        this.tip.className = 'chart-tip';
        this.tip.style.display = 'none';
        this.u.over.appendChild(this.tip);

        this.visible = true;            // hidden = collect, don't redraw
    }

    /// Show/hide. While hidden push() still records the rolling window but
    /// skips setData — no redraw cost. Re-showing flushes the backlog in
    /// one pass so the full history reappears.
    setVisible(v) {
        this.visible = v;
        if (v) {
            const el = this.u.root.parentElement;
            this.u.setSize({ width: el.clientWidth,
                             height: el.clientHeight });
            this.u.setData([this.x, this.w, this.f]);
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
