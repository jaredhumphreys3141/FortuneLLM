# Training the fortune model

The sketch ships a character-level GPT with its weights baked into
`../model_weights.h`. This directory is how that header is produced.

The architecture is not a free choice. `../gpt.h` is hand-written C for the
ESP32: it walks flat float arrays by computed offsets and keeps its own
key/value cache, so the dimensions, the layer layout and the order the weights
are written in are all part of the interface between these scripts and that
file.

    vocabulary   54 characters    dim      64     layers  4
    heads        4                context  128    hidden  256
    209,408 parameters, tied output head, no biases on the linear layers

## The corpus

`fortunes.txt` holds one fortune per line and is what `train.py` reads. It is
not checked in, because it is whatever text the model is being trained on this
time; `prepare_corpus.py` produces it from a raw file:

    python3 prepare_corpus.py their_file.txt --out fortunes.txt

That folds curly quotes, dashes and accented letters into the model's character
set, strips list numbering, drops fortunes the sketch would reject, removes
duplicates, and reports anything it could not handle. Characters genuinely
outside the set - a digit, an exclamation mark, a capital `Q` - are dropped
along with their fortunes unless `--extend-vocab` is passed, which keeps them
and grows `GPT_VOCAB` instead.

`fortunes_sample.txt` is a worked example: 2471 fortunes written to fit the
shipped vocabulary exactly. It trains and exports cleanly end to end, but see
the warning below before using it as a model of what a good corpus looks like.

### What actually decides quality

Word diversity, not fortune count. A 209K-parameter character-level model has
to see a word many times before it can spell it reliably. Measured on this
engine, generating 400 fortunes at a time and counting those containing a word
that never appeared in training:

| corpus                        | at temp 0.8 | at temp 1.5 |
| ----------------------------- | ----------- | ----------- |
| 2809 fortunes, 688 words      | 6%          | 45%         |
| 2471 fortunes, 1697 words     | 59%         | 91%         |

Same architecture, same recipe; only the vocabulary changed. The sketch runs at
temperature 1.5. Aim for roughly 700 distinct words and a corpus where every
word appears many times - repetitive phrasing is a feature here, not a flaw.
`prepare_corpus.py` warns past 900 distinct words.

Two things constrain the text itself, and `check_corpus.py` enforces both:

* **The 54-character vocabulary.** It has no digits, no exclamation or question
  marks, no colons or quotation marks, and no capital `Q`, `U`, `V`, `X` or `Z`
  — those simply never occurred in the original training text. Staying inside it
  keeps the new header a drop-in replacement for the old one. Going outside it
  is allowed but not free: nothing hard-codes 54, every consumer reads
  `GPT_VOCAB`, and each character costs only 64 floats, but the parameter count
  changes and `sim/` has to be regenerated.
* **The sketch's acceptance rules.** `rejectReason()` in `../FortuneLLM.ino`
  throws away anything under 12 characters, over 90, or not ending in a full
  stop. Training on fortunes the sketch would reject teaches the model to
  produce output the sketch will reject.

Every one of the 54 characters has to appear somewhere in the corpus, or the
exported vocabulary comes out smaller than the shipped one. That still works -
the sketch reads `GPT_VOCAB` - but it is worth knowing about rather than
discovering after a flash, so both scripts report it.

## Running it

    python3 prepare_corpus.py raw.txt --out fortunes.txt
    python3 check_corpus.py          # before anything else
    python3 train.py                 # writes model.pt, keeps the best validation loss
    python3 export_header.py         # model.pt -> ../model_weights.h
    python3 check_header.py          # lengths, sorting, finiteness
    python3 check_parity.py          # the header computes what the checkpoint does

`train.py` holds out whole fortunes rather than a slice of the text, so the
validation loss measures fortunes the model has not seen; it saves the best
checkpoint to `model.pt` and the last one to `<out>_last.pt`.

**Use `_last.pt`, not `model.pt`, for this model.** Validation loss rewards
generalizing away from the training text, but this model has nothing to
generalize to — its only job is to reproduce a fixed small vocabulary
correctly, and generalizing away from it is exactly what makes it invent
misspelled words. Measured on this engine, generating 400 fortunes and
counting how many contain a word the model never learned to spell, on the same
1286-fortune corpus:

| checkpoint                          | temp 0.8 | temp 1.0 |
| ------------------------------------ | -------- | -------- |
| best validation loss (`model.pt`)    | 76%      | 85%      |
| last checkpoint (`model_last.pt`)    | 38%      | 48%      |
| last checkpoint, `--weight-decay 0`  | 17%      | 23%      |

`--weight-decay 0` removes the other force pushing the model away from
memorizing: AdamW's weight decay, which exists to help a model generalize.
Together, using the last checkpoint and training with `--weight-decay 0` cut
the garbled fraction by roughly 4x over the default settings on this corpus.
The sketch carries `GPT_SEEN` so it can also recognize a fortune it has simply
copied outright, if `SKIP_COPIES` is turned on.

`check_parity.py` is the one that catches the mistakes that matter. A
transposed matrix or a layer written in the wrong order produces a header that
compiles and generates confident nonsense; comparing logits against
`../host/reference.py`, which is written from the architecture rather than from
either the checkpoint or the header, catches it.

## Afterwards

The export rewrites `GPT_SEEN`, `GPT_WORDS` and `GPT_TRIGRAMS` from the corpus
as well as the weights, so all three stay in step with the text the model was
actually trained on. `GPT_TRIGRAMS` backs `hasBadTrigram()` in
`../FortuneLLM.ino`: every run of 3 consecutive words that occurred in the
corpus, hashed, so the sketch can reject a fortune whose words are individually
real but never occurred in that order - the "word salad" a small model
produces once vocabulary alone stops being the limiting factor. It costs
roughly 4 bytes per distinct trigram (about 35 KB on the 1497-fortune corpus).

Two other places carry a copy of the weights and need regenerating:

    cd ../sim && python3 make_sim_data.py     # the browser simulator
    cd ../host && make && ./tinygpt --selftest

`../host/tinygpt` runs the same engine off the board, which is the cheap way to
judge new weights: `--count 40 --temp 1.5 --topk 8` is what the sketch does, and
`--verbose` shows which fortunes were rejected and why.
