#!/usr/bin/env python3
"""An independent reference implementation of the fortune model.

gpt.h is hand-written C for a microcontroller: flat arrays, manual offsets and
a key/value cache. This file computes the same thing from the architecture
description instead (GPT-2 style, pre-LayerNorm, tanh GELU, no linear biases,
tied output head), in double precision, with the weights read straight out of
model_weights.h. Where the two disagree, one of them has a bug.

It is deliberately plain Python with no dependencies, so it runs anywhere the
board's weights do. That makes it slow - tens of milliseconds a token - which
is fine for checking a handful of steps.

    python3 reference.py --prompt "The"     # logits after each step
    python3 reference.py --generate         # greedy text
"""

import argparse
import math
import os
import re
import sys
from operator import mul

WEIGHTS_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                         "model_weights.h")


# ----------------------------------------------------------------- weights --
def load_weights(path):
    """Reads the #defines, the character table and the float arrays."""
    with open(path, "r") as f:
        text = f.read()

    cfg = {k: int(v) for k, v in re.findall(r"#define\s+(GPT_\w+)\s+(\d+)", text)}

    chars_m = re.search(r"GPT_CHARS\[\w+\]\s*=\s*\{([^}]*)\}", text)
    if not chars_m:
        raise SystemExit("could not find GPT_CHARS in %s" % path)
    chars = [int(v) for v in chars_m.group(1).split(",")]

    arrays = {}
    for name, body in re.findall(r"static const float\s+(\w+)\[\d+\]\s*=\s*\{([^}]*)\}", text):
        arrays[name] = [float(v) for v in body.split(",")]

    missing = [n for n in ("W_wte", "W_wpe", "W_ln1_w", "W_ln1_b", "W_qkv", "W_proj",
                           "W_ln2_w", "W_ln2_b", "W_fc", "W_fc_proj", "W_lnf_w",
                           "W_lnf_b") if n not in arrays]
    if missing:
        raise SystemExit("model_weights.h is missing: %s" % ", ".join(missing))
    return cfg, chars, arrays


def rows(flat, n_rows, n_cols):
    """Splits a flat row-major array into a list of rows."""
    return [flat[r * n_cols:(r + 1) * n_cols] for r in range(n_rows)]


# ------------------------------------------------------------------- model --
class Reference(object):
    def __init__(self, path=WEIGHTS_H):
        cfg, chars, w = load_weights(path)
        self.D = cfg["GPT_DIM"]
        self.L = cfg["GPT_LAYERS"]
        self.H = cfg["GPT_HEADS"]
        self.T = cfg["GPT_CTX"]
        self.V = cfg["GPT_VOCAB"]
        self.HID = cfg["GPT_HIDDEN"]
        self.HS = self.D // self.H
        self.chars = "".join(chr(c) for c in chars)

        D, L, HID, V = self.D, self.L, self.HID, self.V
        self.wte = rows(w["W_wte"], V, D)
        self.wpe = rows(w["W_wpe"], self.T, D)
        # Per-layer weights, each stored as a matrix of rows.
        self.ln1_w = rows(w["W_ln1_w"], L, D)
        self.ln1_b = rows(w["W_ln1_b"], L, D)
        self.ln2_w = rows(w["W_ln2_w"], L, D)
        self.ln2_b = rows(w["W_ln2_b"], L, D)
        self.qkv = [rows(w["W_qkv"][l * 3 * D * D:(l + 1) * 3 * D * D], 3 * D, D)
                    for l in range(L)]
        self.proj = [rows(w["W_proj"][l * D * D:(l + 1) * D * D], D, D) for l in range(L)]
        self.fc = [rows(w["W_fc"][l * HID * D:(l + 1) * HID * D], HID, D) for l in range(L)]
        self.fc_proj = [rows(w["W_fc_proj"][l * D * HID:(l + 1) * D * HID], D, HID)
                        for l in range(L)]
        self.lnf_w = w["W_lnf_w"]
        self.lnf_b = w["W_lnf_b"]
        self.reset()

    def reset(self):
        self.pos = 0
        self.keys = [[] for _ in range(self.L)]    # per layer, per position
        self.values = [[] for _ in range(self.L)]

    # -- pieces -------------------------------------------------------------
    @staticmethod
    def matvec(matrix, vec):
        return [sum(map(mul, row, vec)) for row in matrix]

    @staticmethod
    def layernorm(vec, weight, bias):
        n = len(vec)
        mean = sum(vec) / n
        var = sum((v - mean) ** 2 for v in vec) / n
        inv = 1.0 / math.sqrt(var + 1e-5)
        return [(v - mean) * inv * weight[i] + bias[i] for i, v in enumerate(vec)]

    @staticmethod
    def gelu(u):
        return 0.5 * u * (1.0 + math.tanh(0.7978845608 * (u + 0.044715 * u ** 3)))

    @staticmethod
    def softmax(scores):
        top = max(scores)
        exps = [math.exp(s - top) for s in scores]
        total = sum(exps)
        return [e / total for e in exps]

    # -- one token ----------------------------------------------------------
    def step(self, token):
        """Feeds one token and returns the logits for the next one."""
        if self.pos >= self.T:
            raise RuntimeError("context window of %d tokens is full" % self.T)
        x = [a + b for a, b in zip(self.wte[token], self.wpe[self.pos])]

        for l in range(self.L):
            # attention over everything seen so far, this token included
            h = self.layernorm(x, self.ln1_w[l], self.ln1_b[l])
            qkv = self.matvec(self.qkv[l], h)
            q, k, v = qkv[:self.D], qkv[self.D:2 * self.D], qkv[2 * self.D:]
            self.keys[l].append(k)
            self.values[l].append(v)

            scale = 1.0 / math.sqrt(self.HS)
            attended = []
            for head in range(self.H):
                lo, hi = head * self.HS, (head + 1) * self.HS
                qh = q[lo:hi]
                scores = [sum(map(mul, qh, key[lo:hi])) * scale for key in self.keys[l]]
                weights = self.softmax(scores)
                out = [0.0] * self.HS
                for a, value in zip(weights, self.values[l]):
                    vh = value[lo:hi]
                    for i in range(self.HS):
                        out[i] += a * vh[i]
                attended.extend(out)
            x = [a + b for a, b in zip(x, self.matvec(self.proj[l], attended))]

            # feed-forward
            h = self.layernorm(x, self.ln2_w[l], self.ln2_b[l])
            hidden = [self.gelu(u) for u in self.matvec(self.fc[l], h)]
            x = [a + b for a, b in zip(x, self.matvec(self.fc_proj[l], hidden))]

        self.pos += 1
        return self.matvec(self.wte, self.layernorm(x, self.lnf_w, self.lnf_b))

    # -- convenience --------------------------------------------------------
    def encode(self, text):
        out = []
        for c in text:
            if c not in self.chars:
                raise SystemExit("'%s' is not in the model's vocabulary" % c)
            out.append(self.chars.index(c))
        return out

    def logits_per_step(self, prompt_tokens):
        """Logits after the start token and after each prompt token."""
        self.reset()
        return [self.step(t) for t in [0] + list(prompt_tokens)]

    def generate_greedy(self, prompt_tokens=(), max_gen=110):
        """Greedy text, the deterministic counterpart of the sketch's sampler."""
        self.reset()
        out = ""
        token = 0  # token 0 = newline = start/end marker
        for t in prompt_tokens:
            self.step(token)
            token = t
            out += self.chars[token]
        for _ in range(max_gen):
            logits = self.step(token)
            token = max(range(self.V), key=lambda i: logits[i])
            if token == 0:
                break
            out += self.chars[token]
        return out


# -------------------------------------------------------------------- main --
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompt", default="", help="text to feed before reading the logits")
    ap.add_argument("--generate", action="store_true", help="print greedy text instead of logits")
    ap.add_argument("--max", type=int, default=110, help="character cap for --generate")
    ap.add_argument("--weights", default=WEIGHTS_H, help="path to model_weights.h")
    args = ap.parse_args()

    model = Reference(args.weights)
    tokens = model.encode(args.prompt)
    if args.generate:
        print(model.generate_greedy(tokens, args.max))
        return

    steps = model.logits_per_step(tokens)
    fed = [0] + tokens
    print("# reference logits  vocab=%d steps=%d" % (model.V, len(steps)))
    for i, logits in enumerate(steps):
        print("# step %d token %d" % (i, fed[i]))
        print(" ".join("%.9g" % v for v in logits))


if __name__ == "__main__":
    sys.exit(main())
