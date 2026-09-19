// Port of scene.h - random synthwave landscapes, drawn from scratch every time.
(function (root) {
  'use strict';
  var mix = root.synth.mix, to565 = root.synth.to565, from565 = root.synth.from565;

  function Rng(seed) { this.s = (seed >>> 0) || 1; }
  Rng.prototype.next = function () {
    var s = this.s;
    s = (s ^ (s << 13)) >>> 0;
    s = (s ^ (s >>> 17)) >>> 0;
    s = (s ^ (s << 5)) >>> 0;
    this.s = s;
    return s;
  };
  Rng.prototype.f = function () { return (this.next() >>> 8) * (1.0 / 16777216.0); };
  Rng.prototype.range = function (a, b) { return a + (b - a) * this.f(); };
  Rng.prototype.irange = function (a, b) { return a + (this.next() % ((b - a + 1) >>> 0)); };

  // skyTop, skyHorizon, sunTop, sunBottom, far, near, edge, grid, floorTop, floorBottom
  var PALETTES = [
    // classic magenta
    [[14,0,40],[210,40,130],[255,235,100],[255,40,150],[90,20,110],[28,0,48],[255,70,210],[255,40,200],[45,0,65],[8,0,22]],
    // miami teal
    [[4,8,38],[40,150,210],[255,210,90],[255,70,130],[30,60,120],[8,16,48],[70,240,255],[70,240,255],[10,22,55],[0,4,18]],
    // orange dusk
    [[40,0,60],[255,110,60],[255,245,160],[255,80,40],[120,30,70],[45,6,40],[255,150,70],[255,120,60],[55,8,50],[12,0,20]],
    // violet night
    [[4,0,14],[110,40,200],[235,130,255],[120,40,255],[50,20,90],[14,4,30],[190,100,255],[180,90,255],[24,6,44],[4,0,12]],
    // hot pink / cyan
    [[10,0,30],[255,60,120],[120,240,255],[255,90,200],[110,20,90],[30,0,45],[90,230,255],[90,230,255],[40,0,55],[6,0,18]]
  ];
  var PALETTE_NAMES = ['classic magenta', 'miami teal', 'orange dusk', 'violet night', 'hot pink / cyan'];
  var SKY_TOP = 0, SKY_HORIZON = 1, SUN_TOP = 2, SUN_BOTTOM = 3, FAR = 4,
      NEAR = 5, EDGE = 6, GRID = 7, FLOOR_TOP = 8, FLOOR_BOTTOM = 9;

  function blend(fb, i, c, a) {
    if (a <= 0) return;
    if (a >= 1) { fb[i] = to565(c); return; }
    fb[i] = to565(mix(from565(fb[i]), c, a));
  }

  // Midpoint-displacement ridge: heights (pixels above the horizon) for each x.
  function ridge(rng, W, base, amp, rough) {
    var N = 512;
    var h = new Float64Array(N + 1);
    h[0] = base + rng.range(-amp, amp) * 0.5;
    h[N] = base + rng.range(-amp, amp) * 0.5;
    var a = amp;
    for (var step = N; step > 1; step = (step / 2) | 0) {
      for (var i = (step / 2) | 0; i < N; i += step)
        h[i] = (h[i - (step / 2 | 0)] + h[i + (step / 2 | 0)]) / 2 + rng.range(-a, a);
      a *= rough;
    }
    var out = new Float64Array(W);
    for (var x = 0; x < W; x++) out[x] = Math.max(0, h[(x * N / W) | 0]);
    return out;
  }

  // Round-brush polyline into a distance buffer (min distance to centerline).
  function strokeDist(dist, W, H, pts, reach, bias) {
    bias = bias || 0;
    for (var k = 0; k + 1 < pts.length; k++) {
      var ax = pts[k][0], ay = pts[k][1], bx = pts[k + 1][0], by = pts[k + 1][1];
      var x0 = Math.floor(Math.min(ax, bx) - reach), x1 = Math.ceil(Math.max(ax, bx) + reach);
      var y0 = Math.floor(Math.min(ay, by) - reach), y1 = Math.ceil(Math.max(ay, by) + reach);
      if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
      if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
      var dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
      for (var y = y0; y <= y1; y++) {
        for (var x = x0; x <= x1; x++) {
          var px = x + 0.5 - ax, py = y + 0.5 - ay;
          var t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
          t = t < 0 ? 0 : (t > 1 ? 1 : t);
          var ex = px - t * dx, ey = py - t * dy;
          var d = Math.sqrt(ex * ex + ey * ey) - bias;
          if (d < dist[y * W + x]) dist[y * W + x] = d;
        }
      }
    }
  }

  // Draw a polyline whose thickness tapers from biasStart to biasEnd.
  function taperedStroke(dist, W, H, pts, biasStart, biasEnd) {
    for (var k = 0; k + 1 < pts.length; k++) {
      var t = k / (pts.length - 1);
      strokeDist(dist, W, H, [pts[k], pts[k + 1]], 8, biasStart + (biasEnd - biasStart) * t);
    }
  }

  // One frond: starts at angle a0 and bends steadily downward like a leaf under
  // gravity, so upper fronds arch over and low fronds hang down.
  function frond(x, y, dir, a0, len, bend) {
    var pts = [[x, y]];
    var N = 14, ang = a0;
    for (var i = 1; i <= N; i++) {
      var step = len / N;
      x += Math.cos(ang) * step * dir;
      y -= Math.sin(ang) * step;
      ang -= bend / N;
      if (ang < -1.45) ang = -1.45;              // never curl back under itself
      pts.push([x, y]);
    }
    return pts;
  }

  function palmTree(rng, dist, W, H, baseX, height, dir) {
    // trunk: gentle curve leaning toward dir, thicker at the base
    var trunk = [];
    var lean = rng.range(0.15, 0.35) * dir;
    for (var i = 0; i <= 10; i++) {
      var t = i / 10.0;
      trunk.push([baseX + lean * height * t * t, H + 4 - height * t]);
    }
    taperedStroke(dist, W, H, trunk, 2.4, 1.0);
    var tx = trunk[trunk.length - 1][0], ty = trunk[trunk.length - 1][1];

    // fronds on both sides: a spread of starting angles from steeply up to down
    var angles = [1.25, 0.8, 0.4, 0.05, -0.35, -0.8];
    for (var side = -1; side <= 1; side += 2) {
      for (var ai = 0; ai < angles.length; ai++) {
        var base = angles[ai];
        if (rng.f() < 0.12) continue;            // occasional gap
        var a0 = base + rng.range(-0.12, 0.12);
        var len = height * rng.range(0.38, 0.5) * (base < -0.5 ? 0.75 : 1.0);
        var bend = rng.range(1.5, 2.1) * (base > 1.0 ? 1.3 : 1.0);
        var f = frond(tx, ty, side, a0, len, bend);
        taperedStroke(dist, W, H, f, 1.1, -0.9);
        // leaflets hanging from the underside for a feathery edge
        for (var i2 = 3; i2 + 1 < f.length; i2 += 2) {
          var dx = f[i2 + 1][0] - f[i2][0], dy = f[i2 + 1][1] - f[i2][1];
          var l = Math.sqrt(dx * dx + dy * dy) + 1e-3;
          var ll = len * 0.13 * (1.0 - i2 / f.length * 0.6);
          // perpendicular pointing downward, swept toward the tip
          var px = -dy / l, py = dx / l;
          if (py < 0) { px = -px; py = -py; }
          strokeDist(dist, W, H,
            [f[i2], [f[i2][0] + (px + dx / l * 0.8) * ll, f[i2][1] + (py + dy / l * 0.8) * ll]],
            5, -1.2);
        }
      }
    }
    // crown with a few coconuts
    strokeDist(dist, W, H, [[tx - 2, ty + 2], [tx + 2, ty + 3]], 6, 1.6);
  }

  // Render one random scene. dist: scratch buffer of W*H floats.
  // Returns a short description of what was drawn, for the serial log.
  function render(fb, dist, W, H, seed) {
    var rng = new Rng(seed);
    var pi = rng.next() % PALETTES.length;
    var P = PALETTES[pi];
    var horizon = (H * rng.range(0.56, 0.68)) | 0;

    // ---- sky ----
    for (var y = 0; y < horizon; y++) {
      var t = Math.pow(y / horizon, 2.2);
      var c = to565(mix(P[SKY_TOP], P[SKY_HORIZON], t));
      for (var x = 0; x < W; x++) fb[y * W + x] = c;
    }
    var stars = rng.irange(30, 90);
    for (var i = 0; i < stars; i++) {
      var sx = rng.irange(0, W - 1), sy = rng.irange(0, (horizon * 2 / 3) | 0);
      blend(fb, sy * W + sx, [255, 240, 255], rng.range(0.3, 0.9));
    }

    // ---- sun with glow and stripes ----
    var R = rng.range(32, 58);
    var cx = W * rng.range(0.32, 0.68);
    var cy = horizon - R * rng.range(0.35, 0.7);
    var stripes = rng.irange(4, 7);
    for (var y2 = 0; y2 < horizon; y2++) {
      for (var x2 = 0; x2 < W; x2++) {
        var dx = x2 + 0.5 - cx, dy = y2 + 0.5 - cy, d = Math.sqrt(dx * dx + dy * dy);
        var idx = y2 * W + x2;
        if (d > R) {                                      // glow halo
          var g = 1 - (d - R) / (R * 0.9);
          if (g > 0) blend(fb, idx, P[SUN_BOTTOM], 0.45 * g * g);
          continue;
        }
        var v = (y2 + 0.5 - (cy - R)) / (2 * R);          // 0 top .. 1 bottom
        if (v > 0.35) {                                   // stripe gaps, widening downward
          var s = (v - 0.35) / 0.65;
          var phase = (s * stripes) % 1.0;
          if (phase < 0.12 + 0.45 * s) continue;
        }
        var aa = Math.min(1, R - d + 0.5);
        blend(fb, idx, mix(P[SUN_TOP], P[SUN_BOTTOM], v), aa);
      }
    }

    // ---- far mountains ----
    var farH = ridge(rng, W, rng.range(18, 34), rng.range(14, 26), 0.55);
    for (var x3 = 0; x3 < W; x3++)
      for (var y3 = horizon - (farH[x3] | 0); y3 < horizon; y3++)
        if (y3 >= 0)
          fb[y3 * W + x3] = to565(mix(P[FAR], P[SKY_HORIZON], 0.25 * (horizon - y3) / (farH[x3] + 1)));

    // ---- near layer: mountains or city ----
    var nearKind;
    if (rng.f() < 0.6) {
      nearKind = 'mountains';
      var nearH = ridge(rng, W, rng.range(6, 16), rng.range(10, 20), 0.6);
      for (var x4 = 0; x4 < W; x4++) {
        var top = horizon - (nearH[x4] | 0);
        for (var y4 = top; y4 < horizon; y4++) if (y4 >= 0) {
          // faint neon contour lines on the slopes
          var contour = ((horizon - y4) % 5 === 0) && (y4 > top + 1);
          fb[y4 * W + x4] = to565(contour ? mix(P[NEAR], P[EDGE], 0.3) : P[NEAR]);
        }
        if (top >= 0 && top < horizon) blend(fb, top * W + x4, P[EDGE], 0.9);
      }
    } else {
      nearKind = 'city skyline';
      var x5 = 0;
      while (x5 < W) {
        var w = rng.irange(8, 24), h = rng.irange(10, (horizon * 0.55) | 0);
        if (rng.f() < 0.25) h = (h / 2) | 0;
        for (var yy = horizon - h; yy < horizon; yy++)
          for (var xx = x5; xx < x5 + w && xx < W; xx++) if (yy >= 0) {
            var edge = (yy === horizon - h) || xx === x5;
            var window_ = ((yy - (horizon - h)) % 4 === 2) && ((xx - x5) % 3 === 1) && rng.f() < 0.35;
            var col = P[NEAR];
            if (edge) col = mix(P[NEAR], P[EDGE], 0.7);
            if (window_) col = rng.f() < 0.5 ? [255, 220, 120] : P[EDGE];
            fb[yy * W + xx] = to565(col);
          }
        x5 += w + rng.irange(0, 3);
      }
    }

    // ---- floor + neon grid ----
    var cols = rng.range(26, 40), rowsScale = rng.range(1.2, 1.9);
    for (var y6 = horizon; y6 < H; y6++) {
      var depth = (y6 - horizon + 1) / (H - horizon);         // 0 far .. 1 near
      var z = 1.0 / depth;
      var base = mix(P[FLOOR_TOP], P[FLOOR_BOTTOM], depth);
      var hl = Math.abs((z * rowsScale) % 1.0 - 0.5);
      for (var x6 = 0; x6 < W; x6++) {
        var gx = (x6 - W / 2.0) * z / cols;
        var vl = Math.abs(gx - Math.round(gx));
        var hLine = (0.05 * z < 0.35 && hl < 0.05 * z) ? 1.0 : 0.0;  // skip rows too dense to see
        var vLine = vl < Math.min(0.035 * z, 0.3) ? 1.0 : 0.0;
        var fade = Math.min(1.0, depth * 1.6);
        fb[y6 * W + x6] = to565(mix(base, P[GRID], Math.max(hLine, vLine) * (0.15 + 0.8 * fade) * fade));
      }
    }
    for (var x7 = 0; x7 < W; x7++) blend(fb, horizon * W + x7, P[EDGE], 0.8);

    // ---- palm trees (silhouettes with a neon rim) ----
    var palms = rng.irange(0, 2);
    if (palms) {
      for (var i3 = 0; i3 < W * H; i3++) dist[i3] = 1e9;
      for (var p = 0; p < palms; p++) {
        var left = (palms === 2) ? p === 0 : rng.f() < 0.5;
        var bx = left ? W * rng.range(0.03, 0.2) : W * rng.range(0.8, 0.97);
        palmTree(rng, dist, W, H, bx, rng.range(85, 125), left ? 1 : -1);
      }
      var r = 2.2, silhouette = [8, 0, 16];
      for (var i4 = 0; i4 < W * H; i4++) {
        var dd = dist[i4];
        if (dd < r) blend(fb, i4, silhouette, Math.min(1, r - dd + 0.5));
        else if (dd < r + 1.6) blend(fb, i4, P[EDGE], 0.55 * (1 - (dd - r) / 1.6));
      }
    }

    // ---- CRT scanlines ----
    for (var y8 = 1; y8 < H; y8 += 2)
      for (var x8 = 0; x8 < W; x8++) {
        var cc = from565(fb[y8 * W + x8]);
        fb[y8 * W + x8] = to565([cc[0] * 0.86, cc[1] * 0.86, cc[2] * 0.86]);
      }

    return { palette: PALETTE_NAMES[pi], near: nearKind, palms: palms, horizon: horizon };
  }

  root.scene = { render: render, Rng: Rng, PALETTE_NAMES: PALETTE_NAMES };
})(window);
