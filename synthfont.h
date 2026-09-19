// ============================================================================
//  SynthFont - an original geometric synthwave stroke font + renderer
//  Glyphs are polylines on a 6-unit-tall grid, drawn with round brushes,
//  italic slant, a two-tone "sunset chrome" fill, and a neon glow.
//  Renders into a 320x172 RGB565 framebuffer; plain C++ so it can be
//  previewed on a PC with exactly the same code the ESP32 runs.
// ============================================================================
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

namespace synth {

struct Pt { float x, y; };          // y = 0 baseline, y = 6 cap height
struct Glyph { char c; float width; std::vector<std::vector<Pt>> strokes; };

// ---------------------------------------------------------------- glyphs ----
static const std::vector<Glyph> &glyphs() {
  static const std::vector<Glyph> g = {
    {'A', 4, {{{0,0},{0,4},{2,6},{4,4},{4,0}}, {{0,2.8f},{4,2.8f}}}},
    {'B', 4, {{{0,0},{0,6},{3,6},{4,5},{4,4},{3,3},{0,3}}, {{3,3},{4,2},{4,1},{3,0},{0,0}}}},
    {'C', 4, {{{4,6},{1,6},{0,5},{0,1},{1,0},{4,0}}}},
    {'D', 4, {{{0,0},{0,6},{2.5f,6},{4,4.5f},{4,1.5f},{2.5f,0},{0,0}}}},
    {'E', 4, {{{4,6},{0,6},{0,0},{4,0}}, {{0,3},{3,3}}}},
    {'F', 4, {{{4,6},{0,6},{0,0}}, {{0,3},{3,3}}}},
    {'G', 4, {{{4,5.2f},{3.2f,6},{1,6},{0,5},{0,1},{1,0},{3,0},{4,1},{4,3},{2,3}}}},
    {'H', 4, {{{0,0},{0,6}}, {{4,0},{4,6}}, {{0,3},{4,3}}}},
    {'I', 0, {{{0,0},{0,6}}}},
    {'J', 4, {{{4,6},{4,1},{3,0},{1,0},{0,1}}}},
    {'K', 4, {{{0,0},{0,6}}, {{4,6},{0,2.4f}}, {{1.6f,3.6f},{4,0}}}},
    {'L', 4, {{{0,6},{0,0},{4,0}}}},
    {'M', 5, {{{0,0},{0,6},{2.5f,3},{5,6},{5,0}}}},
    {'N', 4, {{{0,0},{0,6},{4,0},{4,6}}}},
    {'O', 4, {{{1,0},{0,1},{0,5},{1,6},{3,6},{4,5},{4,1},{3,0},{1,0}}}},
    {'P', 4, {{{0,0},{0,6},{3,6},{4,5},{4,4},{3,3},{0,3}}}},
    {'Q', 4, {{{1,0},{0,1},{0,5},{1,6},{3,6},{4,5},{4,1},{3,0},{1,0}}, {{2.6f,1.4f},{4.4f,-0.4f}}}},
    {'R', 4, {{{0,0},{0,6},{3,6},{4,5},{4,4},{3,3},{0,3}}, {{2,3},{4,0}}}},
    {'S', 4, {{{4,5.2f},{3.2f,6},{1,6},{0,5},{0,4},{1,3},{3,3},{4,2},{4,1},{3,0},{1,0},{0,0.8f}}}},
    {'T', 4, {{{0,6},{4,6}}, {{2,6},{2,0}}}},
    {'U', 4, {{{0,6},{0,1},{1,0},{3,0},{4,1},{4,6}}}},
    {'V', 4, {{{0,6},{2,0},{4,6}}}},
    {'W', 5, {{{0,6},{1.1f,0},{2.5f,4},{3.9f,0},{5,6}}}},
    {'X', 4, {{{0,6},{4,0}}, {{0,0},{4,6}}}},
    {'Y', 4, {{{0,6},{2,3},{4,6}}, {{2,3},{2,0}}}},
    {'Z', 4, {{{0,6},{4,6},{0,0},{4,0}}}},
    {'0', 4, {{{1,0},{0,1},{0,5},{1,6},{3,6},{4,5},{4,1},{3,0},{1,0}}, {{0.6f,0.8f},{3.4f,5.2f}}}},
    {'1', 2, {{{0,4.6f},{2,6},{2,0}}}},
    {'2', 4, {{{0,5},{1,6},{3,6},{4,5},{4,4},{0,0},{4,0}}}},
    {'3', 4, {{{0,6},{4,6},{2,3.6f},{3,3.6f},{4,2.6f},{4,1},{3,0},{0,0}}}},
    {'4', 4, {{{3,0},{3,6},{0,2},{4,2}}}},
    {'5', 4, {{{4,6},{0,6},{0,3.4f},{3,3.4f},{4,2.4f},{4,1},{3,0},{0,0}}}},
    {'6', 4, {{{3.6f,6},{1,6},{0,5},{0,1},{1,0},{3,0},{4,1},{4,2.4f},{3,3.4f},{0,3.4f}}}},
    {'7', 4, {{{0,6},{4,6},{1.2f,0}}}},
    {'8', 4, {{{1,3},{0,4},{0,5},{1,6},{3,6},{4,5},{4,4},{3,3},{1,3},{0,2},{0,1},{1,0},{3,0},{4,1},{4,2},{3,3}}}},
    {'9', 4, {{{4,2.6f},{1,2.6f},{0,3.6f},{0,5},{1,6},{3,6},{4,5},{4,1},{3,0},{0.4f,0}}}},
    {'.', 0, {{{0,0},{0,0}}}},
    {',', 0.6f, {{{0.6f,0.3f},{0,-1.2f}}}},
    {'\'', 0.6f, {{{0.6f,6},{0,4.6f}}}},
    {'-', 2.6f, {{{0,2.8f},{2.6f,2.8f}}}},
    {';', 0.6f, {{{0.6f,3.4f},{0.6f,3.4f}}, {{0.6f,0.3f},{0,-1.2f}}}},
    {':', 0, {{{0,3.4f},{0,3.4f}}, {{0,0},{0,0}}}},
    {'!', 0, {{{0,6},{0,2}}, {{0,0},{0,0}}}},
    {'?', 4, {{{0,5},{1,6},{3,6},{4,5},{4,4},{2,2.6f},{2,1.9f}}, {{2,0},{2,0}}}},
  };
  return g;
}

static const Glyph *find(char c) {
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  for (const Glyph &g : glyphs()) if (g.c == c) return &g;
  return nullptr;
}

// ----------------------------------------------------------------- style ----
const float SLANT = 0.22f;      // italic shear (x offset per unit of height)
const float GAP_UNITS = 2.4f;   // space between letters
const float SPACE_UNITS = 6.2f; // word space
const float LINE_SPACING = 1.7f;
const float STROKE_FRAC = 0.085f;  // stroke radius as fraction of cap height
const int MARGIN = 8;
const float SIZE_SCALE = 1.0f;     // < 1 leaves breathing room around the text

struct RGB { float r, g, b; };
static inline RGB mix(RGB a, RGB b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}
static inline uint16_t to565(RGB c) {
  auto cl = [](float v) { return v < 0 ? 0 : (v > 255 ? 255 : (int)v); };
  return ((cl(c.r) & 0xF8) << 8) | ((cl(c.g) & 0xFC) << 3) | (cl(c.b) >> 3);
}
static inline RGB from565(uint16_t v) {
  return {(float)((v >> 11) << 3), (float)(((v >> 5) & 0x3F) << 2), (float)((v & 0x1F) << 3)};
}

// ------------------------------------------------------------- layout ----
struct Layout { float cap; std::vector<std::string> lines; };

static float wordWidth(const std::string &w, float unit) {
  float x = 0;
  for (size_t i = 0; i < w.size(); i++) {
    const Glyph *g = find(w[i]);
    x += (g ? g->width : 3) * unit;
    if (i + 1 < w.size()) x += GAP_UNITS * unit;
  }
  return x;
}

// Largest cap height whose word-wrapped text fits the screen.
static Layout fit(const std::string &text, int W, int H) {
  std::vector<std::string> words;
  std::string cur;
  for (char c : text) { if (c == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); } else cur += c; }
  if (!cur.empty()) words.push_back(cur);

  for (float cap = 64; cap >= 8; cap -= 1) {
    float unit = cap / 6, r = cap * STROKE_FRAC;
    float maxW = W - 2 * MARGIN - cap * SLANT - 2 * r;
    std::vector<std::string> lines;
    std::string line;
    float lw = 0;
    bool ok = true;
    for (auto &w : words) {
      float ww = wordWidth(w, unit);
      if (ww > maxW) { ok = false; break; }
      if (line.empty()) { line = w; lw = ww; }
      else if (lw + SPACE_UNITS * unit + ww <= maxW) { line += " " + w; lw += SPACE_UNITS * unit + ww; }
      else { lines.push_back(line); line = w; lw = ww; }
    }
    if (!ok) continue;
    if (!line.empty()) lines.push_back(line);
    float blockH = lines.size() * cap * LINE_SPACING - cap * (LINE_SPACING - 1) + 2 * r + cap * 0.2f;
    if (blockH <= H - 2 * MARGIN) return {floorf(cap * SIZE_SCALE), lines};
  }
  return {8, {text}};
}

// ---------------------------------------------------------- background ----
static void drawBackground(uint16_t *fb, int W, int H) {
  const RGB top = {12, 2, 34}, bottom = {52, 6, 70}, grid = {150, 30, 170};
  const int horizon = H * 62 / 100;
  for (int y = 0; y < H; y++) {
    RGB base = mix(top, bottom, (float)y / (H - 1));
    for (int x = 0; x < W; x++) {
      RGB c = base;
      if (y >= horizon) {  // perspective grid on the "floor"
        float depth = (float)(y - horizon + 1) / (H - horizon);   // 0 far .. 1 near
        float z = 1.0f / depth;                                   // pseudo distance
        float hl = fabsf(fmodf(z * 1.5f, 1.0f) - 0.5f);           // horizontal lines
        float cx = (x - W / 2.0f) * z / 40.0f;                    // converging verticals
        float vl = fabsf(cx - roundf(cx));
        float line = fmaxf(hl < 0.06f * z ? 1.0f : 0.0f, vl < 0.03f * z ? 1.0f : 0.0f);
        c = mix(c, grid, line * 0.35f * depth);
      }
      if (y == horizon) c = mix(c, grid, 0.6f);
      fb[y * W + x] = to565(c);
    }
  }
  uint32_t s = 12345;  // a few fixed stars in the sky
  for (int i = 0; i < 40; i++) {
    s = s * 1103515245 + 12345; int x = (s >> 8) % W;
    s = s * 1103515245 + 12345; int y = (s >> 8) % horizon;
    fb[y * W + x] = to565(mix(from565(fb[y * W + x]), {255, 220, 255}, 0.5f));
  }
}

// -------------------------------------------------------------- render ----
// dist: scratch buffer of W*H floats (distance to nearest stroke centerline)
static void render(uint16_t *fb, float *dist, int W, int H, const std::string &text) {
  drawBackground(fb, W, H);
  Layout L = fit(text, W, H);
  const float cap = L.cap, unit = cap / 6, r = cap * STROKE_FRAC;
  const float glowR = r + fmaxf(3.0f, cap * 0.28f);
  const float lineH = cap * LINE_SPACING;
  const float blockH = L.lines.size() * lineH - (lineH - cap);
  const float firstBaseline = (H - blockH) / 2 + cap;

  for (int i = 0; i < W * H; i++) dist[i] = 1e9f;
  std::vector<float> baselines;

  for (size_t li = 0; li < L.lines.size(); li++) {
    const std::string &line = L.lines[li];
    float baseline = firstBaseline + li * lineH;
    baselines.push_back(baseline);
    float lw = 0;  // measure line
    {
      std::string w; std::vector<std::string> ws;
      for (char c : line) { if (c == ' ') { ws.push_back(w); w.clear(); } else w += c; }
      ws.push_back(w);
      for (size_t k = 0; k < ws.size(); k++) lw += wordWidth(ws[k], unit) + (k ? SPACE_UNITS * unit : 0);
    }
    float penX = (W - lw - cap * SLANT) / 2;
    for (size_t ci = 0; ci < line.size(); ci++) {
      char c = line[ci];
      if (c == ' ') { penX += SPACE_UNITS * unit - GAP_UNITS * unit; continue; }
      const Glyph *g = find(c);
      if (!g) { penX += 3 * unit + GAP_UNITS * unit; continue; }
      for (auto &st : g->strokes) {
        for (size_t k = 0; k < st.size(); k++) {
          const Pt &a = st[k], &b = st[k + 1 < st.size() ? k + 1 : k];
          if (k + 1 >= st.size() && st.size() > 1) break;
          // glyph space -> screen (italic shear, y flipped)
          float ax = penX + (a.x + a.y * SLANT) * unit, ay = baseline - a.y * unit;
          float bx = penX + (b.x + b.y * SLANT) * unit, by = baseline - b.y * unit;
          int x0 = (int)floorf(fminf(ax, bx) - glowR), x1 = (int)ceilf(fmaxf(ax, bx) + glowR);
          int y0 = (int)floorf(fminf(ay, by) - glowR), y1 = (int)ceilf(fmaxf(ay, by) + glowR);
          if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;
          float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
          for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
              float px = x + 0.5f - ax, py = y + 0.5f - ay;
              float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0;
              t = t < 0 ? 0 : (t > 1 ? 1 : t);
              float ex = px - t * dx, ey = py - t * dy;
              float d = sqrtf(ex * ex + ey * ey);
              if (d < dist[y * W + x]) dist[y * W + x] = d;
            }
        }
      }
      penX += g->width * unit + GAP_UNITS * unit;
    }
  }

  // Shade: neon glow + two-tone sunset chrome fill
  const RGB glowC = {255, 30, 170}, edgeC = {90, 0, 90};
  const RGB topA = {120, 220, 255}, topB = {255, 90, 220};        // cyan -> pink sky half
  const RGB botA = {255, 70, 110}, botB = {255, 215, 90};         // red -> gold sun half
  const RGB shine = {255, 245, 255};
  for (int y = 0; y < H; y++) {
    // nearest line's baseline for the gradient
    float bl = baselines.empty() ? 0 : baselines[0];
    for (float b : baselines) if (fabsf(y - (b - cap / 2)) < fabsf(y - (bl - cap / 2))) bl = b;
    float v = (bl - (y + 0.5f)) / cap;   // 1 at cap top, 0 at baseline
    RGB fill;
    if (v > 0.52f) fill = mix(topB, topA, fminf(1, (v - 0.52f) / 0.55f));
    else if (v > 0.46f) fill = shine;
    else fill = mix(botB, botA, fminf(1, fmaxf(0, v) / 0.46f));
    for (int x = 0; x < W; x++) {
      float d = dist[y * W + x];
      if (d > glowR) continue;
      RGB c = from565(fb[y * W + x]);
      float gt = 1 - (d - r) / (glowR - r);            // glow falloff outside stroke
      if (d > r) { c = mix(c, glowC, 0.75f * gt * gt); fb[y * W + x] = to565(c); continue; }
      float edge = d / r;                              // darker rim for a chrome bevel
      RGB f = edge > 0.72f ? mix(fill, edgeC, (edge - 0.72f) / 0.28f * 0.55f) : fill;
      float aa = fminf(1.0f, (r - d) + 0.5f);           // anti-aliased edge
      c = mix(mix(c, glowC, 0.75f), f, aa);
      fb[y * W + x] = to565(c);
    }
  }
}

}  // namespace synth
