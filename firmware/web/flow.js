// Disturbance rejection + smooth flow for weight/flow series.
// Render-time transform only — stored data stays raw.
//   1. flag samples whose ±DISTURB_HALF_S local line-fit residual
//      exceeds DISTURB_RESID_G (impacts: stirring, setting a cup down)
//   2. dilate the flags by DILATE_S
//   3. bridge each bad run with a straight line between neighbours
//   4. flow = centered least-squares slope of the cleaned weight over
//      FLOW_WIN_S, clipped, with a deadband
// All windows use two pointers — O(n·window_samples), no O(n²) scans.

export const DISTURB_HALF_S = 0.3;
export const DISTURB_RESID_G = 4;
export const DILATE_S = 0.4;
export const FLOW_WIN_S = 2.0;
export const FLOW_CLIP = 30;
export const FLOW_DEADBAND = 0.5;

/// Least-squares slope of (t[k], w[k]) for j0 <= k <= j1.
/// Returns 0 when the window has no t spread.
function slope(t, w, j0, j1) {
    const n = j1 - j0 + 1;
    let mx = 0, my = 0;
    for (let k = j0; k <= j1; k++) { mx += t[k]; my += w[k]; }
    mx /= n; my /= n;
    let sxx = 0, sxy = 0;
    for (let k = j0; k <= j1; k++) {
        const dx = t[k] - mx;
        sxx += dx * dx;
        sxy += dx * (w[k] - my);
    }
    return sxx > 0 ? sxy / sxx : 0;
}

/// t ascending seconds, w grams — returns cleaned weight, derived flow
/// and the dilated bad flag per sample.
export function cleanSeries(t, w) {
    const n = t.length;
    const bad0 = new Array(n).fill(false);
    const bad = new Array(n).fill(false);
    const wc = new Array(n), fc = new Array(n).fill(0);

    // 1. roughness: |t[k]-t[i]| <= DISTURB_HALF_S line fit, max residual
    {
        let j0 = 0, j1 = -1;
        for (let i = 0; i < n; i++) {
            while (j1 + 1 < n && t[j1 + 1] - t[i] <= DISTURB_HALF_S) j1++;
            while (j0 < n && t[i] - t[j0] > DISTURB_HALF_S) j0++;
            const m = j1 - j0 + 1;
            let mx = 0, my = 0;
            for (let k = j0; k <= j1; k++) { mx += t[k]; my += w[k]; }
            mx /= m; my /= m;
            let sxx = 0, sxy = 0;
            for (let k = j0; k <= j1; k++) {
                const dx = t[k] - mx;
                sxx += dx * dx;
                sxy += dx * (w[k] - my);
            }
            const sl = sxx > 0 ? sxy / sxx : 0;
            let maxr = 0;
            for (let k = j0; k <= j1; k++) {
                const r = Math.abs(w[k] - (my + sl * (t[k] - mx)));
                if (r > maxr) maxr = r;
            }
            bad0[i] = maxr > DISTURB_RESID_G;
        }
    }

    // 2. dilate: bad[k] iff some bad0[j] has |t[j]-t[k]| <= DILATE_S
    {
        let j0 = 0, j1 = -1, cnt = 0;
        for (let i = 0; i < n; i++) {
            while (j1 + 1 < n && t[j1 + 1] - t[i] <= DILATE_S) {
                j1++;
                if (bad0[j1]) cnt++;
            }
            while (j0 < n && t[i] - t[j0] > DILATE_S) {
                if (bad0[j0]) cnt--;
                j0++;
            }
            bad[i] = cnt > 0;
        }
    }

    // 3. bridge maximal bad runs linearly between good neighbours
    for (let i = 0; i < n; i++) wc[i] = w[i];
    {
        let i = 0;
        while (i < n) {
            if (!bad[i]) { i++; continue; }
            let j = i;
            while (j < n && bad[j]) j++;
            const a = i - 1, b = j < n ? j : -1;
            for (let k = i; k < j; k++) {
                wc[k] = a < 0 && b < 0 ? w[k]
                    : a < 0 ? w[b]
                    : b < 0 ? w[a]
                    : w[a] + (w[b] - w[a]) * (t[k] - t[a]) / (t[b] - t[a]);
            }
            i = j;
        }
    }

    // 4. centered regression slope on cleaned weight
    {
        let j0 = 0, j1 = -1;
        const half = FLOW_WIN_S / 2;
        for (let i = 0; i < n; i++) {
            while (j1 + 1 < n && t[j1 + 1] - t[i] <= half) j1++;
            while (j0 < n && t[i] - t[j0] > half) j0++;
            let s = slope(t, wc, j0, j1);
            s = Math.max(-FLOW_CLIP, Math.min(FLOW_CLIP, s));
            fc[i] = Math.abs(s) < FLOW_DEADBAND ? 0 : s;
        }
    }

    return { w: wc, f: fc, bad };
}
