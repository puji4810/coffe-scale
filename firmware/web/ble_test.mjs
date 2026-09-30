// Node tests for ble.js ScaleLink — fake bluetooth/device/gatt objects,
// instant injectable delay. Run: node ble_test.mjs

import { ScaleLink } from './ble.js';

let failures = 0;
function check(name, cond, extra = '') {
    console.log(`${cond ? 'ok  ' : 'FAIL'}  ${name} ${extra}`);
    if (!cond) failures++;
}

const UUIDS = {
    service: 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c01',
    state: 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c02',
    command: 'c0ffee00-5ca1-4e5a-9b1e-7d2f3a6b8c03',
};

const tick = () => new Promise(r => setTimeout(r, 0));
const instant = () => Promise.resolve();

class FakeChr extends EventTarget {
    constructor() {
        super();
        this.notifying = false;
        this.writes = [];
        this.writeGates = null; // when set, writeValueWithResponse waits on it
    }
    async startNotifications() { this.notifying = true; }
    async writeValueWithResponse(bytes) {
        if (this.writeGates) await this.writeGates.shift();
        this.writes.push(Array.from(bytes));
    }
    notifyFrame(bytes) {
        this.value = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        this.dispatchEvent(new Event('characteristicvaluechanged'));
    }
}

class FakeDevice extends EventTarget {
    constructor({ watch = false } = {}) {
        super();
        this.chrs = { [UUIDS.state]: new FakeChr(), [UUIDS.command]: new FakeChr() };
        this.connectFails = 0; // how many gatt.connect() calls throw first
        this.connectCalls = 0;
        const self = this;
        this.gatt = {
            connected: false,
            async connect() {
                self.connectCalls++;
                if (self.connectFails > 0) { self.connectFails--; throw new Error('connect failed'); }
                self.gatt.connected = true;
            },
            disconnect() {
                self.gatt.connected = false;
                self.dispatchEvent(new Event('gattserverdisconnected'));
            },
            async getPrimaryService(uuid) {
                check('service uuid', uuid === UUIDS.service, `(${uuid})`);
                return {
                    async getCharacteristic(u) { return self.chrs[u]; },
                };
            },
        };
        if (watch) {
            this.watchingAdvertisements = false;
            this.lastWatchSignal = null;
            this.watchAdvertisements = ({ signal } = {}) => {
                this.watchingAdvertisements = true;
                this.lastWatchSignal = signal ?? null;
                signal?.addEventListener('abort',
                    () => { this.watchingAdvertisements = false; });
                return Promise.resolve();
            };
        }
    }
    dropLink() {
        this.gatt.connected = false;
        this.dispatchEvent(new Event('gattserverdisconnected'));
    }
    // like Chrome dropping the watch when the window loses focus
    killWatch() { this.watchingAdvertisements = false; }
    advertise() {
        if (this.watchingAdvertisements === undefined || this.watchingAdvertisements) {
            this.dispatchEvent(new Event('advertisementreceived'));
        }
    }
}

function mkLink(overrides = {}) {
    const frames = [];
    const statuses = [];
    const link = new ScaleLink({
        bluetooth: overrides.bluetooth,
        uuids: UUIDS,
        delay: instant,
        now: () => 42,
        onFrame: (bytes, t) => frames.push({ bytes, t }),
        onStatus: (kind, text) => statuses.push({ kind, text }),
        ...overrides.inner,
    });
    return { link, frames, statuses };
}

// --- connect() → frames delivered ------------------------------------------
{
    const dev = new FakeDevice();
    const bt = { requestDevice: async o => dev, getDevices: async () => [] };
    const { link, frames, statuses } = mkLink({ bluetooth: bt });
    check('supported', link.supported === true);
    await link.connect();
    check('live after connect', link.status === 'live', `(${link.status})`);
    check('device connected', dev.gatt.connected === true);
    dev.chrs[UUIDS.state].notifyFrame(new Uint8Array([1, 2, 3]));
    check('frame delivered', frames.length === 1 &&
          frames[0].bytes.getUint8(1) === 2 && frames[0].t === 42);
}

// --- unexpected disconnect → reconnect via retry loop (no watch) ------------
{
    const dev = new FakeDevice(); // no watchAdvertisements
    const bt = { requestDevice: async () => dev };
    const { link, statuses } = mkLink({ bluetooth: bt });
    await link.connect();
    dev.connectFails = 2; // first two reconnect attempts fail → backoff path
    dev.dropLink();
    await tick(); await tick(); await tick(); await tick(); await tick();
    check('reconnect retried', dev.connectCalls >= 3, `(${dev.connectCalls} calls)`);
    check('reconnected live', link.status === 'live', `(${link.status})`);
    check('status saw reconnecting', statuses.some(s => s.kind === 'reconnecting'));
}

// --- reconnect via watchAdvertisements --------------------------------------
{
    const dev = new FakeDevice({ watch: true });
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    dev.dropLink();
    await tick(); await tick(); await tick();
    check('watching adverts', dev.watchingAdvertisements === true);
    check('not connected yet', dev.gatt.connected === false);
    dev.advertise();
    await tick(); await tick(); await tick();
    check('adv → reconnected', dev.gatt.connected === true && link.status === 'live');
}

// --- autoConnect via getDevices ----------------------------------------------
{
    const dev = new FakeDevice();
    const bt = {
        requestDevice: async () => { throw new Error('should not pick'); },
        getDevices: async () => [dev],
    };
    const { link } = mkLink({ bluetooth: bt });
    await link.autoConnect();
    await tick(); await tick(); await tick(); await tick();
    check('autoConnect connected', dev.gatt.connected === true && link.status === 'live');
}

// --- autoConnect without getDevices → idle hint ------------------------------
{
    const bt = { requestDevice: async () => new FakeDevice() }; // no getDevices
    const { link, statuses } = mkLink({ bluetooth: bt });
    await link.autoConnect();
    check('idle status', statuses.some(s => s.kind === 'idle' && /click Connect/.test(s.text)),
          `(${statuses.map(s => s.kind).join(',')})`);
}

// --- disconnect() stops reconnection -----------------------------------------
{
    const dev = new FakeDevice();
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    const calls = dev.connectCalls;
    await link.disconnect();
    await tick(); await tick(); await tick();
    check('disconnect no reconnect', dev.connectCalls === calls && link.status === 'idle');
}

// --- send() serialization -----------------------------------------------------
{
    const dev = new FakeDevice();
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    const chr = dev.chrs[UUIDS.command];
    const gate = [];
    chr.writeGates = gate;
    let gateResolve;
    gate.push(new Promise(r => { gateResolve = r; })); // first write blocks
    gate.push(Promise.resolve());                       // second goes through
    const p1 = link.send(new Uint8Array([1]));
    const p2 = link.send(new Uint8Array([2]));
    await tick();
    check('second write waits', chr.writes.length === 0, `(${chr.writes.length})`);
    gateResolve();
    await p1; await p2;
    check('writes serialized in order',
          chr.writes.length === 2 && chr.writes[0][0] === 1 && chr.writes[1][0] === 2,
          `(${JSON.stringify(chr.writes)})`);
    // a rejected write must not poison the queue
    chr.writeValueWithResponse = async () => { throw new Error('gatt err'); };
    await link.send(new Uint8Array([3])).catch(() => {});
    chr.writeValueWithResponse = async b => chr.writes.push(Array.from(b));
    await link.send(new Uint8Array([4]));
    check('queue survives failure', chr.writes.at(-1)[0] === 4);
}

// --- unsupported ---------------------------------------------------------------
{
    const { link, statuses } = mkLink({ bluetooth: undefined });
    check('unsupported flag', link.supported === false);
    await link.autoConnect();
    const s = statuses.find(x => x.kind === 'unsupported');
    check('unsupported status', !!s && /Chrome/.test(s.text) && /chrome:\/\/flags/.test(s.text),
          `(${s?.text})`);
}

// --- watch uses AbortSignal, aborted on first advert --------------------------
{
    const dev = new FakeDevice({ watch: true });
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    dev.dropLink();
    await tick(); await tick(); await tick();
    check('watch signal passed', dev.lastWatchSignal instanceof AbortSignal);
    check('watch active pre-advert', dev.watchingAdvertisements === true);
    dev.advertise();
    await tick(); await tick(); await tick();
    check('watch aborted on advert', dev.lastWatchSignal.aborted === true);
    check('connected after advert', link.status === 'live');
}

// --- _connecting guard: a second disconnect during a pending watch -----------
{
    const dev = new FakeDevice({ watch: true });
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    const before = dev.connectCalls;
    dev.dropLink();
    await tick();
    dev.dropLink(); // duplicate event while the watch is pending
    await tick(); await tick(); await tick();
    dev.advertise();
    await tick(); await tick(); await tick();
    check('no double connect attempt', dev.connectCalls === before + 1,
          `(${dev.connectCalls - before} extra calls)`);
}

// --- resume() re-arms after Chrome killed the watch ---------------------------
{
    const dev = new FakeDevice({ watch: true });
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    dev.dropLink();
    await tick(); await tick(); await tick();
    check('watching before blur', dev.watchingAdvertisements === true);
    dev.killWatch();            // window hidden — Chrome stops delivering
    dev.advertise();            // advert while not watching → no event
    await tick(); await tick();
    check('still offline', dev.gatt.connected === false);
    link.resume();              // window focused again
    await tick(); await tick(); await tick();
    check('watch re-armed on resume', dev.watchingAdvertisements === true);
    dev.advertise();
    await tick(); await tick(); await tick();
    check('resume → live', link.status === 'live');
}

// --- resume() is a no-op when already connected --------------------------------
{
    const dev = new FakeDevice({ watch: true });
    const bt = { requestDevice: async () => dev };
    const { link } = mkLink({ bluetooth: bt });
    await link.connect();
    const calls = dev.connectCalls;
    link.resume();
    await tick();
    check('resume while live = noop', dev.connectCalls === calls);
}

// --- cancelled picker → idle, disarmed -----------------------------------------
{
    const bt = { requestDevice: async () => { throw new Error('User cancelled'); } };
    const { link, statuses } = mkLink({ bluetooth: bt });
    let threw = false;
    await link.connect().catch(() => { threw = true; });
    check('picker error rethrown', threw);
    check('cancel → idle', link.status === 'idle',
          `(${link.status})`);
    check('cancel disarms', link._wantConn === false);
    check('cancel text', statuses.at(-1).text === 'connect cancelled');
}

if (failures) { console.error(`${failures} FAILED`); process.exit(1); }
console.log('all ble tests passed');
