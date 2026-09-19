#!/usr/bin/env python3
"""Checks the board's inference engine against the Python reference.

Runs ./tinygpt --logits and reference.py over the same prompts, and compares
the logits step by step. The engine works in 32-bit floats with a different
summation order, so small differences are expected; anything larger than the
tolerance means the two implementations really disagree.

    python3 compare.py                 # default prompts
    python3 compare.py --prompt "The"  # one prompt of your own
"""

import argparse
import os
import subprocess
import sys

import reference

HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "tinygpt")

DEFAULT_PROMPTS = [
    "",                      # the start token alone
    "A",                     # one step in
    "You will",              # a common opening
    "Today is a good day",   # long enough to exercise the KV cache
]


def run_binary(args):
    """Returns the binary's stdout. Its timing notes go to stderr, so they stay
    out of the text and the logits being compared."""
    try:
        proc = subprocess.Popen([BINARY] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except OSError:
        raise SystemExit("could not run %s - build it first with 'make'" % BINARY)
    out, err = proc.communicate()
    if proc.returncode != 0:
        raise SystemExit("%s failed:\n%s" % (BINARY, err.decode("utf-8", "replace")))
    return out.decode("utf-8", "replace")


def parse_logits(text):
    """Reads the '# step N token T' / values format both sides print."""
    steps = []
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        steps.append([float(v) for v in line.split()])
    return steps


def compare_prompt(model, prompt, tolerance):
    engine = parse_logits(run_binary(["--logits", "--prompt", prompt]))
    expected = model.logits_per_step(model.encode(prompt))

    label = repr(prompt) if prompt else "(start token only)"
    if len(engine) != len(expected):
        print("  FAIL %s - engine returned %d steps, reference %d"
              % (label, len(engine), len(expected)))
        return False

    worst = 0.0
    worst_step = 0
    ranking_ok = True
    for i, (got, want) in enumerate(zip(engine, expected)):
        for a, b in zip(got, want):
            if abs(a - b) > worst:
                worst, worst_step = abs(a - b), i
        if max(range(len(got)), key=lambda j: got[j]) != \
           max(range(len(want)), key=lambda j: want[j]):
            ranking_ok = False

    ok = worst <= tolerance and ranking_ok
    print("  %s %-24s %d steps, largest logit difference %.2e%s"
          % ("PASS" if ok else "FAIL", label, len(engine), worst,
             "" if ranking_ok else ", and the likeliest token differs"))
    if not ok and worst > tolerance:
        print("       largest difference at step %d, tolerance is %.2e" % (worst_step, tolerance))
    return ok


def compare_greedy(model, prompt):
    """Greedy decoding has no randomness, so the two must produce the same text."""
    engine = run_binary(["--greedy", "--prompt", prompt]).strip()
    expected = model.generate_greedy(model.encode(prompt)).strip()
    ok = engine == expected
    print("  %s greedy text matches" % ("PASS" if ok else "FAIL"))
    print("       engine:    %s" % engine)
    if not ok:
        print("       reference: %s" % expected)
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--prompt", action="append", help="prompt to compare (repeatable)")
    ap.add_argument("--tolerance", type=float, default=2e-3,
                    help="largest acceptable logit difference (default 2e-3)")
    args = ap.parse_args()

    prompts = args.prompt if args.prompt else DEFAULT_PROMPTS
    print("Comparing gpt.h against the Python reference (tolerance %.0e)" % args.tolerance)
    model = reference.Reference()

    ok = True
    for prompt in prompts:
        ok &= compare_prompt(model, prompt, args.tolerance)
    ok &= compare_greedy(model, "")

    print("\n%s" % ("the engine matches the reference" if ok else "COMPARISON FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
