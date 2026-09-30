// Web Bluetooth link to the scale — no DOM access, so node tests can
// import it. The caller injects `bluetooth` (navigator.bluetooth or a
// fake), the GATT uuids from Screen.bleUuids(), and the callbacks.
//
// Behaviour:
//   autoConnect() — on page load. If bluetooth.getDevices exists, walk
//     the permitted devices: watchAdvertisements when supported (the
//     page connects when the scale advertises), else retry gatt.connect().
//     Without getDevices there is no way to reconnect silently — status
//     goes 'idle' ("click Connect").
//   connect() — requestDevice picker; needs a user gesture.
//   reconnect — on 'gattserverdisconnected' we did not ask for:
//     watchAdvertisements path if available, else gatt.connect() retried
//     at 1 s → 2 → 4 … capped at 10 s, forever, until connected or
//     disconnect() is called.
//   send() — writes are serialized on a promise chain (Web Bluetooth
//     throws "GATT operation already in progress" on overlapping writes);
//     writeValueWithResponse so a dead link surfaces as a rejected write.

export class ScaleLink {
    static WATCH_CANCELLED = Symbol('watch-cancelled');

    constructor({ bluetooth, uuids, onFrame, onStatus, delay, now } = {}) {
        this.bt = bluetooth ?? null;
        this.uuids = uuids;
        this.onFrame = onFrame ?? (() => {});
        this.onStatus = onStatus ?? (() => {});
        this._delay = delay ?? (ms => new Promise(r => setTimeout(r, ms)));
        this._now = now ?? (() => performance.now());
        this.device = null;
        this.cmdChr = null;
        this.status = 'idle';
        this._wantConn = false;
        this._connecting = false;
        this._advHandler = null;
        this._advAbort = null;
        this._cancelWatch = null;
        this._reconnP = null;
        this._discHandler = null;
        this._writeQueue = Promise.resolve();
    }

    get supported() {
        return !!this.bt && typeof this.bt.requestDevice === 'function';
    }

    get connected() {
        return !!(this.device?.gatt?.connected && this.cmdChr);
    }

    _setStatus(kind, text) {
        this.status = kind;
        this.onStatus(kind, text);
    }

    static unsupportedText() {
        return 'Web Bluetooth 不可用 — use Chrome or Edge ' +
               '(Linux: chrome://flags/#enable-experimental-web-platform-features)';
    }

    _attach(device) {
        if (device.__scaleLinkAttached) {
            return;
        }
        device.__scaleLinkAttached = true;
        this._discHandler = () => {
            if (this._wantConn) {
                this._reconnect(device);
            } else {
                this._setStatus('idle', 'disconnected');
            }
        };
        device.addEventListener('gattserverdisconnected', this._discHandler);
    }

    /// Subscribe to the state characteristic after a gatt connection.
    /// cmdChr is assigned last so a failure leaves the link "not
    /// connected" and the retry loop can re-attempt the whole subscribe.
    async _subscribe(device) {
        const svc = await device.gatt.getPrimaryService(this.uuids.service);
        const chr = await svc.getCharacteristic(this.uuids.state);
        const cmd = await svc.getCharacteristic(this.uuids.command);
        chr.__scaleOnFrame = ev => this.onFrame(ev.target.value, this._now());
        chr.addEventListener('characteristicvaluechanged', chr.__scaleOnFrame);
        await chr.startNotifications();
        this.cmdChr = cmd;
        this._setStatus('live', 'connected');
    }

    /// requestDevice picker — needs a user gesture. On cancel/failure the
    /// status drops back to idle; a rejected pick means no device was
    /// obtained, so the link disarms (auto-reconnect has nothing to aim
    /// at). The error is rethrown for the caller.
    async connect() {
        if (!this.supported) {
            this._setStatus('unsupported', ScaleLink.unsupportedText());
            return;
        }
        this._wantConn = true;
        this._setStatus('connecting', 'choose the scale…');
        try {
            const device = await this.bt.requestDevice(
                { filters: [{ services: [this.uuids.service] }] });
            this.device = device;
            this._attach(device);
            await device.gatt.connect();
            await this._subscribe(device);
        } catch (e) {
            if (!this.device) {
                this._wantConn = false;
            }
            this._setStatus('idle', 'connect cancelled');
            throw e;
        }
    }

    /// Silent reconnect for devices the page already has permission for.
    async autoConnect() {
        if (!this.supported) {
            this._setStatus('unsupported', ScaleLink.unsupportedText());
            return;
        }
        this._wantConn = true;
        if (typeof this.bt.getDevices !== 'function') {
            this._setStatus('idle', 'click Connect');
            return;
        }
        const devices = await this.bt.getDevices();
        if (!devices.length) {
            this._setStatus('idle', 'click Connect');
            return;
        }
        for (const d of devices) {
            this.device = d;
            this._attach(d);
            this._reconnect(d);
        }
    }

    _reconnect(device) {
        if (this._connecting || !this._wantConn) {
            return this._reconnP ?? Promise.resolve();
        }
        this._connecting = true;
        this._setStatus('reconnecting', 'link lost — reconnecting…');
        this._reconnP = (async () => {
            try {
                if (typeof device.watchAdvertisements === 'function') {
                    await this._watchAndConnect(device);
                } else {
                    await this._retryConnect(device);
                }
            } finally {
                this._connecting = false;
                this._reconnP = null;
            }
        })();
        return this._reconnP;
    }

    /// Chrome path: watch for the scale's advertisement (AbortSignal —
    /// abortWatchAdvertisements is non-standard), connect + subscribe when
    /// it shows up. Resolves only once subscribed; watching or connecting
    /// failure falls back to the retry loop. A sentinel rejection marks
    /// disconnect()/resume() cancels — those don't fall back.
    async _watchAndConnect(device) {
        const ac = new AbortController();
        this._advAbort = ac;
        try {
            const connected = new Promise((resolve, reject) => {
                this._cancelWatch = () => reject(ScaleLink.WATCH_CANCELLED);
                this._advHandler = () => {
                    device.removeEventListener('advertisementreceived',
                                               this._advHandler);
                    this._advHandler = null;
                    ac.abort();            // stop scanning, keep the event path
                    this._advAbort = null;
                    device.gatt.connect()
                        .then(() => this._subscribe(device))
                        .then(resolve, reject);
                };
                device.addEventListener('advertisementreceived',
                                        this._advHandler);
            });
            await device.watchAdvertisements({ signal: ac.signal });
            await connected;
        } catch (e) {
            if (this._advHandler) {
                device.removeEventListener('advertisementreceived',
                                           this._advHandler);
                this._advHandler = null;
            }
            ac.abort();
            this._advAbort = null;
            if (e !== ScaleLink.WATCH_CANCELLED && this._wantConn) {
                await this._retryConnect(device);
            }
        } finally {
            this._cancelWatch = null;
        }
    }

    /// Chrome stops watchAdvertisements while the window is hidden — call
    /// this on visibilitychange/focus to re-arm the reconnect. A dead
    /// pending watch is cancelled first (it holds _connecting); the new
    /// _reconnect is chained behind the pending one.
    resume() {
        const d = this.device;
        if (!this._wantConn || !d || this.connected) {
            return;
        }
        if (this._advAbort && d.watchingAdvertisements === false) {
            this._cancelWatch?.();
        }
        const after = () => {
            if (this._wantConn && !this.connected) {
                this._reconnect(d);
            }
        };
        if (this._reconnP) {
            this._reconnP.finally(after);
        } else {
            after();
        }
    }

    /// Fallback path (Firefox / no watchAdvertisements): hammer
    /// gatt.connect() at 1 s → 2 → 4 … capped 10 s, forever. A connected
    /// gatt with a failed subscribe also retries — half-open counts as
    /// not-live.
    async _retryConnect(device) {
        let backoff = 1000;
        while (this._wantConn && device.gatt && !this.connected) {
            try {
                if (!device.gatt.connected) {
                    await device.gatt.connect();
                }
                await this._subscribe(device);
                return;
            } catch {
                this._setStatus('reconnecting',
                                `retrying in ${backoff / 1000} s…`);
                await this._delay(backoff);
                backoff = Math.min(backoff * 2, 10000);
            }
        }
    }

    /// Serialize writes — overlapping GATT ops throw.
    send(bytes) {
        const p = this._writeQueue.then(() => {
            if (!this.connected) {
                throw new Error('not connected');
            }
            return this.cmdChr.writeValueWithResponse(bytes);
        });
        this._writeQueue = p.catch(() => {}); // keep the chain alive
        return p;
    }

    async disconnect() {
        this._wantConn = false;
        const d = this.device;
        if (d) {
            if (this._advHandler) {
                d.removeEventListener('advertisementreceived', this._advHandler);
                this._advHandler = null;
            }
            try {
                this._cancelWatch?.();
                this._advAbort?.abort();
                this._advAbort = null;
            } catch { /* not watching */ }
            try {
                d.gatt?.disconnect();
            } catch { /* already gone */ }
        }
        this.cmdChr = null;
        this._setStatus('idle', 'disconnected');
    }
}
