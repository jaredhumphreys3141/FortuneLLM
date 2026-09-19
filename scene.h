// ============================================================================
//  SynthScene - random synthwave landscapes, drawn from scratch every time
//  Sky gradient + stars, striped sun with glow, mountains or a city skyline,
//  neon perspective grid, optional palm trees, CRT scanlines.
//  Renders into a 320x172 RGB565 framebuffer. Plain C++ (previewable on a PC).
//  Requires synthfont.h (for RGB helpers).
// ============================================================================
#pragma once
#include <math.h>
#include <stdint.h>
#include <vector>
#include "synthfont.h"

namespace scene {
using synth::RGB;
using synth::mix;
using synth::to565;
using synth::from565;

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float f() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float range(float a, float b) { return a + (b - a) * f(); }
  int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
};

struct Palette {
  RGB skyTop, skyHorizon, sunTop, sunBottom, far, near, edge, grid, floorTop, floorBottom;
};

static const Palette PALETTES[] = {
  // classic magenta
  {{14,0,40},{210,40,130},{255,235,100},{255,40,150},{90,20,110},{28,0,48},{255,70,210},{255,40,200},{45,0,65},{8,0,22}},
  // miami teal
  {{4,8,38},{40,150,210},{255,210,90},{255,70,130},{30,60,120},{8,16,48},{70,240,255},{70,240,255},{10,22,55},{0,4,18}},
  // orange dusk
  {{40,0,60},{255,110,60},{255,245,160},{255,80,40},{120,30,70},{45,6,40},{255,150,70},{255,120,60},{55,8,50},{12,0,20}},
  // violet night
  {{4,0,14},{110,40,200},{235,130,255},{120,40,255},{50,20,90},{14,4,30},{190,100,255},{180,90,255},{24,6,44},{4,0,12}},
  // hot pink / cyan
  {{10,0,30},{255,60,120},{120,240,255},{255,90,200},{110,20,90},{30,0,45},{90,230,255},{90,230,255},{40,0,55},{6,0,18}},
};
static const int NUM_PALETTES = sizeof(PALETTES) / sizeof(PALETTES[0]);

static inline void blend(uint16_t *fb, int i, RGB c, float a) {
  if (a <= 0) return;
  if (a >= 1) { fb[i] = to565(c); return; }
  fb[i] = to565(mix(from565(fb[i]), c, a));
}

// Midpoint-displacement ridge: heights (pixels above the horizon) for each x.
static std::vector<float> ridge(Rng &rng, int W, float base, float amp, float rough) {
  const int N = 512;
  std::vector<float> h(N + 1);
  h[0] = base + rng.range(-amp, amp) * 0.5f;
  h[N] = base + rng.range(-amp, amp) * 0.5f;
  float a = amp;
  for (int step = N; step > 1; step /= 2) {
    for (int i = step / 2; i < N; i += step)
      h[i] = (h[i - step / 2] + h[i + step / 2]) / 2 + rng.range(-a, a);
    a *= rough;
  }
  std::vector<float> out(W);
  for (int x = 0; x < W; x++) out[x] = fmaxf(0, h[x * N / W]);
  return out;
}

// Round-brush polyline into a distance buffer (min distance to centerline).
static void strokeDist(float *dist, int W, int H, const std::vector<synth::Pt> &pts, float reach,
                       float bias = 0) {
  for (size_t k = 0; k + 1 < pts.size(); k++) {
    float ax = pts[k].x, ay = pts[k].y, bx = pts[k + 1].x, by = pts[k + 1].y;
    int x0 = (int)floorf(fminf(ax, bx) - reach), x1 = (int)ceilf(fmaxf(ax, bx) + reach);
    int y0 = (int)floorf(fminf(ay, by) - reach), y1 = (int)ceilf(fmaxf(ay, by) + reach);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
    float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f - ax, py = y + 0.5f - ay;
        float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float ex = px - t * dx, ey = py - t * dy;
        float d = sqrtf(ex * ex + ey * ey) - bias;
        if (d < dist[y * W + x]) dist[y * W + x] = d;
      }
  }
}

// Draw a polyline whose thickness tapers from biasStart to biasEnd.
static void taperedStroke(float *dist, int W, int H, const std::vector<synth::Pt> &pts,
                          float biasStart, float biasEnd) {
  for (size_t k = 0; k + 1 < pts.size(); k++) {
    float t = (float)k / (pts.size() - 1);
    strokeDist(dist, W, H, {pts[k], pts[k + 1]}, 8, biasStart + (biasEnd - biasStart) * t);
  }
}

// One frond: starts at angle a0 (radians, 0 = horizontal, + = up) and bends
// steadily downward like a leaf under gravity, so upper fronds arch up and
// over, side fronds reach out then droop, and low fronds hang down.
static std::vector<synth::Pt> frond(float x, float y, int dir, float a0, float len, float bend) {
  std::vector<synth::Pt> pts{{x, y}};
  const int N = 14;
  float ang = a0;
  for (int i = 1; i <= N; i++) {
    float step = len / N;
    x += cosf(ang) * step * dir;
    y -= sinf(ang) * step;
    ang -= bend / N;
    if (ang < -1.45f) ang = -1.45f;   // never curl back under itself
    pts.push_back({x, y});
  }
  return pts;
}

static void palmTree(Rng &rng, float *dist, int W, int H, float baseX, float height, int dir) {
  // trunk: gentle curve leaning toward dir, thicker at the base
  std::vector<synth::Pt> trunk;
  float lean = rng.range(0.15f, 0.35f) * dir;
  for (int i = 0; i <= 10; i++) {
    float t = i / 10.0f;
    trunk.push_back({baseX + lean * height * t * t, H + 4 - height * t});
  }
  taperedStroke(dist, W, H, trunk, 2.4f, 1.0f);
  const float tx = trunk.back().x, ty = trunk.back().y;

  // fronds on both sides: a spread of starting angles from steeply up to down
  const float angles[] = {1.25f, 0.8f, 0.4f, 0.05f, -0.35f, -0.8f};
  for (int side = -1; side <= 1; side += 2) {
    for (float base : angles) {
      if (rng.f() < 0.12f) continue;                       // occasional gap
      float a0 = base + rng.range(-0.12f, 0.12f);
      float len = height * rng.range(0.38f, 0.5f) * (base < -0.5f ? 0.75f : 1.0f);
      float bend = rng.range(1.5f, 2.1f) * (base > 1.0f ? 1.3f : 1.0f);
      std::vector<synth::Pt> f = frond(tx, ty, side, a0, len, bend);
      taperedStroke(dist, W, H, f, 1.1f, -0.9f);
      // leaflets hanging from the underside for a feathery edge
      for (size_t i = 3; i + 1 < f.size(); i += 2) {
        float dx = f[i + 1].x - f[i].x, dy = f[i + 1].y - f[i].y;
        float l = sqrtf(dx * dx + dy * dy) + 1e-3f;
        float ll = len * 0.13f * (1.0f - (float)i / f.size() * 0.6f);
        // perpendicular pointing downward, swept toward the tip
        float px = -dy / l, py = dx / l;
        if (py < 0) { px = -px; py = -py; }
        strokeDist(dist, W, H, {f[i], {f[i].x + (px + dx / l * 0.8f) * ll, f[i].y + (py + dy / l * 0.8f) * ll}},
                   5, -1.2f);
      }
    }
  }
  // crown with a few coconuts
  strokeDist(dist, W, H, {{tx - 2, ty + 2}, {tx + 2, ty + 3}}, 6, 1.6f);
}

// Render one random scene. dist: scratch buffer of W*H floats.
static void render(uint16_t *fb, float *dist, int W, int H, uint32_t seed) {
  Rng rng(seed);
  const Palette &P = PALETTES[rng.next() % NUM_PALETTES];
  const int horizon = (int)(H * rng.range(0.56f, 0.68f));

  // ---- sky ----
  for (int y = 0; y < horizon; y++) {
    float t = powf((float)y / horizon, 2.2f);
    uint16_t c = to565(mix(P.skyTop, P.skyHorizon, t));
    for (int x = 0; x < W; x++) fb[y * W + x] = c;
  }
  int stars = rng.irange(30, 90);
  for (int i = 0; i < stars; i++) {
    int x = rng.irange(0, W - 1), y = rng.irange(0, horizon * 2 / 3);
    blend(fb, y * W + x, {255, 240, 255}, rng.range(0.3f, 0.9f));
  }

  // ---- sun with glow and stripes ----
  const float R = rng.range(32, 58);
  const float cx = W * rng.range(0.32f, 0.68f);
  const float cy = horizon - R * rng.range(0.35f, 0.7f);
  const int stripes = rng.irange(4, 7);
  for (int y = 0; y < horizon; y++)
    for (int x = 0; x < W; x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
      int i = y * W + x;
      if (d > R) {  // glow halo
        float g = 1 - (d - R) / (R * 0.9f);
        if (g > 0) blend(fb, i, P.sunBottom, 0.45f * g * g);
        continue;
      }
      float v = (y + 0.5f - (cy - R)) / (2 * R);  // 0 top .. 1 bottom
      if (v > 0.35f) {  // stripe gaps, widening toward the bottom
        float s = (v - 0.35f) / 0.65f;
        float phase = fmodf(s * stripes, 1.0f);
        if (phase < 0.12f + 0.45f * s) continue;
      }
      float aa = fminf(1, R - d + 0.5f);
      blend(fb, i, mix(P.sunTop, P.sunBottom, v), aa);
    }

  // ---- far mountains ----
  std::vector<float> farH = ridge(rng, W, rng.range(18, 34), rng.range(14, 26), 0.55f);
  for (int x = 0; x < W; x++)
    for (int y = horizon - (int)farH[x]; y < horizon; y++)
      if (y >= 0) fb[y * W + x] = to565(mix(P.far, P.skyHorizon, 0.25f * (horizon - y) / (farH[x] + 1)));

  // ---- near layer: mountains or city ----
  if (rng.f() < 0.6f) {
    std::vector<float> nearH = ridge(rng, W, rng.range(6, 16), rng.range(10, 20), 0.6f);
    for (int x = 0; x < W; x++) {
      int top = horizon - (int)nearH[x];
      for (int y = top; y < horizon; y++) if (y >= 0) {
        // faint neon contour lines on the slopes
        bool contour = ((horizon - y) % 5 == 0) && (y > top + 1);
        fb[y * W + x] = to565(contour ? mix(P.near, P.edge, 0.3f) : P.near);
      }
      if (top >= 0 && top < horizon) blend(fb, top * W + x, P.edge, 0.9f);
    }
  } else {
    int x = 0;
    while (x < W) {
      int w = rng.irange(8, 24), h = rng.irange(10, (int)(horizon * 0.55f));
      if (rng.f() < 0.25f) h /= 2;
      for (int yy = horizon - h; yy < horizon; yy++)
        for (int xx = x; xx < x + w && xx < W; xx++) if (yy >= 0) {
          bool edge = (yy == horizon - h) || xx == x;
          bool window = ((yy - (horizon - h)) % 4 == 2) && ((xx - x) % 3 == 1) && rng.f() < 0.35f;
          RGB c = P.near;
          if (edge) c = mix(P.near, P.edge, 0.7f);
          if (window) c = rng.f() < 0.5f ? RGB{255, 220, 120} : P.edge;
          fb[yy * W + xx] = to565(c);
        }
      x += w + rng.irange(0, 3);
    }
  }

  // ---- floor + neon grid ----
  const float cols = rng.range(26, 40), rowsScale = rng.range(1.2f, 1.9f);
  for (int y = horizon; y < H; y++) {
    float depth = (float)(y - horizon + 1) / (H - horizon);   // 0 far .. 1 near
    float z = 1.0f / depth;
    RGB base = mix(P.floorTop, P.floorBottom, depth);
    float hl = fabsf(fmodf(z * rowsScale, 1.0f) - 0.5f);
    for (int x = 0; x < W; x++) {
      float gx = (x - W / 2.0f) * z / cols;
      float vl = fabsf(gx - roundf(gx));
      float h = (0.05f * z < 0.35f && hl < 0.05f * z) ? 1.0f : 0.0f;  // skip rows too dense to see
      float v = vl < fminf(0.035f * z, 0.3f) ? 1.0f : 0.0f;
      float fade = fminf(1.0f, depth * 1.6f);
      fb[y * W + x] = to565(mix(base, P.grid, fmaxf(h, v) * (0.15f + 0.8f * fade) * fade));
    }
  }
  for (int x = 0; x < W; x++) blend(fb, horizon * W + x, P.edge, 0.8f);

  // ---- palm trees (silhouettes with a neon rim) ----
  int palms = rng.irange(0, 2);
  if (palms) {
    for (int i = 0; i < W * H; i++) dist[i] = 1e9f;
    for (int p = 0; p < palms; p++) {
      bool left = (palms == 2) ? p == 0 : rng.f() < 0.5f;
      float bx = left ? W * rng.range(0.03f, 0.2f) : W * rng.range(0.8f, 0.97f);
      palmTree(rng, dist, W, H, bx, rng.range(85, 125), left ? 1 : -1);
    }
    const float r = 2.2f;
    RGB silhouette = {8, 0, 16};
    for (int i = 0; i < W * H; i++) {
      float d = dist[i];
      if (d < r) blend(fb, i, silhouette, fminf(1, r - d + 0.5f));
      else if (d < r + 1.6f) blend(fb, i, P.edge, 0.55f * (1 - (d - r) / 1.6f));
    }
  }

  // ---- CRT scanlines ----
  for (int y = 1; y < H; y += 2)
    for (int x = 0; x < W; x++) {
      RGB c = from565(fb[y * W + x]);
      fb[y * W + x] = to565({c.r * 0.86f, c.g * 0.86f, c.b * 0.86f});
    }
}

}  // namespace scene
