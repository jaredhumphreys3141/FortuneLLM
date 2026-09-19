# Screen simulator

The sketch draws its screens with plain C++ that does not touch the Arduino
API: `synthfont.h` and `scene.h` only ever write RGB565 pixels into a
320&times;172 buffer. This folder is a port of that code to the browser, so the
panel can be looked at without flashing a board.

Open `index.html` over HTTP and it boots the way the board does: the live
status screen while the first fortune is written, then fortunes and landscapes
alternating every three seconds, with the BOOT button available to skip ahead.

```
python3 -m http.server        # then open http://localhost:8000/sim/
node verify.js                # check the ports against the C code
python3 make_sim_data.py      # after retraining or regenerating the blocklist
```

Opening `index.html` as a `file://` URL does not work: it fetches the weights,
and browsers refuse cross-origin reads from the filesystem.

## What is in here

| | |
|---|---|
| `index.html` | the page: the board, the controls and the serial monitor |
| `synthfont.js` | port of `synthfont.h` - glyphs, layout, glow, chrome fill |
| `scene.js` | port of `scene.h` - skies, sun, ridges, skylines, grid, palms |
| `tinygpt.js` | port of `gpt.h` and `blocklist.h` - inference and the filter |
| `device.js` | the board: an `Arduino_GFX`-shaped panel, plus `setup()` and `loop()` from `FortuneLLM.ino` |
| `make_sim_data.py` | repacks the headers into `meta.json` and `weights.b64.txt` |
| `verify.js` | checks the ports against `../host/tinygpt` |
| `glcdfont.c` | the Adafruit GFX classic 5&times;7 font, which the boot screen prints through |

`meta.json` and `weights.b64.txt` are generated but checked in, the same way
`model_weights.h` and `blocklist_data.h` are, so a fresh clone works without a
build step. Rerun `make_sim_data.py` when either of those headers changes.

## How close it is

Close enough that a rejection you see here is a rejection the board would make.
The fortunes are written by the real weights, one character at a time, and
screened by the real blocklist and the real acceptance rules. Every pixel is
quantised to RGB565 exactly as `synth::to565` does it, and the boot screen goes
through the same partial-rectangle redraws as `status::restore`, so it flickers
the way the board flickers.

Three things differ, all of them deliberate:

**Speed is a setting, not a measurement.** Nothing here was timed on a board.
The page generates at a configurable ms/char, defaulting to an estimate. The
board prints its real figure over Serial after every fortune; set the slider to
that number and the boot screen runs at your board's pace.

**Arithmetic is double, not float.** `gpt.h` computes in 32-bit floats and
JavaScript has no float type, so the logits agree to about 3e-6 rather than
exactly. That is enough to flip an occasional sample, so a seed will not
reproduce the board's exact wording. `verify.js` pins this down by comparing
greedy output, where no random number is involved and the two agree character
for character.

**The BOOT button goes dead during generation.** That is not a bug in the
simulator. On the board `loop()` is blocked inside `makeFortune()` while the
next fortune is written behind a landscape, and never polls the pin.

## Checking a change to the drawing code

`verify.js` renders a fortune screen, two landscapes and the boot screen to
`shot-*.png` (gitignored), so a change to `synthfont.h` or `scene.h` can be
ported and eyeballed here before it goes near hardware.
