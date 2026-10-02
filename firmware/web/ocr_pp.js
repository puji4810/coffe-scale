// OCR post/pre-process helpers — pure functions on RGBA byte arrays and
// float tensors. No DOM, no onnxruntime: unit-testable in node.
//
// Pipeline (PaddleOCR / RapidOCR conventions):
//   detInput()    frame → normalized CHW float32 for the DB detector
//   findTextBoxes prob map → expanded quads in DET-image coords
//   warpQuad()    quad → rect patch via inverse homography, bilinear
//   recInput()    patch → normalized CHW float32 for the recognizer
//   ctcDecode()   logits → {text, score}

const DET_MEAN = [0.485, 0.456, 0.406], DET_STD = [0.229, 0.224, 0.225];

// Bilinear-resize RGBA (Uint8Array/ClampedArray) to dst dims.
export function resizeRGBA(src, sw, sh, dstW, dstH) {
    const out = new Uint8Array(dstW * dstH * 4);
    const xr = sw / dstW, yr = sh / dstH;
    for (let y = 0; y < dstH; y++) {
        const sy = (y + 0.5) * yr - 0.5;
        const y0 = Math.max(0, Math.floor(sy)), y1 = Math.min(sh - 1, y0 + 1);
        const fy = sy - y0;
        const row = y * dstW * 4, r0 = y0 * sw * 4, r1 = y1 * sw * 4;
        for (let x = 0; x < dstW; x++) {
            const sx = (x + 0.5) * xr - 0.5;
            const x0 = Math.max(0, Math.floor(sx)), x1 = Math.min(sw - 1, x0 + 1);
            const fx = sx - x0, i = row + x * 4;
            const i00 = r0 + x0 * 4, i01 = r0 + x1 * 4, i10 = r1 + x0 * 4, i11 = r1 + x1 * 4;
            for (let c = 0; c < 4; c++) {
                const t = src[i00 + c] * (1 - fx) + src[i01 + c] * fx;
                const b = src[i10 + c] * (1 - fx) + src[i11 + c] * fx;
                out[i + c] = t * (1 - fy) + b * fy;
            }
        }
    }
    return out;
}

// RGBA frame → NCHW float32 for the DB detector. PP-OCR det takes BGR order
// (it feeds cv2's decode straight into imagenet normalization), so channel 0
// is B here — off-by-swap hurts small-text recall on real photos.
export function detInput(rgba, w, h, limit = 960) {
    const r = Math.min(1, limit / Math.max(w, h));
    let W = Math.max(32, Math.round(w * r / 32) * 32);
    let H = Math.max(32, Math.round(h * r / 32) * 32);
    const img = resizeRGBA(rgba, w, h, W, H);
    const n = W * H, data = new Float32Array(3 * n);
    for (let i = 0; i < n; i++) {
        for (let c = 0; c < 3; c++) {
            data[c * n + i] = (img[i * 4 + (2 - c)] / 255 - DET_MEAN[c]) / DET_STD[c];
        }
    }
    return { data, W, H, sx: W / w, sy: H / h };
}

// --- detection postprocess -------------------------------------------------

// Connected components of a binary mask (8-connectivity), returns arrays of
// flat pixel indices.
function components(mask, w, h) {
    const label = new Int32Array(w * h);
    const parent = [0];
    let next = 0;
    const find = a => { while (parent[a] !== a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    const union = (a, b) => { const ra = find(a), rb = find(b); if (ra !== rb) parent[rb] = ra; };
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const i = y * w + x;
            if (!mask[i]) continue;
            const up = y > 0, l = x > 0, rr = x + 1 < w;
            const nl = l ? label[i - 1] : 0;
            const nu = up ? label[i - w] : 0;
            const nul = up && l ? label[i - w - 1] : 0;
            const nur = up && rr ? label[i - w + 1] : 0;
            const nb = [nl, nu, nul, nur].filter(v => v);
            if (!nb.length) { parent.push(++next); label[i] = next; continue; }
            label[i] = nb[0];
            for (const v of nb) union(nb[0], v);
        }
    }
    const map = new Map();
    for (let i = 0; i < w * h; i++) {
        if (!mask[i]) continue;
        const r = find(label[i]);
        let a = map.get(r);
        if (!a) map.set(r, a = []);
        a.push(i);
    }
    return [...map.values()];
}

// Convex hull of pixel points (Andrew monotone chain). pts: flat indices.
function hull(pts, w) {
    const p = pts.map(i => [i % w, (i / w) | 0]);
    p.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
    const lo = [], hi = [];
    for (const q of p) {
        while (lo.length >= 2 && cross(lo[lo.length - 2], lo[lo.length - 1], q) <= 0) lo.pop();
        lo.push(q);
    }
    for (let i = p.length - 1; i >= 0; i--) {
        const q = p[i];
        while (hi.length >= 2 && cross(hi[hi.length - 2], hi[hi.length - 1], q) <= 0) hi.pop();
        hi.push(q);
    }
    return lo.slice(0, -1).concat(hi.slice(0, -1));
}

// Minimum-area rotated rectangle over a hull: try each hull edge angle.
function minAreaRect(hp) {
    let best = null;
    for (let i = 0; i < hp.length; i++) {
        const [x1, y1] = hp[i], [x2, y2] = hp[(i + 1) % hp.length];
        const ang = Math.atan2(y2 - y1, x2 - x1);
        const c = Math.cos(-ang), s = Math.sin(-ang);
        let mnx = 1e18, mxx = -1e18, mny = 1e18, mxy = -1e18;
        for (const [x, y] of hp) {
            const rx = x * c - y * s, ry = x * s + y * c;
            if (rx < mnx) mnx = rx; if (rx > mxx) mxx = rx;
            if (ry < mny) mny = ry; if (ry > mxy) mxy = ry;
        }
        const w = mxx - mnx, h = mxy - mny, a = w * h;
        if (!best || a < best.a) {
            // center back in original coords
            const ccx = (mnx + mxx) / 2, ccy = (mny + mxy) / 2;
            const ca = Math.cos(ang), sa = Math.sin(ang);
            best = { a, w, h, ang,
                     cx: ccx * ca - ccy * sa, cy: ccx * sa + ccy * ca };
        }
    }
    return best;
}

// Order quad corners tl,tr,br,bl from a rotated rect.
function rectQuad(r, wExp, hExp) {
    const ca = Math.cos(r.ang), sa = Math.sin(r.ang);
    const hw = wExp / 2, hh = hExp / 2;
    return [[-hw, -hh], [hw, -hh], [hw, hh], [-hw, hh]]
        .map(([x, y]) => [r.cx + x * ca - y * sa, r.cy + x * sa + y * ca]);
}

// DB postprocess: prob map → text quads [{pts:[[x,y]×4], score}] in the
// DET input's coordinate space.
export function findTextBoxes(prob, W, H,
                              { thr = 0.3, boxThr = 0.5, unclip = 1.6,
                                minSide = 3, minPts = 6, maxBoxes = 200 } = {}) {
    const mask = new Uint8Array(W * H);
    for (let i = 0; i < W * H; i++) mask[i] = prob[i] > thr ? 1 : 0;
    const boxes = [];
    for (const comp of components(mask, W, H)) {
        if (comp.length < minPts) continue;
        let score = 0;
        for (const i of comp) score += prob[i];
        score /= comp.length;
        if (score < boxThr) continue;
        const r = minAreaRect(hull(comp, W));
        if (!r) continue;
        const short = Math.min(r.w, r.h);
        if (short < minSide) continue;
        // unclip: expand each side by d = unclip*area/perimeter
        const d = unclip * r.a / (2 * (r.w + r.h));
        const pts = rectQuad(r, r.w + 2 * d, r.h + 2 * d)
            .map(([x, y]) => [
                Math.min(W - 1, Math.max(0, x)),
                Math.min(H - 1, Math.max(0, y))]);
        boxes.push({ pts, score });
    }
    boxes.sort((a, b) => boxCy(a) - boxCy(b));
    return boxes.slice(0, maxBoxes);
}

const boxCy = b => (b.pts[0][1] + b.pts[2][1]) / 2;

export function scaleQuad(pts, kx, ky = kx) {
    return pts.map(([x, y]) => [x * kx, y * ky]);
}

// --- homography warp (quad → rect patch) ------------------------------------

// Solve H mapping the unit-ish dst rect (0,0)-(w,0)-(w,h)-(0,h) onto the src
// quad, then sample bilinearly. Returns RGBA patch.
export function warpQuad(rgba, W, H, quad, outW, outH) {
    const dst = [[0, 0], [outW, 0], [outW, outH], [0, outH]];
    const M = homography(dst, quad);            // dst → src
    const out = new Uint8Array(outW * outH * 4);
    for (let y = 0; y < outH; y++) {
        for (let x = 0; x < outW; x++) {
            const d = M[6] * x + M[7] * y + M[8];
            let sx = (M[0] * x + M[1] * y + M[2]) / d;
            let sy = (M[3] * x + M[4] * y + M[5]) / d;
            sx = Math.min(W - 1.001, Math.max(0, sx));
            sy = Math.min(H - 1.001, Math.max(0, sy));
            const x0 = sx | 0, y0 = sy | 0, fx = sx - x0, fy = sy - y0;
            const i00 = (y0 * W + x0) * 4, i01 = i00 + 4, i10 = i00 + W * 4, i11 = i10 + 4;
            const o = (y * outW + x) * 4;
            for (let c = 0; c < 4; c++) {
                const t = rgba[i00 + c] * (1 - fx) + rgba[i01 + c] * fx;
                const b = rgba[i10 + c] * (1 - fx) + rgba[i11 + c] * fx;
                out[o + c] = t * (1 - fy) + b * fy;
            }
        }
    }
    return out;
}

// 8-point homography via Gaussian elimination. src/dst: [[x,y]×4].
function homography(src, dst) {
    const A = [], B = [];
    for (let i = 0; i < 4; i++) {
        const [x, y] = src[i], [X, Y] = dst[i];
        A.push([x, y, 1, 0, 0, 0, -X * x, -X * y]); B.push(X);
        A.push([0, 0, 0, x, y, 1, -Y * x, -Y * y]); B.push(Y);
    }
    for (let c = 0; c < 8; c++) {
        let p = c;
        for (let r = c + 1; r < 8; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
        [A[c], A[p]] = [A[p], A[c]]; [B[c], B[p]] = [B[p], B[c]];
        for (let r = 0; r < 8; r++) {
            if (r === c) continue;
            const f = A[r][c] / A[c][c];
            for (let k = c; k < 8; k++) A[r][k] -= f * A[c][k];
            B[r] -= f * B[c];
        }
    }
    const h = B.map((b, i) => b / A[i][i]);
    return [...h, 1];
}

// --- recognition ------------------------------------------------------------

// Patch RGBA (w×h) → NCHW float32, height 48, width ∝ aspect (PP-OCR rec:
// (v/255-0.5)/0.5). Vertical strips get rotated to horizontal.
export function recInput(patch, w, h, maxW = 960) {
    if (h > w) patch = rot90(patch, w, h), [w, h] = [h, w];
    const W = Math.min(maxW, Math.max(16, Math.round(48 * w / h)));
    const img = resizeRGBA(patch, w, h, W, 48);
    const n = W * 48, data = new Float32Array(3 * n);
    for (let i = 0; i < n; i++) {
        data[i] = img[i * 4] / 127.5 - 1;
        data[n + i] = img[i * 4 + 1] / 127.5 - 1;
        data[2 * n + i] = img[i * 4 + 2] / 127.5 - 1;
    }
    return { data, W };
}

// Invert RGB channels of an RGBA patch — light-on-dark label text gets a
// second chance as dark-on-light (the recognizer's training distribution).
export function invertPatch(patch) {
    const out = patch.slice();
    for (let i = 0; i < out.length; i += 4) {
        out[i] = 255 - out[i];
        out[i + 1] = 255 - out[i + 1];
        out[i + 2] = 255 - out[i + 2];
    }
    return out;
}

function rot90(src, w, h) {
    const out = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++)
            for (let c = 0; c < 4; c++)
                out[(x * h + (h - 1 - y)) * 4 + c] = src[(y * w + x) * 4 + c];
    return out;
}

// CTC greedy decode. logits: T×C float32; keys: chars array (index 0 of the
// class space is the CTC blank; keys[i] sits at class i+1, final class = ' ').
// PP-OCR exports end in a softmax, so values may already be probabilities —
// detect that once and use the raw max as confidence.
export function ctcDecode(logits, T, C, keys) {
    let isProb = true;
    for (let i = 0; i < Math.min(C, logits.length); i++)
        if (logits[i] < 0 || logits[i] > 1) { isProb = false; break; }
    let text = '', conf = 0, cnt = 0, prev = -1;
    for (let t = 0; t < T; t++) {
        let bi = 0, bp = -1e30;
        const row = t * C;
        for (let c = 0; c < C; c++) if (logits[row + c] > bp) { bp = logits[row + c]; bi = c; }
        if (bi !== 0 && bi !== prev) {
            const ch = bi <= keys.length ? keys[bi - 1] : ' ';
            if (ch) {
                text += ch;
                conf += isProb ? bp : Math.exp(bp - logsumexp(logits, row, C));
                cnt++;
            }
        }
        prev = bi;
    }
    return { text, score: cnt ? conf / cnt : 0 };
}

function logsumexp(a, off, n) {
    let m = -1e30;
    for (let i = 0; i < n; i++) if (a[off + i] > m) m = a[off + i];
    let s = 0;
    for (let i = 0; i < n; i++) s += Math.exp(a[off + i] - m);
    return m + Math.log(s);
}
