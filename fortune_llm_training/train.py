#!/usr/bin/env python3
"""Trains the character-level fortune model that ships in model_weights.h.

The architecture is fixed by gpt.h, which is hand-written C for the ESP32 and
indexes flat weight arrays by hand: GPT-2 style, pre-LayerNorm, tanh GELU, no
biases on the linear layers, and an output head tied to the token embedding.
Changing any dimension here means changing gpt.h and the sketch too, so the
defaults below are the shipped ones and are not meant to be tuned casually.

Each fortune is wrapped in a newline on both sides: newline is token 0 and
serves as both the start and the end marker, which is the convention
FortuneLLM.ino uses when it generates.

    python3 train.py                       # train and write model.pt
    python3 train.py --steps 6000          # longer run
    python3 train.py --corpus fortunes.txt

Then export_header.py turns model.pt into model_weights.h.
"""

import argparse
import math
import os
import time

import torch
import torch.nn as nn
import torch.nn.functional as F

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------- architecture ----
# These must match the #defines gpt.h reads out of model_weights.h.
DIM, LAYERS, HEADS, CTX = 64, 4, 4, 128
HIDDEN = 4 * DIM


class CausalSelfAttention(nn.Module):
    def __init__(self):
        super(CausalSelfAttention, self).__init__()
        # One fused projection, laid out [q | k | v] by row, exactly as gpt.h
        # slices it out of W_qkv.
        self.qkv = nn.Linear(DIM, 3 * DIM, bias=False)
        self.proj = nn.Linear(DIM, DIM, bias=False)
        self.register_buffer("mask", torch.tril(torch.ones(CTX, CTX)).view(1, 1, CTX, CTX))

    def forward(self, x):
        B, T, C = x.size()
        q, k, v = self.qkv(x).split(DIM, dim=2)
        hs = DIM // HEADS
        q = q.view(B, T, HEADS, hs).transpose(1, 2)
        k = k.view(B, T, HEADS, hs).transpose(1, 2)
        v = v.view(B, T, HEADS, hs).transpose(1, 2)
        att = (q @ k.transpose(-2, -1)) / math.sqrt(hs)
        att = att.masked_fill(self.mask[:, :, :T, :T] == 0, float("-inf"))
        att = F.softmax(att, dim=-1)
        y = (att @ v).transpose(1, 2).contiguous().view(B, T, C)
        return self.proj(y)


class Block(nn.Module):
    def __init__(self):
        super(Block, self).__init__()
        self.ln1 = nn.LayerNorm(DIM)
        self.attn = CausalSelfAttention()
        self.ln2 = nn.LayerNorm(DIM)
        self.fc = nn.Linear(DIM, HIDDEN, bias=False)
        self.fc_proj = nn.Linear(HIDDEN, DIM, bias=False)

    def forward(self, x):
        x = x + self.attn(self.ln1(x))
        # gelu(..., approximate="tanh") is the same formula gpt.h computes.
        x = x + self.fc_proj(F.gelu(self.fc(self.ln2(x)), approximate="tanh"))
        return x


class GPT(nn.Module):
    def __init__(self, vocab):
        super(GPT, self).__init__()
        self.vocab = vocab
        self.wte = nn.Embedding(vocab, DIM)
        self.wpe = nn.Embedding(CTX, DIM)
        self.blocks = nn.ModuleList([Block() for _ in range(LAYERS)])
        self.lnf = nn.LayerNorm(DIM)
        self.apply(self._init)
        # The residual projections are scaled down at init the way GPT-2 does,
        # so deep residual streams do not blow up early in training.
        for name, p in self.named_parameters():
            if name.endswith("proj.weight"):
                nn.init.normal_(p, mean=0.0, std=0.02 / math.sqrt(2 * LAYERS))

    @staticmethod
    def _init(m):
        if isinstance(m, (nn.Linear, nn.Embedding)):
            nn.init.normal_(m.weight, mean=0.0, std=0.02)

    def forward(self, idx, targets=None):
        B, T = idx.size()
        pos = torch.arange(T, device=idx.device)
        x = self.wte(idx) + self.wpe(pos)
        for b in self.blocks:
            x = b(x)
        x = self.lnf(x)
        logits = x @ self.wte.weight.t()          # tied head, as in gpt.h
        if targets is None:
            return logits, None
        loss = F.cross_entropy(logits.view(-1, self.vocab), targets.reshape(-1),
                               ignore_index=-1)
        return logits, loss


# ------------------------------------------------------------------ data ----
def load_corpus(path):
    """Reads the fortunes and builds the newline-delimited training stream."""
    lines = [l.strip() for l in open(path, encoding="utf-8")]
    lines = [l for l in lines if l]
    # "\n" before and after every fortune: token 0 is the start and end marker.
    text = "\n" + "\n".join(lines) + "\n"
    chars = sorted(set(text))
    return lines, text, chars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default=os.path.join(HERE, "fortunes.txt"))
    ap.add_argument("--out", default=os.path.join(HERE, "model.pt"))
    ap.add_argument("--steps", type=int, default=8000)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--lr", type=float, default=3e-3)
    ap.add_argument("--min-lr", type=float, default=1e-4)
    ap.add_argument("--warmup", type=int, default=200)
    ap.add_argument("--val-frac", type=float, default=0.05)
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--eval-every", type=int, default=250)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    lines, text, chars = load_corpus(args.corpus)
    stoi = {c: i for i, c in enumerate(chars)}
    data = torch.tensor([stoi[c] for c in text], dtype=torch.long)

    # Hold out whole fortunes, not a slice of the stream, so validation loss
    # measures fortunes the model has not seen rather than the tail of the file.
    n_val = max(1, int(len(lines) * args.val_frac))
    g = torch.Generator().manual_seed(args.seed)
    perm = torch.randperm(len(lines), generator=g).tolist()
    val_lines = [lines[i] for i in perm[:n_val]]
    train_lines = [lines[i] for i in perm[n_val:]]
    train_data = torch.tensor([stoi[c] for c in "\n" + "\n".join(train_lines) + "\n"],
                              dtype=torch.long)
    val_data = torch.tensor([stoi[c] for c in "\n" + "\n".join(val_lines) + "\n"],
                            dtype=torch.long)

    print("corpus      %s" % args.corpus)
    print("fortunes    %d (%d train, %d val)" % (len(lines), len(train_lines), len(val_lines)))
    print("characters  %d distinct, %d total" % (len(chars), len(data)))
    print("vocabulary  %r" % "".join(chars).replace("\n", "\\n"))

    model = GPT(len(chars))
    n_params = sum(p.numel() for p in model.parameters())
    print("parameters  %d (%.0fK)" % (n_params, n_params / 1000.0))

    def get_batch(source):
        ix = torch.randint(len(source) - CTX - 1, (args.batch,))
        x = torch.stack([source[i:i + CTX] for i in ix])
        y = torch.stack([source[i + 1:i + 1 + CTX] for i in ix])
        return x, y

    @torch.no_grad()
    def estimate(source, iters=40):
        model.eval()
        total = 0.0
        for _ in range(iters):
            x, y = get_batch(source)
            _, loss = model(x, y)
            total += loss.item()
        model.train()
        return total / iters

    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, betas=(0.9, 0.99),
                            weight_decay=0.1)

    def lr_at(step):
        if step < args.warmup:
            return args.lr * (step + 1) / float(args.warmup)
        t = (step - args.warmup) / float(max(1, args.steps - args.warmup))
        return args.min_lr + 0.5 * (args.lr - args.min_lr) * (1 + math.cos(math.pi * t))

    best = float("inf")
    t0 = time.time()
    for step in range(args.steps):
        for group in opt.param_groups:
            group["lr"] = lr_at(step)
        x, y = get_batch(train_data)
        _, loss = model(x, y)
        opt.zero_grad(set_to_none=True)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()

        if step % args.eval_every == 0 or step == args.steps - 1:
            tr, va = estimate(train_data), estimate(val_data)
            flag = ""
            if va < best:
                best = va
                flag = "  <- best, saved"
                torch.save({"model": model.state_dict(), "chars": chars,
                            "dims": {"VOCAB": len(chars), "DIM": DIM, "LAYERS": LAYERS,
                                     "HEADS": HEADS, "CTX": CTX, "HIDDEN": HIDDEN},
                            "corpus": os.path.basename(args.corpus),
                            "fortunes": len(lines), "step": step, "val_loss": va},
                           args.out)
            print("step %5d  lr %.5f  train %.4f  val %.4f  %5.0fs%s"
                  % (step, lr_at(step), tr, va, time.time() - t0, flag))

    print("best validation loss %.4f, written to %s" % (best, args.out))


if __name__ == "__main__":
    main()
