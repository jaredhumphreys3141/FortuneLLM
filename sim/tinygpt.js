// Port of gpt.h + blocklist.h - the transformer inference engine and the
// blocked-term filter, with the same maths and the same acceptance rules.
(function (root) {
  'use strict';

  function TinyGPT(meta, buf) {
    var d = meta.dims;
    this.D = d.DIM; this.L = d.LAYERS; this.H = d.HEADS; this.T = d.CTX;
    this.V = d.VOCAB; this.HID = d.HIDDEN; this.HS = d.DIM / d.HEADS;
    this.chars = meta.chars.map(function (c) { return String.fromCharCode(c); });

    var all = new Float32Array(buf);
    var w = {};
    for (var k in meta.offsets) {
      var o = meta.offsets[k];
      w[k] = all.subarray(o[0], o[0] + o[1]);
    }
    this.w = w;

    var D = this.D;
    this.kc = new Float32Array(this.L * this.T * D);
    this.vc = new Float32Array(this.L * this.T * D);
    this.x = new Float32Array(D);
    this.xn = new Float32Array(D);
    this.qkv = new Float32Array(3 * D);
    this.y = new Float32Array(D);
    this.hid = new Float32Array(this.HID);
    this.att = new Float32Array(this.T);
    this.logits = new Float32Array(this.V);
    this.pos = 0;
  }

  TinyGPT.prototype.reset = function () { this.pos = 0; };

  function matvec(out, W, wOff, inp, rows, cols) {
    for (var r = 0; r < rows; r++) {
      var o = wOff + r * cols, s = 0;
      for (var c = 0; c < cols; c++) s += W[o + c] * inp[c];
      out[r] = s;
    }
  }

  function layernorm(out, inp, W, wOff, B, bOff, D) {
    var mean = 0, vari = 0, i;
    for (i = 0; i < D; i++) mean += inp[i];
    mean /= D;
    for (i = 0; i < D; i++) { var dv = inp[i] - mean; vari += dv * dv; }
    var inv = 1.0 / Math.sqrt(vari / D + 1e-5);
    for (i = 0; i < D; i++) out[i] = (inp[i] - mean) * inv * W[wOff + i] + B[bOff + i];
  }

  // Feed one token, get logits for the next token.
  TinyGPT.prototype.step = function (token) {
    var D = this.D, L = this.L, H = this.H, T = this.T, V = this.V,
        HID = this.HID, HS = this.HS, w = this.w;
    var x = this.x, xn = this.xn, qkv = this.qkv, y = this.y,
        hid = this.hid, att = this.att, logits = this.logits;
    if (this.pos >= T) this.pos = T - 1;
    var pos = this.pos, i, l, h, t;

    for (i = 0; i < D; i++) x[i] = w.W_wte[token * D + i] + w.W_wpe[pos * D + i];

    for (l = 0; l < L; l++) {
      // ---- attention ----
      layernorm(xn, x, w.W_ln1_w, l * D, w.W_ln1_b, l * D, D);
      matvec(qkv, w.W_qkv, l * 3 * D * D, xn, 3 * D, D);
      var kOff = l * T * D, vOff = l * T * D;
      for (i = 0; i < D; i++) {
        this.kc[kOff + pos * D + i] = qkv[D + i];
        this.vc[vOff + pos * D + i] = qkv[2 * D + i];
      }
      var scale = 1.0 / Math.sqrt(HS);
      for (h = 0; h < H; h++) {
        var qOff = h * HS, mx = -1e30, s;
        for (t = 0; t <= pos; t++) {
          var kOff2 = kOff + t * D + h * HS;
          s = 0;
          for (i = 0; i < HS; i++) s += qkv[qOff + i] * this.kc[kOff2 + i];
          att[t] = s * scale;
          if (att[t] > mx) mx = att[t];
        }
        var sum = 0;
        for (t = 0; t <= pos; t++) { att[t] = Math.exp(att[t] - mx); sum += att[t]; }
        var yOff = h * HS;
        for (i = 0; i < HS; i++) y[yOff + i] = 0;
        for (t = 0; t <= pos; t++) {
          var a = att[t] / sum, vOff2 = vOff + t * D + h * HS;
          for (i = 0; i < HS; i++) y[yOff + i] += a * this.vc[vOff2 + i];
        }
      }
      matvec(xn, w.W_proj, l * D * D, y, D, D);
      for (i = 0; i < D; i++) x[i] += xn[i];

      // ---- MLP ----
      layernorm(xn, x, w.W_ln2_w, l * D, w.W_ln2_b, l * D, D);
      matvec(hid, w.W_fc, l * HID * D, xn, HID, D);
      for (i = 0; i < HID; i++) {
        var u = hid[i];
        hid[i] = 0.5 * u * (1.0 + Math.tanh(0.7978845608 * (u + 0.044715 * u * u * u)));
      }
      matvec(xn, w.W_fc_proj, l * D * HID, hid, D, HID);
      for (i = 0; i < D; i++) x[i] += xn[i];
    }

    layernorm(xn, x, w.W_lnf_w, 0, w.W_lnf_b, 0, D);
    matvec(logits, w.W_wte, 0, xn, V, D);          // tied output head
    this.pos++;
    return logits;
  };

  // Sample from the last logits. r must be uniform in [0, 1).
  TinyGPT.prototype.sample = function (temperature, topK, r) {
    var V = this.V, logits = this.logits, i;
    var p = new Float64Array(V);
    var mx = -1e30;
    for (i = 0; i < V; i++) if (logits[i] > mx) mx = logits[i];
    var cutoff = -1e30;
    if (topK > 0 && topK < V) {                    // find the k-th largest logit
      var tmp = Float64Array.from(logits);
      for (var k = 0; k < topK; k++) {
        var best = 0;
        for (i = 1; i < V; i++) if (tmp[i] > tmp[best]) best = i;
        cutoff = tmp[best];
        tmp[best] = -1e30;
      }
    }
    var sum = 0;
    for (i = 0; i < V; i++) {
      p[i] = (logits[i] >= cutoff) ? Math.exp((logits[i] - mx) / temperature) : 0.0;
      sum += p[i];
    }
    var acc = 0, target = r * sum;
    for (i = 0; i < V; i++) {
      acc += p[i];
      if (acc > target) return i;
    }
    return V - 1;
  };

  // ------------------------------------------------------------ blocklist ----
  function fnv1a(s) {
    var h = 2166136261;
    for (var i = 0; i < s.length; i++) h = Math.imul(h ^ (s.charCodeAt(i) & 0xFF), 16777619);
    return h >>> 0;
  }

  function Blocklist(hashes, maxWords) { this.h = hashes; this.maxWords = maxWords; }

  // Letters, digits and apostrophes make up a word; everything else separates them.
  Blocklist.isWordChar = function (c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === "'";
  };

  Blocklist.prototype.isTerm = function (phrase) {
    var target = fnv1a(phrase), lo = 0, hi = this.h.length - 1;
    while (lo <= hi) {
      var mid = (lo + hi) >>> 1, v = this.h[mid] >>> 0;
      if (v === target) return true;
      if (v < target) lo = mid + 1; else hi = mid - 1;
    }
    return false;
  };

  // Split text into lower-case words, dropping all punctuation and spacing.
  Blocklist.split = function (text) {
    var out = [], w = '';
    for (var i = 0; i <= text.length; i++) {
      var c = i < text.length ? text[i] : ' ';
      if (Blocklist.isWordChar(c)) w += c.toLowerCase();
      else if (w) { out.push(w); w = ''; }
    }
    return out;
  };

  // True if any run of words ending at word `last` is a blocked term.
  Blocklist.prototype.endingAt = function (words, last) {
    var phrase = '';
    for (var n = 0; n < this.maxWords && n <= last; n++) {
      var first = words[last - n];
      phrase = n ? first + ' ' + phrase : first;
      if (this.isTerm(phrase)) return true;
    }
    return false;
  };

  Blocklist.prototype.contains = function (text) {
    var words = Blocklist.split(text);
    for (var i = 0; i < words.length; i++) if (this.endingAt(words, i)) return true;
    return false;
  };

  // True if the text *ends* with a blocked term - for checking a fortune while
  // it is still being written.
  Blocklist.prototype.endsWithTerm = function (text) {
    var words = Blocklist.split(text);
    return words.length > 0 && this.endingAt(words, words.length - 1);
  };

  root.TinyGPT = TinyGPT;
  root.Blocklist = Blocklist;
  root.fnv1a = fnv1a;
})(window);
