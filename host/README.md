# Desktop test harness

`gpt.h` is plain C++ so it can run on a PC as well as on the board. This folder
is that PC build: it compiles the inference engine and `model_weights.h` on a
desktop, writes fortunes, checks the engine's behaviour, and compares it against
an independent implementation of the same model. Nothing here is flashed to the
board, and the Arduino IDE ignores this folder, so the sketch is unaffected.

```
make            build ./tinygpt
make test       run the self-test
make compare    check the engine against the Python reference
```

## Writing fortunes

`./tinygpt` uses the same sampler, the same start/end token and the same
defaults as `FortuneLLM.ino` (temperature 0.8, top-k 8), so what it prints is
what the board would say.

```
$ ./tinygpt --count 3 --seed 7
A wise builder learns what others miss.
You will find its a good time to share your ideas.
You will soon meet someone who needs your help this year.
```

Randomness comes from a seeded xorshift rather than `esp_random()`, so a seed
reproduces a run exactly on any machine. `--greedy` drops sampling altogether
and always takes the likeliest token, which is the mode to use when you want a
run that cannot vary. `--prompt` starts a fortune with text of your own, and
`--filter` applies the sketch's acceptance rules (too short, too long,
unfinished, and optionally made-up words or copies of the training set) and
retries, so you can see how often a setting produces something usable.
`./tinygpt --help` lists the rest.

## Self-test

`./tinygpt --selftest` checks the things that would otherwise fail quietly on
the board and look merely like a bad fortune: that the weights are finite and
the model's dimensions divide evenly, that the training-set hashes the sketch
binary-searches really are sorted, that logits stay finite and keep changing as
the context grows, that `reset()` clears the key/value cache so one fortune
cannot bleed into the next, that the context limit is handled rather than
overrun, and that the sampler's output frequencies match the softmax it came
from. It exits non-zero if anything fails.

## Comparing against a reference

There is no PyTorch checkout to compare against here - `train.py` lives in a
separate training repository, and the weights arrive already baked into
`model_weights.h` - so `reference.py` is a second implementation written to the
architecture description rather than to the C code: GPT-2 style, pre-LayerNorm,
tanh GELU, no linear biases, tied output head. It reads the weights straight out
of the header, works in double precision, and builds its matrices differently
from the engine's flat arrays and manual offsets, so an indexing or layout
mistake in either one shows up as a disagreement. It is plain Python with no
dependencies; that makes it slow, which does not matter for a handful of steps.

`make compare` feeds both the same prompts and compares the logits after every
step, plus the greedy text end to end:

```
$ make compare
Comparing gpt.h against the Python reference (tolerance 2e-03)
  PASS (start token only)       1 steps, largest logit difference 2.66e-06
  PASS 'A'                      2 steps, largest logit difference 2.66e-06
  PASS 'You will'               9 steps, largest logit difference 3.67e-06
  PASS 'Today is a good day'    20 steps, largest logit difference 6.40e-06
  PASS greedy text matches
       engine:    You will soon be rewarded for your curiosity before long.
```

Differences of a few parts in a million are the engine's 32-bit floats and its
different summation order; the tolerance is 2e-3, far above that and far below a
real bug. Perturbing one cached value by 2% moves the logits by 1.5e-2 and the
comparison fails, so the check has room to spare in both directions.

If you retrain the model and regenerate `model_weights.h`, run `make test &&
make compare` before flashing: both read the header directly, so they check the
weights you are about to put on the board.

## Benchmarking

`./tinygpt --bench 5000` times the forward pass. Desktop numbers say nothing
about the ESP32's speed, but they make the cost of an architecture change
visible immediately instead of after a flash.
