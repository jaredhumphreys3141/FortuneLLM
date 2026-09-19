#!/usr/bin/env python3
"""
Repack the sketch's data for the browser simulator.

`index.html` cannot #include a C header, so this script turns the three headers
the simulator needs into two files it can fetch:

    weights.b64.txt   every weight from model_weights.h, float32 little-endian,
                      base64-encoded, in the order listed in ORDER below
    meta.json         model dimensions, the offset of each array inside the
                      blob, the character vocabulary, the blocked-term hashes
                      from blocklist_data.h, and the 5x7 font from glcdfont.c

Run it after retraining the model or regenerating the blocklist:

    python3 make_sim_data.py

It rewrites both files in place. They are checked in, the same way
model_weights.h and blocklist_data.h are, so the simulator works from a fresh
clone without a build step.
"""

import base64
import json
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The order the weights are concatenated in. index.html reads them back by the
# offsets recorded in meta.json, so this order is free to change - but it has to
# match what is in weights.b64.txt, which means rerunning this script.
ORDER = ["W_wte", "W_wpe", "W_ln1_w", "W_ln1_b", "W_qkv", "W_proj",
         "W_ln2_w", "W_ln2_b", "W_fc", "W_fc_proj", "W_lnf_w", "W_lnf_b"]

DIMS = ["VOCAB", "DIM", "LAYERS", "HEADS", "CTX", "HIDDEN"]


def define(name, text):
    """Value of a #define, as an int."""
    m = re.search(r"#define\s+%s\s+(\d+)" % re.escape(name), text)
    if not m:
        raise SystemExit("error: #define %s not found" % name)
    return int(m.group(1))


def array(name, text):
    """Elements of a `static const T name[N] = {...};` declaration, as strings.

    N may be a number or another #define, which is how GPT_CHARS and
    BLOCK_HASH are declared.
    """
    m = re.search(r"\b%s\s*\[\s*(\w+)\s*\]\s*=\s*\{(.*?)\}\s*;" % re.escape(name),
                  text, re.S)
    if not m:
        raise SystemExit("error: array %s not found" % name)
    size = m.group(1)
    n = int(size) if size.isdigit() else define(size, text)
    vals = [v.strip() for v in m.group(2).split(",") if v.strip()]
    if len(vals) != n:
        raise SystemExit("error: %s declares %d values but holds %d" % (name, n, len(vals)))
    return vals


def main():
    weights_h = open(os.path.join(ROOT, "model_weights.h")).read()
    blocklist_h = open(os.path.join(ROOT, "blocklist_data.h")).read()
    glcdfont_c = open(os.path.join(HERE, "glcdfont.c")).read()

    dims = {k: define("GPT_" + k, weights_h) for k in DIMS}

    blob = bytearray()
    offsets = {}
    total = 0
    for name in ORDER:
        vals = array(name, weights_h)
        blob += struct.pack("<%df" % len(vals), *[float(v) for v in vals])
        offsets[name] = [total, len(vals)]
        total += len(vals)

    chars = [int(v) for v in array("GPT_CHARS", weights_h)]

    block_hashes = [int(v) for v in array("BLOCK_HASH", blocklist_h)]
    block_blob = struct.pack("<%dI" % len(block_hashes), *block_hashes)

    # The Adafruit GFX classic font: 256 glyphs, 5 column bytes each. The boot
    # screen prints through it, so the simulator needs the same bitmaps.
    body = glcdfont_c[glcdfont_c.index("{", glcdfont_c.index("font[]")):]
    body = body[1:body.index("};")]
    font = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]{2}", body)]
    if len(font) != 1280:
        raise SystemExit("error: glcdfont.c holds %d bytes, expected 1280" % len(font))

    meta = {
        "dims": dims,
        "offsets": offsets,
        "total": total,
        "chars": chars,
        "blockCount": define("BLOCK_COUNT", blocklist_h),
        "blockMaxWords": define("BLOCK_MAX_WORDS", blocklist_h),
        "blockHashB64": base64.b64encode(block_blob).decode(),
        "fontB64": base64.b64encode(bytes(font)).decode(),
    }

    with open(os.path.join(HERE, "weights.b64.txt"), "w") as f:
        f.write(base64.b64encode(bytes(blob)).decode())
    with open(os.path.join(HERE, "meta.json"), "w") as f:
        json.dump(meta, f)

    print("weights.b64.txt: %d parameters, %d bytes of float32" % (total, len(blob)))
    print("meta.json:       vocab %d, %d blocked terms, %d font bytes"
          % (dims["VOCAB"], meta["blockCount"], len(font)))


if __name__ == "__main__":
    main()
