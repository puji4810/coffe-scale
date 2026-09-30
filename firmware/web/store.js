// Brew/bean library on IndexedDB — quota is far larger than localStorage
// and survives cache clears, which matters because brew curves are the
// user's data, not app state.
//
//   beans { id, name, dose, created }
//   brews { id, beanId|null, date, durationS, dose, liquid, rating 0-5,
//           fav, t[], w[], f[] }   // curve: seconds / g / g/s @ frame rate

const DB_NAME = 'coffee-scale', DB_VER = 1;
let dbp = null;

function open() {
    if (dbp) return dbp;
    dbp = new Promise((res, rej) => {
        const r = indexedDB.open(DB_NAME, DB_VER);
        r.onupgradeneeded = () => {
            const db = r.result;
            db.createObjectStore('beans', { keyPath: 'id', autoIncrement: true });
            db.createObjectStore('brews', { keyPath: 'id', autoIncrement: true });
        };
        r.onsuccess = () => res(r.result);
        r.onerror = () => rej(r.error);
    });
    return dbp;
}

function wrap(r) {
    return new Promise((res, rej) => {
        r.onsuccess = () => res(r.result);
        r.onerror = () => rej(r.error);
    });
}

async function store(name, mode, fn) {
    const db = await open();
    return new Promise((res, rej) => {
        const tx = db.transaction(name, mode);
        const out = fn(tx.objectStore(name));
        tx.oncomplete = () => res(out);
        tx.onerror = tx.onabort = () => rej(tx.error);
    });
}

export const beans = {
    list: () => store('beans', 'readonly', s => wrap(s.getAll())),
    add: (name, dose) => store('beans', 'readwrite',
        s => wrap(s.add({ name, dose, created: Date.now() }))),
    update: b => store('beans', 'readwrite', s => wrap(s.put(b))),
    del: id => store('beans', 'readwrite', s => wrap(s.delete(id))),
};

export const brews = {
    list: () => store('brews', 'readonly', s => wrap(s.getAll())),
    add: b => store('brews', 'readwrite', s => wrap(s.add(b))),
    update: b => store('brews', 'readwrite', s => wrap(s.put(b))),
    del: id => store('brews', 'readwrite', s => wrap(s.delete(id))),
    clear: () => store('brews', 'readwrite', s => wrap(s.clear())),
};

export async function exportAll() {
    return {
        app: 'coffee-scale', version: 1, exported: new Date().toISOString(),
        beans: await beans.list(),
        brews: await brews.list(),
    };
}

// Import merges by id: rows whose ids collide are overwritten, new rows
// are added. Returns {beans, brews} counts written.
export async function importAll(data) {
    let nb = 0, nw = 0;
    for (const b of data.beans || []) { await beans.update(b); nb++; }
    for (const w of data.brews || []) { await brews.update(w); nw++; }
    return { beans: nb, brews: nw };
}
