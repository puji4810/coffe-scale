// Bean-label parsing — turn OCR text lines into bean card fields.
// Pure functions; unit-tested in ocr_test.mjs.
//
//   parseBeanLabel(lines, opts) → { fields, matches, used }
//     lines:  [{text, score, quad}]   (quad unused here except height)
//     opts:   {brands:[], beans:[{id,name,brand,...}]}
//     fields: {name, brand, process, variety, estate, note}
//     matches: existing beans that likely ARE this label
//     used:   per-line annotations for the result UI

// keyword tables — longer/more specific entries first so '厌氧日晒' wins
// over '日晒'. Values are the canonical strings used by bean cards.
const PROCESS = [
    [/厌氧日晒|anaerobic\s*natural/i, '厌氧日晒'],
    [/水洗厌氧|厌氧水洗|anaerobic\s*washed/i, '水洗厌氧'],
    [/双重厌氧|double\s*anaerobic/i, '双重厌氧'],
    [/酒桶|barrel|ruff?|rum\s*barrel/i, '酒桶发酵'],
    [/葡萄干|raisin/i, '葡萄干处理'],
    [/蜜处理|honey/i, '蜜处理'],
    [/湿刨|半水洗|wet[-\s]?hull|semi[-\s]?washed/i, '湿刨'],
    [/日晒|natural|dry[-\s]?process/i, '日晒'],
    [/水洗|washed|wet[-\s]?process|fully[-\s]?washed/i, '水洗'],
    [/厌氧|anaerobic/i, '厌氧'],
];
const VARIETY = [
    [/瑰夏|geisha|gesha/i, '瑰夏'],
    [/帕卡马拉|pacamara/i, '帕卡马拉'],
    [/粉波旁|pink\s*bourbon/i, '粉波旁'],
    [/黄波旁|yellow\s*bourbon/i, '黄波旁'],
    [/红波旁|red\s*bourbon/i, '红波旁'],
    [/波旁|bourbon/i, '波旁'],
    [/卡杜艾|卡图艾|catuai/i, '卡杜艾'],
    [/卡杜拉|caturra/i, '卡杜拉'],
    [/卡蒂姆|catimor/i, '卡蒂姆'],
    [/卡斯蒂略|castillo/i, '卡斯蒂略'],
    [/铁皮卡|铁比卡|typica/i, '铁皮卡'],
    [/原生种|埃塞原生种|heirloom/i, '原生种'],
    [/帕卡斯|pacas/i, '帕卡斯'],
    [/薇拉萨奇|villa\s*sarchi/i, '薇拉萨奇'],
    [/新世界|mundo\s*novo/i, '新世界'],
    [/马拉戈吉佩|maragogipe|象豆/i, '马拉戈吉佩'],
    [/sl\s*-?\s*28/i, 'SL28'], [/sl\s*-?\s*34/i, 'SL34'],
    [/蓝山|blue\s*mountain/i, '蓝山'],
    [/尤金|eugenioides|母种/i, '尤金尼奥德斯'],
];
const ORIGIN = [
    [/耶加雪菲|耶加|yirgache?ffe/i, '耶加雪菲'],
    [/西达摩|sidam[ao]/i, '西达摩'],
    [/古姬|guji/i, '古姬'],
    [/瑰夏村|gesha\s*village/i, '瑰夏村'],
    [/翡翠庄园|esmeralda/i, '翡翠庄园'],
    [/哈特曼|hartmann/i, '哈特曼庄园'],
    [/黛博拉|deborah/i, '黛博拉庄园'],
    [/艾丽达|elida/i, '艾丽达庄园'],
    [/曼特宁|mandhe?ling/i, '曼特宁'],
    [/埃塞俄比亚|埃塞|ethiopia/i, '埃塞俄比亚'],
    [/肯尼亚|kenya/i, '肯尼亚'],
    [/巴拿马|panama/i, '巴拿马'],
    [/哥伦比亚|colombia/i, '哥伦比亚'],
    [/哥斯达黎加|costa\s*rica/i, '哥斯达黎加'],
    [/危地马拉|guatemala/i, '危地马拉'],
    [/洪都拉斯|honduras/i, '洪都拉斯'],
    [/萨尔瓦多|el\s*salvador/i, '萨尔瓦多'],
    [/巴西|brazil/i, '巴西'],
    [/秘鲁|peru/i, '秘鲁'],
    [/卢旺达|rwanda/i, '卢旺达'],
    [/坦桑尼亚|tanzania/i, '坦桑尼亚'],
    [/牙买加|jamaica/i, '牙买加'],
    [/印度尼西亚|印尼|indonesia/i, '印度尼西亚'],
    [/云南|yunnan/i, '云南'],
    [/巴布亚|papua\s*new\s*guinea/i, '巴布亚新几内亚'],
    [/夏威夷|hawaii|kona/i, '夏威夷'],
    [/墨西哥|mexico/i, '墨西哥'],
];
const ESTATE_WORD = /庄园|村|estate|finca|hacienda|farm|处理站|station|coop|合作社|village/i;
const BRAND_WORD = /coffee|roast|咖啡|烘焙|工坊|工作室/i;

const NETWT = /(?:净含量|净重|net\s*(?:wt|weight)?|规格|含量)[:：]?\s*(\d+(?:\.\d+)?)\s*(kg|g|克|千克|lb|oz)/i;
const NETWT2 = /(\d{2,4})\s*(g\b|克|千克)/i;
const ROAST = /(?:烘焙日期|烘焙|roast(?:ed)?(?:\s*on)?|date)[:：]?\s*(\d{4}[./年-]\s?\d{1,2}[./月-]\s?\d{1,2}|\d{1,2}[./月-]\s?\d{1,2}[./日]?)/i;
const FLAVOR = /风味|notes?|flavo[u]?r|tasting|花香|柑橘|莓果/i;
const GRADE = /\b(g\s?1|g\s?2|g\s?3|aa|ab|shb|supremo|excelso)\b/i;

const norm = s => String(s || '').toLowerCase().replace(/[\s·・,，.。:：\-_/()（）[\]【】]+/g, '');

// dice bigram coefficient on normalized strings
function sim(a, b) {
    a = norm(a); b = norm(b);
    if (!a || !b) return 0;
    if (a.includes(b) || b.includes(a)) return 1;
    const bi = s => { const m = new Map(); for (let i = 0; i < s.length - 1; i++) m.set(s.slice(i, i + 2), (m.get(s.slice(i, i + 2)) || 0) + 1); return m; };
    const A = bi(a), B = bi(b);
    let hit = 0;
    for (const [k, v] of A) hit += Math.min(v, B.get(k) || 0);
    return (2 * hit) / (a.length - 1 + b.length - 1 || 1);
}

function boxH(l) {
    if (!l.quad) return 10;
    return Math.abs(l.quad[2][1] - l.quad[0][1]) || 10;
}

const pick = (lines, table) => {
    for (const l of lines)
        for (const [re, label] of table)
            if (re.test(l.text)) return { label, line: l };
    return null;
};

export function parseBeanLabel(lines, { brands = [], beans = [] } = {}) {
    const usable = lines.filter(l => l.text && l.text.trim().length >= 1 && l.score >= 0.4);
    const used = usable.map(l => ({ line: l, as: null }));
    const fields = { name: '', brand: '', process: '', variety: '', estate: '', note: '' };
    const notes = [];

    const tag = (l, as) => { const u = used.find(u => u.line === l); if (u && !u.as) u.as = as; };

    // brand — only confident matches against the known brand list, or a
    // short line carrying a roaster cue word
    const allBrands = [...new Set([...brands, ...beans.map(b => b.brand).filter(Boolean)])];
    for (const l of usable) {
        const hit = allBrands.find(b => sim(b, l.text) >= 0.6);
        if (hit) { fields.brand = hit; tag(l, 'brand'); break; }
    }
    if (!fields.brand) {
        const l = usable.find(l => BRAND_WORD.test(l.text) &&
            norm(l.text).length <= 14 &&
            !/^(烘焙|roast|净含量|net|生产|保质|风味|notes?)/i.test(l.text.trim()));
        if (l) { fields.brand = l.text.trim(); tag(l, 'brand'); }
    }

    const p = pick(usable, PROCESS); if (p) { fields.process = p.label; tag(p.line, 'process'); }
    const v = pick(usable, VARIETY); if (v) { fields.variety = v.label; tag(v.line, 'variety'); }
    const o = pick(usable, ORIGIN);

    // estate: a line explicitly marked as estate/farm/station, else origin name
    const el = usable.find(l => ESTATE_WORD.test(l.text));
    if (el) { fields.estate = el.text.trim(); tag(el, 'estate'); }
    else if (o) { fields.estate = o.label; tag(o.line, 'estate'); }

    // name: prefer lines that carry origin/variety/process words (that's what
    // a bean name usually is); tiebreak on visual size × confidence. Brand
    // and note-type lines are excluded. Always a raw OCR line, never invented.
    const isKw = l => PROCESS.some(([re]) => re.test(l.text)) ||
                      VARIETY.some(([re]) => re.test(l.text)) ||
                      ORIGIN.some(([re]) => re.test(l.text)) ||
                      ESTATE_WORD.test(l.text);
    const meta = new Set(usable.filter(l =>
        /^(净含量|net|烘焙|roast|保质期|生产日期|storage|保存)/i.test(l.text.trim())));
    const cands = usable.filter(l => {
        const u = used.find(u => u.line === l);
        return (!u.as || !['brand', 'note'].includes(u.as)) &&
               !meta.has(l) && norm(l.text).length >= 2;
    });
    cands.sort((a, b2) => {
        const fa = boxH(a) * a.score * (isKw(a) ? 1.6 : 1);
        const fb = boxH(b2) * b2.score * (isKw(b2) ? 1.6 : 1);
        return fb - fa;
    });
    if (cands.length) { fields.name = cands[0].text.trim().slice(0, 40); tag(cands[0], 'name'); }

    // extras → note
    for (const l of usable) {
        const wt = l.text.match(NETWT) || l.text.match(NETWT2);
        if (wt) { notes.push(`净含量 ${wt[1]}${wt[2]}`); tag(l, 'note'); continue; }
        const rd = l.text.match(ROAST);
        if (rd && /\d{4}|\d{1,2}[./月]/.test(rd[1])) { notes.push(`烘焙 ${rd[1].trim()}`); tag(l, 'note'); continue; }
        if (FLAVOR.test(l.text) && norm(l.text).length >= 3) { notes.push(l.text.trim()); tag(l, 'note'); continue; }
        if (GRADE.test(l.text)) { notes.push(`等级 ${l.text.match(GRADE)[0].toUpperCase()}`); tag(l, 'note'); }
    }
    fields.note = [...new Set(notes)].join('；').slice(0, 200);

    // match existing beans by name similarity against any line / full text
    const full = usable.map(l => l.text).join(' ');
    const matches = beans.map(b => {
        let s = sim(b.name, full);
        for (const l of usable) s = Math.max(s, sim(b.name, l.text));
        if (b.brand) s = Math.max(s, Math.min(1, sim(b.name, full) * (allBrands.includes(b.brand) ? 1.05 : 1)));
        return { bean: b, s };
    }).filter(m => m.s >= 0.55).sort((a, b) => b.s - a.s).slice(0, 3);

    return { fields, matches, used };
}
