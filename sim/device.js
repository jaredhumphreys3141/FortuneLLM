// The board itself: an Arduino_GFX-shaped panel, the live boot screen from
// FortuneLLM.ino, and the fortune / landscape cycle its loop() drives.
(function (root) {
  'use strict';
  var synth = root.synth, scene = root.scene;

  var SCREEN_W = 320, SCREEN_H = 172;

  // --------------------------------------------------------------- printf ----
  function fmt(spec) {
    var args = Array.prototype.slice.call(arguments, 1), i = 0;
    return spec.replace(/%(?:\.(\d+))?l?l?([dufsc%])/g, function (m, prec, kind) {
      if (kind === '%') return '%';
      var v = args[i++];
      if (kind === 'f') return Number(v).toFixed(prec === undefined ? 6 : +prec);
      if (kind === 'd' || kind === 'u') return String(Math.trunc(v));
      return String(v);
    });
  }

  // ----------------------------------------------- Arduino_GFX-shaped panel ----
  // Holds what is physically lit on the ST7789. The sketch composes most of a
  // screen in `frame` off-panel and pushes it, but the boot screen prints text
  // straight onto the panel, so both surfaces have to exist separately.
  function Panel(font) {
    this.w = SCREEN_W; this.h = SCREEN_H;
    this.px = new Uint16Array(SCREEN_W * SCREEN_H);
    this.font = font;                 // 1280 bytes, Adafruit GFX classic 5x7
    this.cx = 0; this.cy = 0; this.size = 1; this.color = 0xFFFF;
    this.dirty = true;
  }
  Panel.prototype.fillScreen = function (c) { this.px.fill(c); this.dirty = true; };
  Panel.prototype.setTextSize = function (s) { this.size = s; };
  Panel.prototype.setTextColor = function (c) { this.color = c; };
  Panel.prototype.setCursor = function (x, y) { this.cx = x; this.cy = y; };
  Panel.prototype.draw16bitRGBBitmap = function (x0, y0, src, w, h) {
    for (var y = 0; y < h; y++) {
      var dy = y0 + y;
      if (dy < 0 || dy >= this.h) continue;
      for (var x = 0; x < w; x++) {
        var dx = x0 + x;
        if (dx < 0 || dx >= this.w) continue;
        this.px[dy * this.w + dx] = src[y * w + x];
      }
    }
    this.dirty = true;
  };
  // Classic 5x7 glyphs in a 6x8 cell, drawn transparently (no text background).
  Panel.prototype.drawChar = function (x, y, ch, color, size) {
    var c = ch.charCodeAt(0);
    if (c > 255) c = 63;
    for (var i = 0; i < 5; i++) {
      var col = this.font[c * 5 + i];
      for (var j = 0; j < 8; j++) {
        if (!(col & (1 << j))) continue;
        for (var sy = 0; sy < size; sy++) {
          for (var sx = 0; sx < size; sx++) {
            var px = x + i * size + sx, py = y + j * size + sy;
            if (px >= 0 && px < this.w && py >= 0 && py < this.h) this.px[py * this.w + px] = color;
          }
        }
      }
    }
    this.dirty = true;
  };
  // setTextWrap(false) in setup(), so text just runs off the edge.
  Panel.prototype.print = function (s) {
    s = String(s);
    for (var i = 0; i < s.length; i++) {
      var ch = s[i];
      if (ch === '\n') { this.cy += 8 * this.size; this.cx = 0; continue; }
      if (ch !== '\r') this.drawChar(this.cx, this.cy, ch, this.color, this.size);
      this.cx += 6 * this.size;
    }
  };
  Panel.prototype.printf = function () { this.print(fmt.apply(null, arguments)); };

  // ----------------------------------------------------------------- Device ----
  function Device(opts) {
    this.gpt = opts.gpt;
    this.blocklist = opts.blocklist;
    this.meta = opts.meta;
    this.onSerial = opts.onSerial || function () {};
    this.onState = opts.onState || function () {};

    this.gfx = new Panel(opts.font);
    this.frame = new Uint16Array(SCREEN_W * SCREEN_H);
    this.distBuf = new Float32Array(SCREEN_W * SCREEN_H);

    // Settings, as declared at the top of FortuneLLM.ino.
    this.TEMPERATURE = 0.8;
    this.TOP_K = 8;
    this.MAX_GEN = 110;
    this.MAX_CHARS = 90;
    this.MAX_TRIES = 6;
    this.SHOW_MS = 3000;
    this.LED_LEVEL = 40;
    this.msPerChar = 18;            // how long a token takes on the board (estimate)

    this.led = [0, 0, 0];
    this.buttonHeld = false;
    this.busy = false;              // true while the sketch is blocked generating
    this.screen = 'off';
    this.lastScene = null;
    this.sceneCount = 0;
    this.fortuneCount = 0;
  }

  Device.prototype.millis = function () { return performance.now() - this.t0; };

  Device.prototype.rndFloat = function () {
    var s = this.rng;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.rng = s;
    return (s >>> 8) * (1.0 / 16777216.0);
  };
  Device.prototype.espRandom = function () {
    var s = this.rng;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.rng = s;
    return s;
  };

  // ------------------------------------------------------------- rendering ----
  Device.prototype.showText = function (text) {
    var t0 = this.millis();
    synth.render(this.frame, this.distBuf, SCREEN_W, SCREEN_H, text);
    this.gfx.draw16bitRGBBitmap(0, 0, this.frame, SCREEN_W, SCREEN_H);
    this.onSerial(fmt('  (fortune screen rendered in %lu ms)', Math.round(this.millis() - t0)));
  };

  Device.prototype.newScene = function () {
    var t0 = this.millis();
    this.lastScene = scene.render(this.frame, this.distBuf, SCREEN_W, SCREEN_H, this.espRandom());
    this.sceneCount++;
    this.gfx.draw16bitRGBBitmap(0, 0, this.frame, SCREEN_W, SCREEN_H);
    this.onSerial(fmt('Landscape (rendered in %lu ms)', Math.round(this.millis() - t0)));
  };

  // -------------------------------------------------- Live status screen ----
  // Shown at power-up while the first fortune is written.
  var CYAN = synth.to565([110, 230, 255]);
  var PINK = synth.to565([255, 90, 210]);
  var GOLD = synth.to565([255, 215, 100]);
  var DIM = synth.to565([190, 150, 210]);
  var TEXT_Y = 76, TEXT_LINES = 4, LINE_H = 18, WRAP = 25;

  // Restore a rectangle of the background from the frame buffer (erases old text).
  Device.prototype.statusRestore = function (x0, y0, w, h) {
    for (var y = y0; y < y0 + h && y < SCREEN_H; y++)
      this.gfx.draw16bitRGBBitmap(x0, y, this.frame.subarray(y * SCREEN_W + x0, y * SCREEN_W + x0 + w), w, 1);
  };

  Device.prototype.statusBegin = function () {
    var d = this.meta.dims;
    this.statusActive = true;
    this.statusLastDraw = -1e9;
    this.screen = 'boot';
    synth.drawBackground(this.frame, SCREEN_W, SCREEN_H);
    this.gfx.draw16bitRGBBitmap(0, 0, this.frame, SCREEN_W, SCREEN_H);
    var params = d.VOCAB * d.DIM + d.CTX * d.DIM +
                 d.LAYERS * (12 * d.DIM * d.DIM + 4 * d.DIM) + 2 * d.DIM;
    var g = this.gfx;
    g.setTextSize(2); g.setTextColor(CYAN); g.setCursor(8, 6);
    g.print('WRITING A FORTUNE');
    g.setTextSize(1); g.setTextColor(DIM); g.setCursor(8, 26);
    g.printf('TinyGPT: %ldK params, %d layers, on-chip', Math.trunc(params / 1000), d.LAYERS);
  };

  Device.prototype.statusUpdate = function (attempt, text, attemptStart, note, force) {
    if (!this.statusActive) return;
    if (!force && this.millis() - this.statusLastDraw < 80) return;
    this.statusLastDraw = this.millis();
    var g = this.gfx;

    // stats line
    this.statusRestore(0, 40, SCREEN_W, 34);
    var ms = this.millis() - attemptStart;
    g.setTextSize(1); g.setTextColor(GOLD); g.setCursor(8, 42);
    g.printf('Try %d of %d   Characters: %d', attempt, this.MAX_TRIES, text.length);
    g.setCursor(8, 54);
    if (text.length) g.printf('Speed: %.1f ms/char   Elapsed: %.1f s', ms / text.length, ms / 1000.0);
    else g.print('Starting...');
    if (note) {
      g.setTextColor(PINK); g.setCursor(8, 64);
      if (note === 'Done!') g.print('Done!');
      else g.printf('Rejected (%s) - trying again', note);
    }

    // the fortune as it's being written (last few wrapped lines)
    var lines = [], line = '';
    for (var i = 0; i < text.length; i++) {
      line += text[i];
      if (line.length >= WRAP) {
        var sp = line.lastIndexOf(' ');
        if (sp > 0) { lines.push(line.slice(0, sp)); line = line.slice(sp + 1); }
        else { lines.push(line); line = ''; }
      }
    }
    lines.push(line + '_');                                  // cursor
    var first = lines.length > TEXT_LINES ? lines.length - TEXT_LINES : 0;
    this.statusRestore(0, TEXT_Y, SCREEN_W, TEXT_LINES * LINE_H);
    g.setTextSize(2); g.setTextColor(PINK);
    for (var k = first; k < lines.length; k++) {
      g.setCursor(8, TEXT_Y + (k - first) * LINE_H);
      g.print(lines[k]);
    }
  };

  // ------------------------------------------------------ acceptance rules ----
  Device.prototype.rejectReason = function (s) {
    if (this.blocklist.contains(s)) return 'blocked word';
    if (s.length < 12) return 'too short';
    if (s.length > this.MAX_CHARS) return 'too long';
    var last = s[s.length - 1];
    if (last !== '.' && last !== '!' && last !== '?') return 'unfinished';
    return null;                                             // SKIP_MADE_UP_WORDS is false
  };

  // --------------------------------------------------- generation, stepped ----
  // On the board makeFortune() blocks; here it is advanced a token at a time so
  // the page stays responsive, at the same pace the board would manage.
  Device.prototype.beginFortune = function () {
    this.busy = true;
    this.gen = {
      t0: this.millis(), ta: this.millis(), attempt: 1, chars: 0,
      out: '', tok: 0, i: 0, blocked: false, nextTokenAt: this.millis(),
      pauseUntil: 0, finished: false, result: null
    };
    this.gpt.reset();
    this.statusUpdate(1, '', this.gen.ta, null, true);
  };

  Device.prototype.stepFortune = function (budgetMs) {
    var g = this.gen, now = this.millis(), deadline = performance.now() + budgetMs;
    if (g.pauseUntil > now) return false;

    while (!g.finished && performance.now() < deadline) {
      now = this.millis();
      if (g.pauseUntil > now) return false;
      if (now < g.nextTokenAt) { this.statusUpdate(g.attempt, g.out, g.ta, null, false); return false; }
      g.nextTokenAt += this.msPerChar;
      if (g.nextTokenAt < now - this.msPerChar * 4) g.nextTokenAt = now;   // don't bank a backlog

      var done = false;
      if (g.i >= this.MAX_GEN) {
        done = true;
      } else {
        this.gpt.step(g.tok);
        g.tok = this.gpt.sample(this.TEMPERATURE, this.TOP_K, this.rndFloat());
        g.i++;
        if (g.tok === 0) done = true;                        // token 0 ends the fortune
        else {
          g.out += this.gpt.chars[g.tok];
          // A finished word on the blocklist ends the attempt right away, so the
          // rest of the fortune is never written and none of it reaches the screen.
          var last = g.out[g.out.length - 1];
          if (!root.Blocklist.isWordChar(last) && this.blocklist.endsWithTerm(g.out)) {
            g.blocked = true;
            done = true;
          }
          var pulse = 0.5 + 0.5 * Math.sin(this.millis() / 120.0);
          this.led = [this.LED_LEVEL * pulse, 0, this.LED_LEVEL * pulse * 0.6];
          this.statusUpdate(g.attempt, g.out, g.ta, null, false);
        }
      }
      if (done) this.finishAttempt();
    }
    return g.finished;
  };

  Device.prototype.finishAttempt = function () {
    var g = this.gen;
    g.chars += g.out.length + 1;
    var why = g.blocked ? 'blocked word' : this.rejectReason(g.out);
    var ok = (why === null);
    // A blocked fortune is never redrawn or logged - the reason alone is enough.
    this.statusUpdate(g.attempt, g.blocked ? '' : g.out, g.ta, ok ? 'Done!' : why, true);
    this.onSerial(fmt('  try %d: %s%s%s', g.attempt, g.blocked ? '(withheld)' : g.out,
      why ? '  -> rejected: ' : '', why ? why : ''));

    if (ok) { this.completeFortune(g.out, g.chars); return; }
    if (this.statusActive) g.pauseUntil = this.millis() + 600;   // let the rejection be seen
    if (g.attempt >= this.MAX_TRIES) {
      this.completeFortune('The stars are silent. Try again.', g.chars);
      return;
    }
    g.attempt++;
    g.out = ''; g.tok = 0; g.i = 0; g.blocked = false;
    g.ta = this.millis();
    g.nextTokenAt = g.pauseUntil || this.millis();
    this.gpt.reset();
  };

  Device.prototype.completeFortune = function (text, chars) {
    var g = this.gen;
    g.finished = true;
    g.result = text;
    var ms = this.millis() - g.t0;
    this.onSerial(fmt('Fortune: %s', text));
    this.onSerial(fmt('  (%d chars generated in %lu ms, %.1f ms/char)',
      chars, Math.round(ms), chars ? ms / chars : 0.0));
    this.led = [0, 0, 0];
    this.fortuneCount++;
  };

  // ------------------------------------------- Setup / loop, from the .ino ----
  Device.prototype.boot = function (seed) {
    this.t0 = performance.now();
    this.rng = (seed >>> 0) || 1;
    this.seed = this.rng;
    this.gfx.fillScreen(0);
    var d = this.meta.dims;
    this.onSerial('');
    this.onSerial(fmt('TinyGPT: %d layers, dim %d, vocab %d, context %d -> ready',
      d.LAYERS, d.DIM, d.VOCAB, d.CTX));
    this.nextIsFortune = true;
    this.havePending = false;
    this.pendingFortune = '';
    this.shownAt = 0;
    this.phase = 'setup-gen';       // setup(): first fortune, with the status screen
    this.statusBegin();
    this.beginFortune();
    this.onState();
  };

  // showNext() from the sketch. Generation is kicked off rather than awaited;
  // `phase` carries what the blocking call would have been doing.
  Device.prototype.showNext = function () {
    if (this.nextIsFortune) {
      if (!this.havePending) { this.phase = 'fortune-gen'; this.beginFortune(); return; }
      this.showText(this.pendingFortune);
      this.havePending = false;
      this.shownAt = this.millis();
      this.screen = 'fortune';
      this.currentFortune = this.pendingFortune;
      this.nextIsFortune = false;
      this.phase = 'showing';
      this.busy = false;
    } else {
      this.newScene();
      this.shownAt = this.millis();   // countdown starts now; the fortune is written behind it
      this.screen = 'scene';
      this.nextIsFortune = true;
      this.phase = 'scene-gen';       // the board blocks here writing the next fortune
      this.beginFortune();
    }
    this.onState();
  };

  Device.prototype.press = function () {
    // loop() only polls the button when it is not blocked inside makeFortune().
    if (this.busy || this.phase !== 'showing') return false;
    this.showNext();
    return true;
  };

  Device.prototype.tick = function (budgetMs) {
    if (this.phase === 'setup-gen') {
      if (this.stepFortune(budgetMs)) {
        this.pendingFortune = this.gen.result;
        this.havePending = true;
        this.phase = 'setup-pause';
        this.pauseUntil = this.millis() + 700;               // a moment to see the result
      }
    } else if (this.phase === 'setup-pause') {
      if (this.millis() >= this.pauseUntil) {
        this.statusActive = false;                           // status::end()
        this.showNext();
      }
    } else if (this.phase === 'fortune-gen' || this.phase === 'scene-gen') {
      if (this.stepFortune(budgetMs)) {
        this.pendingFortune = this.gen.result;
        this.havePending = true;
        this.busy = false;
        if (this.phase === 'fortune-gen') { this.nextIsFortune = true; this.showNext(); }
        else { this.phase = 'showing'; this.onState(); }
      }
    } else if (this.phase === 'showing') {
      if (this.millis() - this.shownAt >= this.SHOW_MS) this.showNext();
    }
  };

  root.Device = Device;
  root.SCREEN_W = SCREEN_W;
  root.SCREEN_H = SCREEN_H;
  root.fmt = fmt;
})(window);
