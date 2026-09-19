#!/usr/bin/env python3
"""Turns a raw file of fortunes into a corpus this model can be trained on.

A file written for people carries things the model cannot use: curly quotes,
accented letters, list numbering, blank lines, fortunes far longer than the
screen. This normalizes what can be normalized, reports what cannot, and writes
a clean one-fortune-per-line file.

Two separate questions come up, and the script answers both explicitly rather
than silently doing something:

CHARACTERS. The shipped model has a 54-character vocabulary. Anything outside
it either gets folded into it (curly quotes to straight, dashes to hyphen,
accented letters to their plain form) or, if it is genuinely new - digits, an
exclamation mark, a capital Q - it is a decision. --extend-vocab keeps it and
grows the vocabulary, which is safe: nothing in the sketch hard-codes 54, every
consumer reads GPT_VOCAB, and each extra character costs 64 floats. Without
that flag the affected fortunes are dropped and listed.

WORD DIVERSITY. This is the one that decides whether the model produces real
words, and it is easy to get wrong. A 209K-parameter character-level model has
to see a word many times to learn to spell it. Measured on this engine: a
corpus of 2809 fortunes over 688 distinct words garbles about 6% of fortunes at
the sketch's temperature of 0.8 and 45% at 1.5; a corpus of 2471 fortunes over
1697 distinct words garbles 59% and 91%. Same architecture, same training
recipe - the vocabulary size is what changed. Aim for roughly 700 distinct
words, and for every word to appear at least a handful of times.

    python3 prepare_corpus.py raw.txt
    python3 prepare_corpus.py raw.txt --out fortunes.txt --extend-vocab
"""

import argparse
import collections
import re
import sys
import unicodedata

# The vocabulary the shipped model_weights.h carries.
BASE_VOCAB = set("\n ',-.;ABCDEFGHIJKLMNOPRSTWYabcdefghijklmnopqrstuvwxyz")

# Characters with an obvious plain-ASCII equivalent. Folding these loses
# nothing a 172x320 screen could show anyway.
FOLD = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'",
    "“": "'", "”": "'", "„": "'", "«": "'", "»": "'",
    "–": "-", "—": "-", "―": "-", "−": "-", "­": "-",
    "…": "...", " ": " ", " ": " ", " ": " ", "\t": " ",
    "•": "", "·": "", "﻿": "",
}

# Leading list markers: "1. ", "12) ", "- ", "* ", bullet.
NUMBERING = re.compile(r"^\s*(?:[-*•]|\(?\d{1,4}[.):]?)\s+")

MIN_CHARS, MAX_CHARS = 12, 90          # rejectReason() in FortuneLLM.ino
ENDINGS = (".", "!", "?")              # what the sketch accepts as finished


def normalize(line):
    """Folds a raw line toward the model's character set. Returns the line."""
    line = line.replace("\r", "")
    for a, b in FOLD.items():
        line = line.replace(a, b)
    # Accented letters to their base letter; anything with no decomposition is
    # left alone so it shows up in the report rather than vanishing.
    out = []
    for c in line:
        if c in BASE_VOCAB or c.isascii():
            out.append(c)
            continue
        stripped = "".join(d for d in unicodedata.normalize("NFD", c)
                           if not unicodedata.combining(d))
        out.append(stripped if stripped else c)
    line = "".join(out)
    line = NUMBERING.sub("", line)
    line = line.strip().strip('"').strip()
    line = re.sub(r"\s+", " ", line)
    return line


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", help="the file of fortunes to clean up")
    ap.add_argument("--out", default="fortunes.txt")
    ap.add_argument("--extend-vocab", action="store_true",
                    help="keep characters outside the 54, growing GPT_VOCAB")
    ap.add_argument("--keep-long", action="store_true",
                    help="keep fortunes over %d characters (the sketch rejects them)" % MAX_CHARS)
    args = ap.parse_args()

    raw = open(args.raw, encoding="utf-8", errors="replace").read().splitlines()
    kept, dropped = [], collections.Counter()
    examples = {}
    outside = collections.Counter()
    changed = 0

    for line in raw:
        clean = normalize(line)
        if not clean:
            dropped["blank"] += 1
            continue
        if clean != line.strip():
            changed += 1

        extra = set(clean) - BASE_VOCAB
        if extra:
            for c in extra:
                outside[c] += 1
            if not args.extend_vocab:
                dropped["character outside the vocabulary"] += 1
                examples.setdefault("character outside the vocabulary", clean)
                continue
        if len(clean) < MIN_CHARS:
            dropped["shorter than %d characters" % MIN_CHARS] += 1
            examples.setdefault("shorter than %d characters" % MIN_CHARS, clean)
            continue
        if len(clean) > MAX_CHARS and not args.keep_long:
            dropped["longer than %d characters" % MAX_CHARS] += 1
            examples.setdefault("longer than %d characters" % MAX_CHARS, clean)
            continue
        if not clean.endswith(ENDINGS):
            clean += "."          # a missing full stop is worth fixing, not dropping
            changed += 1
        kept.append(clean)

    seen, unique = set(), []
    for line in kept:
        if line not in seen:
            seen.add(line)
            unique.append(line)
    n_dupes = len(kept) - len(unique)

    words = collections.Counter()
    for line in unique:
        w = ""
        for c in line + " ":
            if c.isalpha() or c == "'":
                w += c.lower()
            elif w:
                words[w] += 1
                w = ""

    vocab = sorted(set("".join(unique)) | {"\n"})
    print("read        %d lines from %s" % (len(raw), args.raw))
    print("normalized  %d lines changed by folding or punctuation" % changed)
    for reason, n in dropped.most_common():
        print("dropped     %-36s %d%s" % (reason, n,
              ("   e.g. %r" % examples[reason][:60]) if reason in examples else ""))
    print("duplicates  %d removed" % n_dupes)
    print("kept        %d fortunes, %d characters" % (len(unique), sum(len(l) + 1 for l in unique)))
    if unique:
        print("length      min %d, mean %.1f, max %d" % (
            min(len(l) for l in unique), sum(map(len, unique)) / float(len(unique)),
            max(len(l) for l in unique)))
    print("vocabulary  %d characters: %r" % (len(vocab), "".join(vocab).replace("\n", "\\n")))
    if outside:
        kind = "kept (GPT_VOCAB grows)" if args.extend_vocab else "dropped, rerun with --extend-vocab to keep"
        print("outside the shipped 54, %s:" % kind)
        for c, n in outside.most_common(20):
            print("    %-8r %d occurrences" % (c, n))

    once = sum(1 for w, n in words.items() if n == 1)
    print("words       %d distinct, %d appear only once (%.0f%%)"
          % (len(words), once, 100.0 * once / max(1, len(words))))
    if len(words) > 900:
        print("WARNING     %d distinct words is a lot for a 209K-parameter model." % len(words))
        print("            Measured on this engine, 688 words garbled 6% of fortunes at")
        print("            temperature 0.8 and 1697 words garbled 59%. Either cut the")
        print("            vocabulary toward ~700 words or expect garbled output.")
    if unique:
        missing = sorted(BASE_VOCAB - set("".join(unique)) - {"\n"})
        if missing:
            print("NOTE        these shipped characters never occur, so GPT_VOCAB will")
            print("            shrink below 54: %r" % "".join(missing))

    with open(args.out, "w", encoding="utf-8") as f:
        for line in unique:
            f.write(line + "\n")
    print("wrote       %s" % args.out)
    return 0 if unique else 1


if __name__ == "__main__":
    sys.exit(main())
