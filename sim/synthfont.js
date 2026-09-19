// Port of synthfont.h - an original geometric synthwave stroke font + renderer.
// Kept line-for-line faithful to the sketch so the panel shows what the board shows.
(function (root) {
  'use strict';

  // ---------------------------------------------------------------- glyphs ----
  // [char, advance width, [stroke, ...]] where a stroke is a flat [x,y,x,y,...]
  var GLYPHS = [
    ['A', 4, [[0,0, 0,4, 2,6, 4,4, 4,0], [0,2.8, 4,2.8]]],
    ['B', 4, [[0,0, 0,6, 3,6, 4,5, 4,4, 3,3, 0,3], [3,3, 4,2, 4,1, 3,0, 0,0]]],
    ['C', 4, [[4,6, 1,6, 0,5, 0,1, 1,0, 4,0]]],
    ['D', 4, [[0,0, 0,6, 2.5,6, 4,4.5, 4,1.5, 2.5,0, 0,0]]],
    ['E', 4, [[4,6, 0,6, 0,0, 4,0], [0,3, 3,3]]],
    ['F', 4, [[4,6, 0,6, 0,0], [0,3, 3,3]]],
    ['G', 4, [[4,5.2, 3.2,6, 1,6, 0,5, 0,1, 1,0, 3,0, 4,1, 4,3, 2,3]]],
    ['H', 4, [[0,0, 0,6], [4,0, 4,6], [0,3, 4,3]]],
    ['I', 0, [[0,0, 0,6]]],
    ['J', 4, [[4,6, 4,1, 3,0, 1,0, 0,1]]],
    ['K', 4, [[0,0, 0,6], [4,6, 0,2.4], [1.6,3.6, 4,0]]],
    ['L', 4, [[0,6, 0,0, 4,0]]],
    ['M', 5, [[0,0, 0,6, 2.5,3, 5,6, 5,0]]],
    ['N', 4, [[0,0, 0,6, 4,0, 4,6]]],
    ['O', 4, [[1,0, 0,1, 0,5, 1,6, 3,6, 4,5, 4,1, 3,0, 1,0]]],
    ['P', 4, [[0,0, 0,6, 3,6, 4,5, 4,4, 3,3, 0,3]]],
    ['Q', 4, [[1,0, 0,1, 0,5, 1,6, 3,6, 4,5, 4,1, 3,0, 1,0], [2.6,1.4, 4.4,-0.4]]],
    ['R', 4, [[0,0, 0,6, 3,6, 4,5, 4,4, 3,3, 0,3], [2,3, 4,0]]],
    ['S', 4, [[4,5.2, 3.2,6, 1,6, 0,5, 0,4, 1,3, 3,3, 4,2, 4,1, 3,0, 1,0, 0,0.8]]],
    ['T', 4, [[0,6, 4,6], [2,6, 2,0]]],
    ['U', 4, [[0,6, 0,1, 1,0, 3,0, 4,1, 4,6]]],
    ['V', 4, [[0,6, 2,0, 4,6]]],
    ['W', 5, [[0,6, 1.1,0, 2.5,4, 3.9,0, 5,6]]],
    ['X', 4, [[0,6, 4,0], [0,0, 4,6]]],
    ['Y', 4, [[0,6, 2,3, 4,6], [2,3, 2,0]]],
    ['Z', 4, [[0,6, 4,6, 0,0, 4,0]]],
    ['0', 4, [[1,0, 0,1, 0,5, 1,6, 3,6, 4,5, 4,1, 3,0, 1,0], [0.6,0.8, 3.4,5.2]]],
    ['1', 2, [[0,4.6, 2,6, 2,0]]],
    ['2', 4, [[0,5, 1,6, 3,6, 4,5, 4,4, 0,0, 4,0]]],
    ['3', 4, [[0,6, 4,6, 2,3.6, 3,3.6, 4,2.6, 4,1, 3,0, 0,0]]],
    ['4', 4, [[3,0, 3,6, 0,2, 4,2]]],
    ['5', 4, [[4,6, 0,6, 0,3.4, 3,3.4, 4,2.4, 4,1, 3,0, 0,0]]],
    ['6', 4, [[3.6,6, 1,6, 0,5, 0,1, 1,0, 3,0, 4,1, 4,2.4, 3,3.4, 0,3.4]]],
    ['7', 4, [[0,6, 4,6, 1.2,0]]],
    ['8', 4, [[1,3, 0,4, 0,5, 1,6, 3,6, 4,5, 4,4, 3,3, 1,3, 0,2, 0,1, 1,0, 3,0, 4,1, 4,2, 3,3]]],
    ['9', 4, [[4,2.6, 1,2.6, 0,3.6, 0,5, 1,6, 3,6, 4,5, 4,1, 3,0, 0.4,0]]],
    ['.', 0, [[0,0, 0,0]]],
    [',', 0.6, [[0.6,0.3, 0,-1.2]]],
    ["'", 0.6, [[0.6,6, 0,4.6]]],
    ['-', 2.6, [[0,2.8, 2.6,2.8]]],
    [';', 0.6, [[0.6,3.4, 0.6,3.4], [0.6,0.3, 0,-1.2]]],
    [':', 0, [[0,3.4, 0,3.4], [0,0, 0,0]]],
    ['!', 0, [[0,6, 0,2], [0,0, 0,0]]],
    ['?', 4, [[0,5, 1,6, 3,6, 4,5, 4,4, 2,2.6, 2,1.9], [2,0, 2,0]]]
  ];

  var BY_CHAR = {};
  for (var i = 0; i < GLYPHS.length; i++) BY_CHAR[GLYPHS[i][0]] = GLYPHS[i];

  function find(c) {
    if (c >= 'a' && c <= 'z') c = c.toUpperCase();
    return BY_CHAR[c] || null;
  }

  // ----------------------------------------------------------------- style ----
  var SLANT = 0.22, GAP_UNITS = 2.4, SPACE_UNITS = 6.2, LINE_SPACING = 1.7;
  var STROKE_FRAC = 0.085, MARGIN = 8, SIZE_SCALE = 1.0;

  function mix(a, b, t) {
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  }
  function cl(v) { return v < 0 ? 0 : (v > 255 ? 255 : (v | 0)); }
  function to565(c) {
    return ((cl(c[0]) & 0xF8) << 8) | ((cl(c[1]) & 0xFC) << 3) | (cl(c[2]) >> 3);
  }
  function from565(v) {
    return [(v >> 11) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3];
  }

  // ------------------------------------------------------------- layout ----
  function wordWidth(w, unit) {
    var x = 0;
    for (var i = 0; i < w.length; i++) {
      var g = find(w[i]);
      x += (g ? g[1] : 3) * unit;
      if (i + 1 < w.length) x += GAP_UNITS * unit;
    }
    return x;
  }

  // Largest cap height whose word-wrapped text fits the screen.
  function fit(text, W, H) {
    var words = [], cur = '';
    for (var i = 0; i < text.length; i++) {
      if (text[i] === ' ') { if (cur) words.push(cur); cur = ''; } else cur += text[i];
    }
    if (cur) words.push(cur);

    for (var cap = 64; cap >= 8; cap -= 1) {
      var unit = cap / 6, r = cap * STROKE_FRAC;
      var maxW = W - 2 * MARGIN - cap * SLANT - 2 * r;
      var lines = [], line = '', lw = 0, ok = true;
      for (var k = 0; k < words.length; k++) {
        var w = words[k], ww = wordWidth(w, unit);
        if (ww > maxW) { ok = false; break; }
        if (!line) { line = w; lw = ww; }
        else if (lw + SPACE_UNITS * unit + ww <= maxW) { line += ' ' + w; lw += SPACE_UNITS * unit + ww; }
        else { lines.push(line); line = w; lw = ww; }
      }
      if (!ok) continue;
      if (line) lines.push(line);
      var blockH = lines.length * cap * LINE_SPACING - cap * (LINE_SPACING - 1) + 2 * r + cap * 0.2;
      if (blockH <= H - 2 * MARGIN) return { cap: Math.floor(cap * SIZE_SCALE), lines: lines };
    }
    return { cap: 8, lines: [text] };
  }

  // ---------------------------------------------------------- background ----
  function drawBackground(fb, W, H) {
    var top = [12, 2, 34], bottom = [52, 6, 70], grid = [150, 30, 170];
    var horizon = (H * 62 / 100) | 0;
    for (var y = 0; y < H; y++) {
      var base = mix(top, bottom, y / (H - 1));
      for (var x = 0; x < W; x++) {
        var c = base;
        if (y >= horizon) {                                   // perspective grid on the "floor"
          var depth = (y - horizon + 1) / (H - horizon);       // 0 far .. 1 near
          var z = 1.0 / depth;                                 // pseudo distance
          var hl = Math.abs((z * 1.5) % 1.0 - 0.5);            // horizontal lines
          var cx = (x - W / 2.0) * z / 40.0;                   // converging verticals
          var vl = Math.abs(cx - Math.round(cx));
          var line = Math.max(hl < 0.06 * z ? 1.0 : 0.0, vl < 0.03 * z ? 1.0 : 0.0);
          c = mix(c, grid, line * 0.35 * depth);
        }
        if (y === horizon) c = mix(c, grid, 0.6);
        fb[y * W + x] = to565(c);
      }
    }
    var s = 12345;                                             // a few fixed stars in the sky
    for (var i = 0; i < 40; i++) {
      s = (Math.imul(s, 1103515245) + 12345) >>> 0; var sx = (s >>> 8) % W;
      s = (Math.imul(s, 1103515245) + 12345) >>> 0; var sy = (s >>> 8) % horizon;
      fb[sy * W + sx] = to565(mix(from565(fb[sy * W + sx]), [255, 220, 255], 0.5));
    }
  }

  // -------------------------------------------------------------- render ----
  function render(fb, dist, W, H, text) {
    drawBackground(fb, W, H);
    var L = fit(text, W, H);
    var cap = L.cap, unit = cap / 6, r = cap * STROKE_FRAC;
    var glowR = r + Math.max(3.0, cap * 0.28);
    var lineH = cap * LINE_SPACING;
    var blockH = L.lines.length * lineH - (lineH - cap);
    var firstBaseline = (H - blockH) / 2 + cap;

    for (var i = 0; i < W * H; i++) dist[i] = 1e9;
    var baselines = [];

    for (var li = 0; li < L.lines.length; li++) {
      var line = L.lines[li];
      var baseline = firstBaseline + li * lineH;
      baselines.push(baseline);
      var lw = 0;                                              // measure line
      var ws = line.split(' ');
      for (var k = 0; k < ws.length; k++) lw += wordWidth(ws[k], unit) + (k ? SPACE_UNITS * unit : 0);
      var penX = (W - lw - cap * SLANT) / 2;
      for (var ci = 0; ci < line.length; ci++) {
        var c = line[ci];
        if (c === ' ') { penX += SPACE_UNITS * unit - GAP_UNITS * unit; continue; }
        var g = find(c);
        if (!g) { penX += 3 * unit + GAP_UNITS * unit; continue; }
        var strokes = g[2];
        for (var si = 0; si < strokes.length; si++) {
          var st = strokes[si], n = st.length / 2;
          for (var p = 0; p < n; p++) {
            if (p + 1 >= n && n > 1) break;                    // last point of a polyline
            var q = (p + 1 < n) ? p + 1 : p;                   // dots draw a zero-length segment
            var axg = st[p * 2], ayg = st[p * 2 + 1];
            var bxg = st[q * 2], byg = st[q * 2 + 1];
            // glyph space -> screen (italic shear, y flipped)
            var ax = penX + (axg + ayg * SLANT) * unit, ay = baseline - ayg * unit;
            var bx = penX + (bxg + byg * SLANT) * unit, by = baseline - byg * unit;
            var x0 = Math.floor(Math.min(ax, bx) - glowR), x1 = Math.ceil(Math.max(ax, bx) + glowR);
            var y0 = Math.floor(Math.min(ay, by) - glowR), y1 = Math.ceil(Math.max(ay, by) + glowR);
            if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
            if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
            var dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
            for (var y = y0; y <= y1; y++) {
              for (var x = x0; x <= x1; x++) {
                var px = x + 0.5 - ax, py = y + 0.5 - ay;
                var t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                var ex = px - t * dx, ey = py - t * dy;
                var d = Math.sqrt(ex * ex + ey * ey);
                if (d < dist[y * W + x]) dist[y * W + x] = d;
              }
            }
          }
        }
        penX += g[1] * unit + GAP_UNITS * unit;
      }
    }

    // Shade: neon glow + two-tone sunset chrome fill
    var glowC = [255, 30, 170], edgeC = [90, 0, 90];
    var topA = [120, 220, 255], topB = [255, 90, 220];         // cyan -> pink sky half
    var botA = [255, 70, 110], botB = [255, 215, 90];          // red -> gold sun half
    var shine = [255, 245, 255];
    for (var yy = 0; yy < H; yy++) {
      var bl = baselines.length ? baselines[0] : 0;            // nearest line's baseline
      for (var bi = 0; bi < baselines.length; bi++) {
        var b = baselines[bi];
        if (Math.abs(yy - (b - cap / 2)) < Math.abs(yy - (bl - cap / 2))) bl = b;
      }
      var v = (bl - (yy + 0.5)) / cap;                         // 1 at cap top, 0 at baseline
      var fill;
      if (v > 0.52) fill = mix(topB, topA, Math.min(1, (v - 0.52) / 0.55));
      else if (v > 0.46) fill = shine;
      else fill = mix(botB, botA, Math.min(1, Math.max(0, v) / 0.46));
      for (var xx = 0; xx < W; xx++) {
        var dd = dist[yy * W + xx];
        if (dd > glowR) continue;
        var col = from565(fb[yy * W + xx]);
        var gt = 1 - (dd - r) / (glowR - r);                   // glow falloff outside stroke
        if (dd > r) { fb[yy * W + xx] = to565(mix(col, glowC, 0.75 * gt * gt)); continue; }
        var edge = dd / r;                                     // darker rim for a chrome bevel
        var f = edge > 0.72 ? mix(fill, edgeC, (edge - 0.72) / 0.28 * 0.55) : fill;
        var aa = Math.min(1.0, (r - dd) + 0.5);                // anti-aliased edge
        fb[yy * W + xx] = to565(mix(mix(col, glowC, 0.75), f, aa));
      }
    }
  }

  root.synth = {
    mix: mix, to565: to565, from565: from565,
    drawBackground: drawBackground, render: render, fit: fit
  };
})(window);
