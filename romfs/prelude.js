/* Nabootsing van de stukjes browser die de game gebruikt.
   Alles wat tekent gaat naar de C-code (__newCtx, __surfNew, ...). */
(function (g) {
  __loadFont(0, 'fonts/sb.ttf'); __loadFont(1, 'fonts/b.ttf'); __loadFont(2, 'fonts/sbi.ttf');
  g.window = g; g.self = g; g.globalThis = g;
  g.console = { log: (...a) => __log(a.join(' ')), warn: (...a) => __log('WARN ' + a.join(' ')), error: (...a) => __log('ERR ' + a.join(' ')) };

  /* ---------- gebeurtenissen ---------- */
  const LS = Symbol('listeners');
  function addEL(t, f) { const m = this[LS] || (this[LS] = {}); (m[t] || (m[t] = [])).push(f); }
  function removeEL(t, f) { const m = this[LS]; if (m && m[t]) m[t] = m[t].filter(x => x !== f); }
  function fire(o, t, e) {
    const m = o[LS]; if (!m || !m[t]) return; e.type = t;
    for (const f of m[t].slice()) { try { f.call(o, e); } catch (x) { __log('Fout in ' + t + ': ' + (x && x.stack || x)); } }
  }
  g.addEventListener = function (t, f) { addEL.call(g, t, f); };
  g.removeEventListener = function (t, f) { removeEL.call(g, t, f); };

  /* ---------- canvas ---------- */
  class Canvas {
    constructor(w, h) { this._s = __surfNew(w || 300, h || 150); this._c = null; }
    get width() { return __surfW(this._s); }
    set width(v) { __surfResize(this._s, v | 0, __surfH(this._s)); }
    get height() { return __surfH(this._s); }
    set height(v) { __surfResize(this._s, __surfW(this._s), v | 0); }
    getContext() { return this._c || (this._c = __newCtx(this._s)); }
    addEventListener(t, f) { addEL.call(this, t, f); }
    removeEventListener(t, f) { removeEL.call(this, t, f); }
    getBoundingClientRect() { const w = this.width + 12, h = this.height + 12; return { left: 0, top: 0, right: w, bottom: h, width: w, height: h }; }
  }
  const top = new Canvas(400, 240), bot = new Canvas(320, 240);
  __bindScreens(top._s, bot._s);
  const screens = { top, bot };

  g.document = {
    getElementById: id => screens[id] || null,
    createElement: tag => new Canvas(300, 150),
    addEventListener(t, f) { addEL.call(this, t, f); },
    removeEventListener(t, f) { removeEL.call(this, t, f); },
    fonts: { ready: Promise.resolve(), load: () => Promise.resolve([]) },
    hidden: false
  };

  /* ---------- afbeeldingen ---------- */
  g.Image = class Image {
    constructor() { this.complete = false; this.naturalWidth = 0; this.naturalHeight = 0; this.onload = null; this.onerror = null; this._src = ''; }
    get src() { return this._src; }
    set src(v) {
      this._src = v; const s = __decode(v);
      if (s) { this._s = s; this.naturalWidth = __surfW(s); this.naturalHeight = __surfH(s); this.complete = true; setTimeout(() => { if (this.onload) this.onload(); }, 0); }
      else setTimeout(() => { if (this.onerror) this.onerror(); }, 0);
    }
    get width() { return this.naturalWidth; } get height() { return this.naturalHeight; }
  };

  /* ---------- opslag op de SD-kaart ---------- */
  const cache = {};
  g.localStorage = {
    getItem(k) { if (k in cache) return cache[k]; const v = __fileRead(k + '.json'); cache[k] = v; return v; },
    setItem(k, v) { v = String(v); if (cache[k] === v) return; cache[k] = v; __fileWrite(k + '.json', v); },
    removeItem(k) { cache[k] = null; __fileRemove(k + '.json'); }
  };

  /* ---------- timers ---------- */
  let tid = 0; const timers = new Map();
  g.setTimeout = (f, ms, ...a) => { const id = ++tid; timers.set(id, { t: __now() + (ms || 0), f, a, iv: 0 }); return id; };
  g.setInterval = (f, ms, ...a) => { const id = ++tid; ms = Math.max(1, ms || 1); timers.set(id, { t: __now() + ms, f, a, iv: ms }); return id; };
  g.clearTimeout = g.clearInterval = id => { timers.delete(id); };
  g.requestAnimationFrame = f => g.setTimeout(() => f(__now()), 16);
  g.performance = { now: () => __now() };
  function runTimers() {
    const now = __now(); const due = [];
    for (const [id, t] of timers) if (t.t <= now) due.push([id, t]);
    due.sort((a, b) => a[1].t - b[1].t || a[0] - b[0]);
    for (const [id, t] of due) {
      if (!timers.has(id)) continue;
      if (t.iv) t.t = now + t.iv; else timers.delete(id);
      try { t.f(...t.a); } catch (x) { __log('Timerfout: ' + (x && x.stack || x)); }
    }
  }

  /* ---------- invoer: 3DS-knoppen en aanraakscherm worden browser-gebeurtenissen ---------- */
  const DIRS = [[64, 'ArrowUp'], [128, 'ArrowDown'], [32, 'ArrowLeft'], [16, 'ArrowRight']];
  const rep = { 64: -1, 128: -1, 32: -1, 16: -1 };
  let prevTouch = false, lx = 0, ly = 0;
  const pev = (t, x, y, b) => ({ type: t, clientX: x + 6, clientY: y + 6, buttons: b, pointerId: 1, target: bot, preventDefault() { }, stopPropagation() { } });
  const kev = key => ({ key, preventDefault() { }, stopPropagation() { } });
  g.__frame = function (down, held, up, tx, ty, touching) {
    if (touching && !prevTouch) fire(bot, 'pointerdown', pev('pointerdown', tx, ty, 1));
    else if (touching && (tx !== lx || ty !== ly)) fire(bot, 'pointermove', pev('pointermove', tx, ty, 1));
    else if (!touching && prevTouch) { fire(bot, 'pointerup', pev('pointerup', lx, ly, 0)); fire(bot, 'pointerleave', pev('pointerleave', lx, ly, 0)); }
    if (touching) { lx = tx; ly = ty; } prevTouch = !!touching;
    for (const [bit, name] of DIRS) {
      if (down & bit) { rep[bit] = 0; fire(g, 'keydown', kev(name)); }
      else if (held & bit) { rep[bit]++; if (rep[bit] >= 14 && (rep[bit] - 14) % 4 === 0) fire(g, 'keydown', kev(name)); }
    }
    if (down & 8) fire(g, 'keydown', kev('Escape'));       /* START = optiesmenu */
    if (down & 512) fire(g, 'keydown', kev('e'));          /* L = einde beurt */
    if (down & 4) { g.__shutdown(); __quit(); }              /* SELECT = afsluiten */
    runTimers();
  };
  g.__shutdown = function () { document.hidden = true; fire(g, 'pagehide', {}); fire(document, 'visibilitychange', {}); };
})(globalThis);
