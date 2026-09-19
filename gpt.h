// ============================================================================
//  TinyGPT - minimal inference engine for the character-level fortune model
//  Matches the architecture in fortune_llm_training/train.py:
//  GPT-2 style, pre-LayerNorm, tanh GELU, no linear biases, tied output head.
//  Plain C++ so it also compiles on a PC for testing against PyTorch.
// ============================================================================
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "model_weights.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

class TinyGPT {
 public:
  static const int D = GPT_DIM, L = GPT_LAYERS, H = GPT_HEADS, T = GPT_CTX,
                   V = GPT_VOCAB, HID = GPT_HIDDEN, HS = GPT_DIM / GPT_HEADS;

  // Allocates the key/value cache (L*T*D*2 floats, 256 KB for the default model).
  bool begin() {
    size_t bytes = sizeof(float) * L * T * D;
#ifdef ESP_PLATFORM
    // Prefer fast internal RAM, fall back to PSRAM.
    kc = (float *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    vc = (float *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!kc) kc = (float *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!vc) vc = (float *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    kc = (float *)malloc(bytes);
    vc = (float *)malloc(bytes);
#endif
    pos = 0;
    return kc && vc;
  }

  void reset() { pos = 0; }
  int position() const { return pos; }

  // Feed one token, get logits for the next token (valid until next call).
  const float *step(int token) {
    if (pos >= T) pos = T - 1;  // should never happen for fortunes (< 110 chars)
    for (int i = 0; i < D; i++) x[i] = W_wte[token * D + i] + W_wpe[pos * D + i];

    for (int l = 0; l < L; l++) {
      // ---- attention ----
      layernorm(xn, x, W_ln1_w + l * D, W_ln1_b + l * D);
      matvec(qkv, W_qkv + (size_t)l * 3 * D * D, xn, 3 * D, D);
      float *kl = kc + (size_t)l * T * D, *vl = vc + (size_t)l * T * D;
      memcpy(kl + pos * D, qkv + D, D * sizeof(float));
      memcpy(vl + pos * D, qkv + 2 * D, D * sizeof(float));
      const float scale = 1.0f / sqrtf((float)HS);
      for (int h = 0; h < H; h++) {
        const float *q = qkv + h * HS;
        float mx = -1e30f;
        for (int t = 0; t <= pos; t++) {
          const float *k = kl + t * D + h * HS;
          float s = 0;
          for (int i = 0; i < HS; i++) s += q[i] * k[i];
          att[t] = s * scale;
          if (att[t] > mx) mx = att[t];
        }
        float sum = 0;
        for (int t = 0; t <= pos; t++) { att[t] = expf(att[t] - mx); sum += att[t]; }
        float *yh = y + h * HS;
        for (int i = 0; i < HS; i++) yh[i] = 0;
        for (int t = 0; t <= pos; t++) {
          const float a = att[t] / sum;
          const float *v = vl + t * D + h * HS;
          for (int i = 0; i < HS; i++) yh[i] += a * v[i];
        }
      }
      matvec(xn, W_proj + (size_t)l * D * D, y, D, D);
      for (int i = 0; i < D; i++) x[i] += xn[i];

      // ---- MLP ----
      layernorm(xn, x, W_ln2_w + l * D, W_ln2_b + l * D);
      matvec(hid, W_fc + (size_t)l * HID * D, xn, HID, D);
      for (int i = 0; i < HID; i++) {
        float u = hid[i];
        hid[i] = 0.5f * u * (1.0f + tanhf(0.7978845608f * (u + 0.044715f * u * u * u)));
      }
      matvec(xn, W_fc_proj + (size_t)l * D * HID, hid, D, HID);
      for (int i = 0; i < D; i++) x[i] += xn[i];
    }

    layernorm(xn, x, W_lnf_w, W_lnf_b);
    matvec(logits, W_wte, xn, V, D);  // tied output head
    pos++;
    return logits;
  }

  // Sample from the last logits. r must be uniform in [0, 1).
  // temperature: lower = safer, higher = wilder. topK <= 0 disables top-k.
  int sample(float temperature, int topK, float r) const {
    float p[V];
    float mx = -1e30f;
    for (int i = 0; i < V; i++) if (logits[i] > mx) mx = logits[i];
    float cutoff = -1e30f;
    if (topK > 0 && topK < V) {  // find the k-th largest logit
      float tmp[V];
      memcpy(tmp, logits, sizeof(tmp));
      for (int k = 0; k < topK; k++) {
        int best = 0;
        for (int i = 1; i < V; i++) if (tmp[i] > tmp[best]) best = i;
        cutoff = tmp[best];
        tmp[best] = -1e30f;
      }
    }
    float sum = 0;
    for (int i = 0; i < V; i++) {
      p[i] = (logits[i] >= cutoff) ? expf((logits[i] - mx) / temperature) : 0.0f;
      sum += p[i];
    }
    float acc = 0, target = r * sum;
    for (int i = 0; i < V; i++) {
      acc += p[i];
      if (acc > target) return i;
    }
    return V - 1;
  }

 private:
  float *kc = nullptr, *vc = nullptr;
  int pos = 0;
  float x[D], xn[D], qkv[3 * D], y[D], hid[HID], att[T], logits[V];

  static void matvec(float *out, const float *W, const float *in, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
      const float *w = W + (size_t)r * cols;
      float s = 0;
      for (int c = 0; c < cols; c++) s += w[c] * in[c];
      out[r] = s;
    }
  }

  static void layernorm(float *out, const float *in, const float *w, const float *b) {
    float mean = 0, var = 0;
    for (int i = 0; i < D; i++) mean += in[i];
    mean /= D;
    for (int i = 0; i < D; i++) { float d = in[i] - mean; var += d * d; }
    const float inv = 1.0f / sqrtf(var / D + 1e-5f);
    for (int i = 0; i < D; i++) out[i] = (in[i] - mean) * inv * w[i] + b[i];
  }
};
