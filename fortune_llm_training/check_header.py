#!/usr/bin/env python3
"""Checks that a freshly exported model_weights.h is the file gpt.h expects.

The sketch trusts this header completely: gpt.h walks the arrays by computed
offsets and the .ino binary-searches the hash tables, so a wrong length or an
unsorted table is a crash or a silent misbehaviour on the board rather than a
compile error. This checks the things the compiler cannot.

    python3 check_header.py [../model_weights.h]
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = os.path.join(HERE, os.pardir, "model_weights.h")

# The shape of the network. gpt.h sizes its per-token scratch buffers and the
# sketch sizes its key/value cache from these, so changing one means changing
# the sketch's memory budget too.
EXPECT = {"GPT_DIM": 64, "GPT_LAYERS": 4, "GPT_HEADS": 4,
          "GPT_CTX": 128, "GPT_HIDDEN": 256}

# GPT_VOCAB is deliberately not in EXPECT. Nothing hard-codes 54: gpt.h, the
# sketch and the simulator all read GPT_VOCAB, and each character costs only
# GPT_DIM floats, so a corpus needing an exclamation mark or a digit may grow
# it. The shipped value is noted so a change is reported rather than silent.
SHIPPED_VOCAB = 54


def sizes(d):
    """The length gpt.h assumes for each array, from the dimensions."""
    D, L, V, T, HID = d["GPT_DIM"], d["GPT_LAYERS"], d["GPT_VOCAB"], d["GPT_CTX"], d["GPT_HIDDEN"]
    return {"W_wte": V * D, "W_wpe": T * D, "W_ln1_w": L * D, "W_ln1_b": L * D,
            "W_qkv": L * 3 * D * D, "W_proj": L * D * D, "W_ln2_w": L * D,
            "W_ln2_b": L * D, "W_fc": L * HID * D, "W_fc_proj": L * D * HID,
            "W_lnf_w": D, "W_lnf_b": D}


def main(path=DEFAULT):
    text = open(path).read()
    fail = []

    dims = {k: int(v) for k, v in re.findall(r"#define\s+(GPT_[A-Z]+)\s+(\d+)", text)}
    for k, want in EXPECT.items():
        if dims.get(k) != want:
            fail.append("%s is %r, gpt.h expects %d" % (k, dims.get(k), want))
    if fail:
        for f in fail:
            print("FAIL %s" % f)
        return 1
    if "GPT_VOCAB" not in dims:
        fail.append("GPT_VOCAB is missing")
        print("FAIL GPT_VOCAB is missing")
        return 1
    if dims["GPT_HIDDEN"] != 4 * dims["GPT_DIM"]:
        fail.append("GPT_HIDDEN should be 4 * GPT_DIM")
    if dims["GPT_DIM"] % dims["GPT_HEADS"]:
        fail.append("GPT_DIM must divide evenly into GPT_HEADS")

    chars = [int(v) for v in re.search(r"GPT_CHARS\[\w+\]\s*=\s*\{([^}]*)\}", text).group(1).split(",")]
    if len(chars) != dims["GPT_VOCAB"]:
        fail.append("GPT_CHARS has %d entries, GPT_VOCAB is %d" % (len(chars), dims["GPT_VOCAB"]))
    if chars != sorted(chars):
        fail.append("GPT_CHARS is not sorted")
    if len(set(chars)) != len(chars):
        fail.append("GPT_CHARS has duplicates")
    if chars and chars[0] != 10:
        fail.append("token 0 must be newline; the sketch uses it as start and end marker")

    want = sizes(dims)
    total = 0
    for name, n in want.items():
        m = re.search(r"static const float\s+%s\[(\d+)\]\s*=\s*\{([^}]*)\}" % name, text)
        if not m:
            fail.append("%s is missing" % name)
            continue
        declared, values = int(m.group(1)), m.group(2).split(",")
        if declared != n:
            fail.append("%s is declared [%d], gpt.h indexes %d" % (name, declared, n))
        if len(values) != n:
            fail.append("%s holds %d values, expected %d" % (name, len(values), n))
        bad = [v for v in values if not re.match(r"^\s*-?(\d|\.|e|E|\+|-)+\s*$", v)]
        if bad:
            fail.append("%s has %d values that are not plain floats" % (name, len(bad)))
        nonfinite = [v for v in values if "nan" in v.lower() or "inf" in v.lower()]
        if nonfinite:
            fail.append("%s contains %d non-finite values" % (name, len(nonfinite)))
        total += len(values)

    for name, count_def in (("GPT_SEEN", "GPT_SEEN_COUNT"), ("GPT_WORDS", "GPT_WORD_COUNT"),
                            ("GPT_TRIGRAMS", "GPT_TRIGRAM_COUNT")):
        n = int(re.search(r"#define\s+%s\s+(\d+)" % count_def, text).group(1))
        vals = [int(v.rstrip("uU")) for v in
                re.search(r"%s\[\w+\]\s*=\s*\{([^}]*)\}" % name, text).group(1).split(",")]
        if len(vals) != n:
            fail.append("%s holds %d hashes, %s says %d" % (name, len(vals), count_def, n))
        # The sketch looks these up with std::binary_search.
        if vals != sorted(vals):
            fail.append("%s is not sorted; the sketch binary-searches it" % name)
        if len(set(vals)) != len(vals):
            fail.append("%s has duplicate hashes" % name)
        if any(v < 0 or v > 0xFFFFFFFF for v in vals):
            fail.append("%s has values outside uint32_t" % name)

    print("%s" % os.path.abspath(path))
    print("  dimensions  %s" % ", ".join("%s=%d" % (k[4:].lower(), dims[k]) for k in
                                         ("GPT_VOCAB", "GPT_DIM", "GPT_LAYERS", "GPT_HEADS",
                                          "GPT_CTX", "GPT_HIDDEN")))
    print("  vocabulary  %r" % "".join(chr(c) for c in chars).replace("\n", "\\n"))
    print("  weights     %d floats (%.0fK parameters)" % (total, total / 1000.0))
    print("  size        %.2f MB" % (os.path.getsize(path) / 1048576.0))
    if dims["GPT_VOCAB"] != SHIPPED_VOCAB:
        print("  NOTE        GPT_VOCAB is %d, not the shipped %d. That is allowed - every"
              % (dims["GPT_VOCAB"], SHIPPED_VOCAB))
        print("              consumer reads GPT_VOCAB - but the parameter count changes,")
        print("              so rerun sim/make_sim_data.py and reflash.")
    for f in fail:
        print("FAIL %s" % f)
    print("%s" % ("FAILED: %d problems" % len(fail) if fail else "all checks passed"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
