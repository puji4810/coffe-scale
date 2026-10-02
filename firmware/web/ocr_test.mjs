// Tests for the label-OCR pipeline — run: node ocr_test.mjs
// Pure postprocess/parse checks always run. The end-to-end block additionally
// needs `onnxruntime-node` resolvable (npm i onnxruntime-node in a scratch
// dir, symlinked into web/node_modules) plus vendored models — CI skips it.

import * as PP from './ocr_pp.js';
import { parseBeanLabel, fixLine } from './bean_parse.js';
import { inflateSync } from 'node:zlib';
import { readFileSync, existsSync } from 'node:fs';

let failures = 0;
function check(name, cond, extra = '') {
    console.log(`${cond ? 'ok  ' : 'FAIL'}  ${name} ${extra}`);
    if (!cond) failures++;
}

// --- PNG decode (8-bit non-interlaced, color types 2/6) ----------------------

function decodePNG(buf) {
    const u8 = buf; // Buffer: readUInt32BE + subarray both work
    let pos = 8, w = 0, h = 0, ctype = 0, idat = [];
    while (pos < u8.length) {
        const len = u8.readUInt32BE(pos); pos += 4;
        const type = u8.toString('latin1', pos, pos + 4); pos += 4;
        const chunk = u8.subarray(pos, pos + len); pos += len + 4;
        if (type === 'IHDR') {
            w = chunk.readUInt32BE(0); h = chunk.readUInt32BE(4);
            if (chunk[8] !== 8) throw new Error('png bitdepth ≠8 unsupported');
            if (chunk[12] !== 0) throw new Error('interlaced png unsupported');
            ctype = chunk[9];
        } else if (type === 'IDAT') idat.push(chunk);
        else if (type === 'IEND') break;
    }
    const bpp = ctype === 6 ? 4 : ctype === 2 ? 3 : 0;
    if (!bpp) throw new Error(`png color type ${ctype} unsupported`);
    const raw = inflateSync(Buffer.concat(idat));
    const stride = w * bpp;
    const img = new Uint8Array(h * stride);
    for (let y = 0; y < h; y++) {
        const f = raw[y * (stride + 1)];
        const row = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
        const out = img.subarray(y * stride, (y + 1) * stride);
        const up = y ? img.subarray((y - 1) * stride, y * stride) : null;
        for (let x = 0; x < stride; x++) {
            const a = x >= bpp ? out[x - bpp] : 0, b = up ? up[x] : 0;
            const c = x >= bpp && up ? up[x - bpp] : 0;
            let v = row[x];
            if (f === 1) v += a;
            else if (f === 2) v += b;
            else if (f === 3) v += (a + b) >> 1;
            else if (f === 4) {
                const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b),
                      pc = Math.abs(p - c);
                v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
            }
            out[x] = v;
        }
    }
    // → RGBA
    const rgba = new Uint8Array(w * h * 4);
    for (let i = 0; i < w * h; i++) {
        rgba[i * 4] = img[i * bpp]; rgba[i * 4 + 1] = img[i * bpp + 1];
        rgba[i * 4 + 2] = img[i * bpp + 2];
        rgba[i * 4 + 3] = bpp === 4 ? img[i * bpp + 3] : 255;
    }
    return { rgba, w, h };
}

// --- ocr_pp unit tests ---------------------------------------------------------

{
    // resizeRGBA: 4x4 constant → 8x8 constant
    const src = new Uint8Array(4 * 4 * 4).fill(200);
    const out = PP.resizeRGBA(src, 4, 4, 8, 8);
    check('resize const', out.length === 8 * 8 * 4 && out[0] === 200);
}

{
    // findTextBoxes: solid rectangle in a prob map
    const W = 96, H = 64;
    const prob = new Float32Array(W * H);
    for (let y = 20; y < 44; y++)
        for (let x = 30; x < 80; x++) prob[y * W + x] = 0.9;
    const boxes = PP.findTextBoxes(prob, W, H, { thr: 0.3, boxThr: 0.5 });
    check('one box found', boxes.length === 1, `(${boxes.length})`);
    if (boxes.length) {
        const q = boxes[0].pts;
        const xs = q.map(p => p[0]), ys = q.map(p => p[1]);
        const bw = Math.max(...xs) - Math.min(...xs);
        const bh = Math.max(...ys) - Math.min(...ys);
        check('box covers rect', bw >= 50 && bw <= 82 && bh >= 24 && bh <= 55,
              `(${bw}x${bh})`);
    }
}

{
    // ctcDecode: emit 咖 啡 — logits spike at the right classes
    const keys = ['咖', '啡', 'x'];
    const C = 1 + keys.length + 1, T = 6;
    const L = new Float32Array(T * C).fill(-10);
    const emit = [0, 1, 1, 0, 2, 0]; // blank, 咖, dup, blank, 啡, blank
    for (let t = 0; t < T; t++) L[t * C + emit[t]] = 8;
    const r = PP.ctcDecode(L, T, C, keys);
    check('ctc decode', r.text === '咖啡', `(${r.text})`);
    check('ctc score', r.score > 0.9, `(${r.score})`);
}

// Equivalent min-area rectangles may choose the opposite hull edge due to
// floating-point ties. The crop must still read left-to-right, not upside-down.
for (const degrees of [-35, -25, -15, -5, 0, 5, 15, 25, 35]) {
    const W = 160, H = 96, a = degrees * Math.PI / 180;
    const prob = new Float32Array(W * H);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
        const rx = (x - 80) * Math.cos(a) + (y - 48) * Math.sin(a);
        const ry = -(x - 80) * Math.sin(a) + (y - 48) * Math.cos(a);
        if (Math.abs(rx) < 45 && Math.abs(ry) < 8) prob[y * W + x] = .9;
    }
    const [b] = PP.findTextBoxes(prob, W, H);
    check(`crop orientation ${degrees}°`, b && b.pts[1][0] > b.pts[0][0] &&
          b.pts[2][0] > b.pts[3][0]);
}

{
    // warpQuad: axis-aligned quad ≈ crop
    const w = 20, h = 20;
    const rgba = new Uint8Array(w * h * 4);
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++)
            rgba[(y * w + x) * 4] = x * 10, rgba[(y * w + x) * 4 + 1] = y * 10;
    const out = PP.warpQuad(rgba, w, h,
        [[4, 4], [16, 4], [16, 12], [4, 12]], 12, 8);
    const mid = ((6 * 12) + 6) * 4; // dst(6,6) → src(10,10) → (100,100)
    check('warp samples src', Math.abs(out[mid] - 100) < 25 &&
          Math.abs(out[mid + 1] - 100) < 25,
          `(${out[mid]},${out[mid + 1]})`);
}

{
    const p = new Uint8Array([10, 20, 30, 255, 250, 240, 230, 255]);
    const inv = PP.invertPatch(p);
    check('invertPatch rgb', inv[0] === 245 && inv[1] === 235 && inv[2] === 225);
    check('invertPatch keeps alpha + input', inv[3] === 255 && p[0] === 10);
}

// --- bean_parse unit tests -----------------------------------------------------

{
    const lines = [
        { text: '小满咖啡', score: .95, quad: [[0,0],[80,0],[80,20],[0,20]] },
        { text: '埃塞俄比亚 耶加雪菲', score: .9, quad: [[0,30],[200,30],[200,60],[0,60]] },
        { text: '水洗 G1 瑰夏', score: .9, quad: [[0,65],[160,65],[160,90],[0,90]] },
        { text: '净含量 100g', score: .8, quad: [[0,95],[100,95],[100,110],[0,110]] },
        { text: '烘焙日期 2026-09-30', score: .8, quad: [[0,115],[140,115],[140,130],[0,130]] },
    ];
    const r = parseBeanLabel(lines, {
        brands: ['小满咖啡', 'Torch'],
        beans: [{ id: 7, name: '肯尼亚 AA 水洗', brand: 'Torch' }],
    });
    check('brand matched', r.fields.brand === '小满咖啡', `(${r.fields.brand})`);
    check('process', r.fields.process === '水洗', `(${r.fields.process})`);
    check('variety', r.fields.variety === '瑰夏', `(${r.fields.variety})`);
    check('estate/origin', r.fields.estate === '耶加雪菲', `(${r.fields.estate})`);
    check('name nonempty', !!r.fields.name, `(${r.fields.name})`);
    check('note has weight', /净含量\s*100/.test(r.fields.note), `(${r.fields.note})`);
    check('note has roast', /烘焙/.test(r.fields.note), `(${r.fields.note})`);

    // notes must not be silently truncated — a long flavor line survives whole
    const longFlavor = '风味 ' + '草莓玫瑰茉莉柑橘苹果梨桃杏葡萄莓果'.repeat(20);
    const rl = parseBeanLabel([
        { text: longFlavor, score: .9, quad: [[0,0],[300,0],[300,40],[0,40]] },
    ], { beans: [] });
    check('long note not truncated', rl.fields.note.length > 200,
          `(${rl.fields.note.length} chars)`);

    const r2 = parseBeanLabel(
        [{ text: 'Torch 肯尼亚 AA 水洗豆', score: .9,
           quad: [[0,0],[200,0],[200,50],[0,50]] }],
        { brands: [], beans: [{ id: 7, name: '肯尼亚 AA 水洗', brand: 'Torch' }] });
    check('existing bean match', r2.matches.length > 0 && r2.matches[0].bean.id === 7,
          `(${r2.matches.map(m => m.bean.id)})`);

    // corroborating evidence: same-brand beans, right one wins via name+fields
    const lib = [
        { id: 1, name: '耶加雪菲 水洗', brand: 'Torch', process: '水洗', variety: '原生种' },
        { id: 2, name: '肯尼亚 AA 水洗', brand: 'Torch', process: '水洗', variety: 'SL28' },
        { id: 3, name: '曼特宁 湿刨', brand: 'Other' },
    ];
    const r3 = parseBeanLabel([
        { text: 'Torch Coffee', score: .9, quad: [[0,0],[200,0],[200,40],[0,40]] },
        { text: '耶加雪菲水洗 G1', score: .9, quad: [[0,50],[220,50],[220,90],[0,90]] },
    ], { brands: [], beans: lib });
    check('brand+fields disambiguate',
          r3.matches[0]?.bean.id === 1 && r3.matches[0].brandHit,
          `(${r3.matches.map(m => `${m.bean.id}:${m.s.toFixed(2)}`)})`);
    check('same-brand wrong bean not promoted',
          !r3.matches.some(m => m.bean.id === 2),
          `(${r3.matches.map(m => m.bean.id)})`);

    // brand alone without any name overlap must not fabricate a match
    const r4 = parseBeanLabel([
        { text: 'Torch Coffee', score: .9, quad: [[0,0],[200,0],[200,40],[0,40]] },
        { text: '水洗', score: .9, quad: [[0,50],[100,50],[100,80],[0,80]] },
    ], { brands: [], beans: [{ id: 5, name: '日晒瑰夏', brand: 'Torch', process: '水洗' }] });
    check('no name overlap → no match', !r4.matches.length,
          `(${r4.matches.map(m => m.bean.id)})`);
}

// --- lexicon correction -----------------------------------------------------

{
    check('fixLine CJK misread', fixLine('耶咖雪菲 水洗豆') === '耶加雪菲 水洗豆',
          `(${fixLine('耶咖雪菲 水洗豆')})`);
    check('fixLine latin misread', fixLine('YUNNAN GESCHA') === 'YUNNAN GESHA',
          `(${fixLine('YUNNAN GESCHA')})`);
    check('fixLine multiword term', fixLine('GESCHA VILLAGE') === 'GEISHA VILLAGE',
          `(${fixLine('GESCHA VILLAGE')})`);
    check('fixLine leaves plain text', fixLine('今日发货 风味蓝莓') === '今日发货 风味蓝莓',
          `(${fixLine('今日发货 风味蓝莓')})`);
    check('fixLine keeps exact terms', fixLine('巴拿马 翡翠庄园') === '巴拿马 翡翠庄园');

    // corrected lines flow into parsing + matching
    const r = parseBeanLabel([
        { text: '耶咖雪菲水洗', score: .9, quad: [[0,0],[200,0],[200,40],[0,40]] },
    ], { beans: [{ id: 9, name: '耶加雪菲 水洗' }] });
    check('corrected line parses', r.fields.process === '水洗',
          `(${r.fields.process} / ${r.fields.name})`);
    check('corrected line matches bean', r.matches[0]?.bean.id === 9,
          `(${r.matches.map(m => m.bean.id)})`);

    // bean-library terms join the lexicon
    check('bean lib term snaps', fixLine('哥伦比亚惠兰庄园', ['哥伦比亚慧兰庄园']) ===
          '哥伦比亚慧兰庄园',
          `(${fixLine('哥伦比亚惠兰庄园', ['哥伦比亚慧兰庄园'])})`);
    check('bean brand snaps', fixLine('MOKKE coffee', ['Mokka']) === 'Mokka coffee',
          `(${fixLine('MOKKE coffee', ['Mokka'])})`);
    const rb = parseBeanLabel([
        { text: '哥伦比亚惠兰庄园', score: .9, quad: [[0,0],[200,0],[200,40],[0,40]] },
    ], { beans: [{ id: 4, name: '哥伦比亚慧兰庄园', brand: 'Mokka' }] });
    check('lib-corrected bean matches', rb.matches[0]?.bean.id === 4,
          `(${rb.matches.map(m => `${m.bean.id}:${m.s.toFixed(2)}`)})`);
}

// --- optional end-to-end with real models ------------------------------------
// Needs: vendor/ocr/v5/* (fetch_ocr.sh), node_modules/onnxruntime-node, and a
// rendered label PNG at testdata/label.png (tools/make_test_label.py).

const FIXTURE = new URL('./testdata/label.png', import.meta.url).pathname;
let ortNode = null;
try { ortNode = (await import('onnxruntime-node')).default ?? await import('onnxruntime-node'); }
catch { /* not installed */ }

if (ortNode && existsSync(FIXTURE) && existsSync(
    new URL('./vendor/ocr/v5/det.onnx', import.meta.url).pathname)) {
    const { rgba, w, h } = decodePNG(readFileSync(FIXTURE));
    console.log(`e2e: ${w}x${h} fixture`);
    const keys = readFileSync(
        new URL('./vendor/ocr/v5/keys.txt', import.meta.url), 'utf8')
        .split('\n').map(s => s.replace(/\r$/, ''));
    if (keys.at(-1) === '') keys.pop();

    const det = await ortNode.InferenceSession.create(
        new URL('./vendor/ocr/v5/det.onnx', import.meta.url).pathname, { logSeverityLevel: 3 });
    const rec = await ortNode.InferenceSession.create(
        new URL('./vendor/ocr/v5/rec.onnx', import.meta.url).pathname, { logSeverityLevel: 3 });

    const inp = PP.detInput(rgba, w, h);
    const t = new ortNode.Tensor('float32', inp.data, [1, 3, inp.H, inp.W]);
    const detOut = await det.run({ [det.inputNames[0]]: t });
    const prob = detOut[det.outputNames[0]];
    const boxes = PP.findTextBoxes(prob.data, inp.W, inp.H);
    check('e2e boxes ≥3', boxes.length >= 3, `(${boxes.length})`);

    const lines = [];
    for (const b of boxes) {
        const pts = PP.scaleQuad(b.pts, 1 / inp.sx, 1 / inp.sy);
        const wt = Math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]);
        const ht = Math.hypot(pts[3][0] - pts[0][0], pts[3][1] - pts[0][1]);
        const patch = PP.warpQuad(rgba, w, h, pts, Math.max(8, wt | 0), Math.max(8, ht | 0));
        const ri = PP.recInput(patch, Math.max(8, wt | 0), Math.max(8, ht | 0));
        const rt = new ortNode.Tensor('float32', ri.data, [1, 3, 48, ri.W]);
        const ro = await rec.run({ [rec.inputNames[0]]: rt });
        const logits = ro[rec.outputNames[0]];
        const r = PP.ctcDecode(logits.data, logits.dims[1], logits.dims[2], keys);
        if (r.text.trim()) lines.push({ text: r.text, score: r.score, quad: pts });
    }
    console.log('e2e lines:', lines.map(l => `${l.text}(${l.score.toFixed(2)})`).join(' | '));
    check('e2e found text', lines.length >= 3);
    const joined = lines.map(l => l.text).join(' ');
    check('e2e reads CJK', /咖啡|水|洗|耶|豆|瑰|夏/.test(joined), `(${joined})`);

    const parsed = parseBeanLabel(lines, { brands: ['测试烘焙'], beans: [] });
    console.log('e2e fields:', JSON.stringify(parsed.fields));
    check('e2e process', parsed.fields.process === '水洗',
          `(${parsed.fields.process})`);
} else {
    console.log('skip e2e — needs vendor/ocr/v5/*, testdata/label.png, onnxruntime-node');
}

if (failures) { console.error(`${failures} FAILED`); process.exit(1); }
await import('./scan_test.mjs');
await import('./ocr_worker_test.mjs');
console.log('all checks passed');
