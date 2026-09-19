#!/usr/bin/env python3
"""Checks fortunes.txt against what the board can actually represent and show.

The vocabulary baked into model_weights.h has 54 characters and no others, and
the sketch throws away any fortune shorter than 12 or longer than 90 characters
or not ending in a full stop. A corpus that breaks those rules trains a model
whose output the sketch would reject, so this runs before training, not after.

    python3 check_corpus.py [fortunes.txt]
"""
import collections
import sys

# The exact character table in model_weights.h, as code points.
VOCAB = [10, 32, 39, 44, 45, 46, 59] + \
        [ord(c) for c in "ABCDEFGHIJKLMNOPRSTWY"] + \
        [ord(c) for c in "abcdefghijklmnopqrstuvwxyz"]
ALLOWED = set(chr(c) for c in VOCAB)
MIN_CHARS, MAX_CHARS = 12, 90   # rejectReason() in FortuneLLM.ino


def main(path="fortunes.txt"):
    lines = [l.rstrip("\n") for l in open(path, encoding="utf-8")]
    lines = [l for l in lines if l.strip()]
    problems = []
    bad_chars = collections.Counter()

    for n, line in enumerate(lines, 1):
        if line != line.strip():
            problems.append("%d: leading or trailing space: %r" % (n, line))
        for c in line:
            if c not in ALLOWED:
                bad_chars[c] += 1
                problems.append("%d: character %r is outside the vocabulary" % (n, c))
        if len(line) < MIN_CHARS:
            problems.append("%d: %d chars, the sketch rejects under %d" % (n, len(line), MIN_CHARS))
        if len(line) > MAX_CHARS:
            problems.append("%d: %d chars, the sketch rejects over %d" % (n, len(line), MAX_CHARS))
        if not line.endswith("."):
            problems.append("%d: does not end in a full stop: %r" % (n, line))

    dupes = [t for t, c in collections.Counter(lines).items() if c > 1]
    words = set()
    for line in lines:
        w = ""
        for c in line + " ":
            if c.isalpha() or c == "'":
                w += c.lower()
            elif w:
                words.add(w)
                w = ""

    used = set("".join(lines)) | {"\n"}
    unused = sorted(ALLOWED - used)

    print("fortunes      %d" % len(lines))
    print("duplicates    %d" % len(dupes))
    print("characters    %d of %d used" % (len(used), len(ALLOWED)))
    print("distinct words %d" % len(words))
    print("total chars   %d" % sum(len(l) + 1 for l in lines))
    print("length        min %d, mean %.1f, max %d" % (
        min(len(l) for l in lines), sum(len(l) for l in lines) / float(len(lines)),
        max(len(l) for l in lines)))
    if unused:
        print("UNUSED vocabulary characters: %r" % ("".join(unused),))
    if bad_chars:
        print("OUT-OF-VOCABULARY characters: %r" % dict(bad_chars))
    for d in dupes[:10]:
        print("duplicate: %r" % d)
    for p in problems[:40]:
        print("problem: %s" % p)
    if len(problems) > 40:
        print("... and %d more problems" % (len(problems) - 40))
    return 1 if problems or dupes else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
