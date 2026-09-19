#!/usr/bin/env python3
"""Checks that model.pt and model_weights.h compute the same thing.

export_header.py flattens PyTorch's tensors into the layout gpt.h indexes by
hand: one fused [q | k | v] matrix per layer, the layers concatenated, the
output head tied to the embedding. A transposed matrix or a layer written in
the wrong order still produces a header that compiles and generates plausible
rubbish, so the export is verified rather than trusted: this runs both models
over the same prompts and compares logits.

host/reference.py is a third implementation, written from the architecture
rather than from either of these, and this reuses it so the comparison is
against something independent.

    python3 check_parity.py
    python3 check_parity.py --tol 1e-4
"""

import argparse
import os
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "host"))

import train as trainer          # noqa: E402  (the same GPT class that was trained)
import reference                 # noqa: E402  (host/reference.py, plain-Python model)

PROMPTS = ["", "The", "A good", "Your patience", "Something you", "Be kind", "You will",
           "Fortune favors the", "The quiet work of this month"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(HERE, "model.pt"))
    ap.add_argument("--header", default=os.path.join(ROOT, "model_weights.h"))
    ap.add_argument("--tol", type=float, default=2e-4)
    args = ap.parse_args()

    ck = torch.load(args.model, map_location="cpu", weights_only=False)
    chars = ck["chars"]
    model = trainer.GPT(len(chars))
    model.load_state_dict(ck["model"])
    model.eval()

    ref = reference.Reference(args.header)
    if ref.chars != "".join(chars):
        print("FAIL the header's vocabulary differs from the checkpoint's")
        return 1

    stoi = {c: i for i, c in enumerate(chars)}
    worst, worst_where = 0.0, ""
    for prompt in PROMPTS:
        # Token 0 (newline) starts a fortune, the same as in the sketch.
        ids = [0] + [stoi[c] for c in prompt]

        with torch.no_grad():
            torch_logits = model(torch.tensor([ids]))[0][0, -1].tolist()

        ref.reset()
        for t in ids:
            ref_logits = ref.step(t)

        diff = max(abs(a - b) for a, b in zip(torch_logits, ref_logits))
        rel = diff / max(1e-9, max(abs(v) for v in torch_logits))
        if diff > worst:
            worst, worst_where = diff, prompt or "(empty prompt)"
        # The ranking is what sampling actually uses, so check it separately.
        order_t = sorted(range(len(chars)), key=lambda i: -torch_logits[i])[:5]
        order_r = sorted(range(len(chars)), key=lambda i: -ref_logits[i])[:5]
        ok = "ok" if diff <= args.tol and order_t == order_r else "MISMATCH"
        print("  %-30r max diff %.2e  rel %.2e  top-5 %s  %s"
              % (prompt or "(start)", diff, rel, "same" if order_t == order_r else "DIFFERENT", ok))
        if ok != "ok":
            print("      torch top-5: %r" % "".join(chars[i] for i in order_t).replace("\n", "\\n"))
            print("      header top-5: %r" % "".join(chars[i] for i in order_r).replace("\n", "\\n"))

    print("worst difference %.3e on %r (tolerance %.1e)" % (worst, worst_where, args.tol))
    if worst > args.tol:
        print("FAILED: the header does not match the checkpoint")
        return 1
    print("all prompts match: the export is faithful")
    return 0


if __name__ == "__main__":
    sys.exit(main())
