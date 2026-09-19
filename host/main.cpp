// ============================================================================
//  Desktop test harness for the fortune model.
//  Compiles gpt.h and model_weights.h off the board so the inference engine
//  can be run, checked and benchmarked on a PC without flashing hardware.
//
//    make && ./tinygpt                 # write a few fortunes
//    ./tinygpt --selftest              # consistency checks on the engine
//    ./tinygpt --logits --prompt "The" # logits per step, for reference checks
//
//  The sampling, the acceptance rules and the start/end token convention
//  mirror FortuneLLM.ino, so what comes out here is what the board would say.
// ============================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "gpt.h"

// ------------------------------------------------------------- Settings ----
// Defaults match the sketch, so a plain run reproduces the board's behaviour.
static float g_temperature = 0.8f;
static int   g_topK        = 8;
static int   g_maxGen      = 110;
static int   g_maxChars    = 90;
static int   g_maxTries    = 6;
static bool  g_skipCopies  = false;
static bool  g_skipMadeUp  = false;

// --------------------------------------------------------------- Random ----
// xorshift32 rather than esp_random(), so a seed reproduces a run exactly on
// any machine. The scaling to [0, 1) is the sketch's rndFloat().
static uint32_t g_rng = 1;
static uint32_t xorshift32() {
  uint32_t x = g_rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g_rng = x;
  return x;
}
static float rndFloat() { return (xorshift32() >> 8) * (1.0f / 16777216.0f); }

// -------------------------------------------------------------- Tokens ----
static int tokenOf(char c) {
  for (int i = 0; i < GPT_VOCAB; i++)
    if (GPT_CHARS[i] == c) return i;
  return -1;
}

// Turns text into tokens, reporting the first character the model has no token
// for. Token 0 (newline) is the start/end-of-fortune marker and is added by the
// caller, not here.
static bool encode(const std::string &s, std::vector<int> &out) {
  for (size_t i = 0; i < s.size(); i++) {
    int t = tokenOf(s[i]);
    if (t < 0) {
      fprintf(stderr, "error: '%c' (0x%02x) is not in the model's vocabulary\n",
              isprint((unsigned char)s[i]) ? s[i] : '?', (unsigned char)s[i]);
      return false;
    }
    out.push_back(t);
  }
  return true;
}

// ------------------------------------------------- Acceptance rules ----
// Copied from FortuneLLM.ino so the harness rejects what the board rejects.
static uint32_t fnv1a(const std::string &s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.size(); i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
  return h;
}

static bool isTrainingCopy(const std::string &s) {
  return std::binary_search(GPT_SEEN, GPT_SEEN + GPT_SEEN_COUNT, fnv1a(s));
}

static bool hasMadeUpWord(const std::string &s) {
  std::string w;
  for (size_t i = 0; i <= s.size(); i++) {
    char c = i < s.size() ? s[i] : ' ';
    if (isalpha((unsigned char)c) || c == '\'') {
      w += (char)tolower((unsigned char)c);
    } else if (!w.empty()) {
      if (!std::binary_search(GPT_WORDS, GPT_WORDS + GPT_WORD_COUNT, fnv1a(w))) return true;
      w.clear();
    }
  }
  return false;
}

static const char *rejectReason(const std::string &s) {
  if (s.size() < 12) return "too short";
  if ((int)s.size() > g_maxChars) return "too long";
  char last = s[s.size() - 1];
  if (last != '.' && last != '!' && last != '?') return "unfinished";
  if (g_skipMadeUp && hasMadeUpWord(s)) return "made-up word";
  return NULL;
}

// ---------------------------------------------------------- Generation ----
static TinyGPT gpt;

static int argmax(const float *v, int n) {
  int best = 0;
  for (int i = 1; i < n; i++)
    if (v[i] > v[best]) best = i;
  return best;
}

// One fortune, from the start token through to the end token. The prompt, if
// any, is fed first and kept in the output. greedy picks the likeliest token
// instead of sampling, which makes a run reproducible without a seed.
static std::string generateOnce(const std::vector<int> &prompt, bool greedy) {
  gpt.reset();
  std::string out;
  int tok = 0;  // token 0 = newline = start/end marker
  for (size_t i = 0; i < prompt.size(); i++) {
    gpt.step(tok);
    tok = prompt[i];
    out += GPT_CHARS[tok];
  }
  for (int i = 0; i < g_maxGen; i++) {
    const float *logits = gpt.step(tok);
    tok = greedy ? argmax(logits, GPT_VOCAB)
                 : gpt.sample(g_temperature, g_topK, rndFloat());
    if (tok == 0) break;
    out += GPT_CHARS[tok];
  }
  return out;
}

// Retries an unusable fortune the way the sketch does.
static std::string makeFortune(const std::vector<int> &prompt, bool greedy, bool filter,
                               bool verbose) {
  if (!filter) return generateOnce(prompt, greedy);
  std::string text;
  for (int attempt = 1; attempt <= g_maxTries; attempt++) {
    text = generateOnce(prompt, greedy);
    const char *why = rejectReason(text);
    if (!why && g_skipCopies && isTrainingCopy(text) && attempt < g_maxTries)
      why = "copy of training";
    if (verbose)
      printf("  try %d: %s%s%s\n", attempt, text.c_str(), why ? "  -> rejected: " : "",
             why ? why : "");
    if (!why) return text;
  }
  return "The stars are silent. Try again.";
}

// -------------------------------------------------------------- Modes ----
static int modeGenerate(int count, const std::vector<int> &prompt, bool greedy, bool filter,
                        bool verbose) {
  clock_t t0 = clock();
  long chars = 0;
  for (int i = 0; i < count; i++) {
    std::string text = makeFortune(prompt, greedy, filter, verbose);
    chars += (long)text.size();
    printf("%s\n", text.c_str());
  }
  double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
  fprintf(stderr, "\n%d fortunes, %ld chars in %.1f ms (%.3f ms/char on this machine)\n",
          count, chars, ms, chars ? ms / (double)chars : 0.0);
  return 0;
}

// Feeds the start token and the prompt, printing the logits after every step.
// One header line per step, then GPT_VOCAB values, which is what compare.py
// reads to check the engine against the Python reference.
static int modeLogits(const std::vector<int> &prompt) {
  gpt.reset();
  std::vector<int> fed;
  fed.push_back(0);
  for (size_t i = 0; i < prompt.size(); i++) fed.push_back(prompt[i]);

  printf("# tinygpt logits  vocab=%d steps=%d\n", GPT_VOCAB, (int)fed.size());
  for (size_t i = 0; i < fed.size(); i++) {
    const float *logits = gpt.step(fed[i]);
    printf("# step %d token %d\n", (int)i, fed[i]);
    for (int v = 0; v < GPT_VOCAB; v++) printf("%s%.9g", v ? " " : "", (double)logits[v]);
    printf("\n");
  }
  return 0;
}

static int g_failures = 0;
static void check(bool ok, const char *what, const char *detail = NULL) {
  printf("  %s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail ? " - " : "", detail ? detail : "");
  if (!ok) g_failures++;
}

// Consistency checks that need no reference implementation: the things that
// would silently produce plausible-looking garbage on the board.
static int modeSelftest() {
  printf("TinyGPT self-test  (dim %d, %d layers, %d heads, vocab %d, ctx %d)\n", GPT_DIM,
         GPT_LAYERS, GPT_HEADS, GPT_VOCAB, GPT_CTX);

  // ---- the model's own shape ----
  check(GPT_DIM % GPT_HEADS == 0, "dim divides evenly into heads");
  bool finiteWeights = true;
  const float *arrays[] = {W_wte,  W_wpe,  W_ln1_w, W_ln1_b, W_proj, W_ln2_w,
                           W_ln2_b, W_fc,  W_fc_proj, W_lnf_w, W_lnf_b, W_qkv};
  const size_t sizes[] = {
      sizeof(W_wte),  sizeof(W_wpe),  sizeof(W_ln1_w),   sizeof(W_ln1_b),
      sizeof(W_proj), sizeof(W_ln2_w), sizeof(W_ln2_b),  sizeof(W_fc),
      sizeof(W_fc_proj), sizeof(W_lnf_w), sizeof(W_lnf_b), sizeof(W_qkv)};
  for (size_t a = 0; a < sizeof(arrays) / sizeof(arrays[0]); a++)
    for (size_t i = 0; i < sizes[a] / sizeof(float); i++)
      if (!std::isfinite(arrays[a][i])) finiteWeights = false;
  check(finiteWeights, "every weight is finite");
  bool sortedHashes = std::adjacent_find(GPT_SEEN, GPT_SEEN + GPT_SEEN_COUNT,
                                         std::greater<uint32_t>()) == GPT_SEEN + GPT_SEEN_COUNT &&
                      std::adjacent_find(GPT_WORDS, GPT_WORDS + GPT_WORD_COUNT,
                                         std::greater<uint32_t>()) == GPT_WORDS + GPT_WORD_COUNT;
  check(sortedHashes, "training-set hashes are sorted", "the sketch binary-searches them");

  // ---- the forward pass ----
  gpt.reset();
  bool finiteLogits = true, varied = false;
  const float *l0 = gpt.step(0);
  std::vector<float> first(l0, l0 + GPT_VOCAB);
  for (int i = 0; i < 64; i++) {
    const float *l = gpt.step(argmax(l0, GPT_VOCAB));
    for (int v = 0; v < GPT_VOCAB; v++)
      if (!std::isfinite(l[v])) finiteLogits = false;
    if (memcmp(l, &first[0], sizeof(float) * GPT_VOCAB) != 0) varied = true;
  }
  check(finiteLogits, "logits stay finite over 65 steps");
  check(varied, "logits change with position", "a dead KV cache would freeze them");

  // ---- reset really clears the cache ----
  const int probe[] = {7, 28, 40, 1};
  gpt.reset();
  const float *p = NULL;
  for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) p = gpt.step(probe[i]);
  std::vector<float> clean(p, p + GPT_VOCAB);
  gpt.reset();
  for (int i = 0; i < 20; i++) gpt.step(i % GPT_VOCAB);  // dirty the cache
  gpt.reset();
  for (size_t i = 0; i < sizeof(probe) / sizeof(probe[0]); i++) p = gpt.step(probe[i]);
  check(memcmp(p, &clean[0], sizeof(float) * GPT_VOCAB) == 0, "reset() clears the KV cache",
        "otherwise the second fortune is polluted by the first");

  // ---- context limit ----
  gpt.reset();
  for (int i = 0; i < GPT_CTX + 8; i++) gpt.step(1);
  check(gpt.position() <= GPT_CTX, "position stays inside the context window");
  check(std::isfinite(gpt.step(1)[0]), "stepping past the context does not blow up");

  // ---- sampling ----
  gpt.reset();
  const float *logits = gpt.step(0);
  int top = argmax(logits, GPT_VOCAB);
  bool topKOne = true;
  for (int i = 0; i < 16; i++)
    if (gpt.sample(1.0f, 1, i / 16.0f) != top) topKOne = false;
  check(topKOne, "top-k of 1 always returns the likeliest token");
  bool coldTemp = gpt.sample(0.01f, 0, 0.5f) == top;
  check(coldTemp, "a near-zero temperature returns the likeliest token");

  // Sampled frequencies should match the softmax they came from.
  double p_ref[GPT_VOCAB], sum = 0;
  float mx = logits[top];
  for (int v = 0; v < GPT_VOCAB; v++) {
    p_ref[v] = exp(((double)logits[v] - mx) / 1.0);
    sum += p_ref[v];
  }
  for (int v = 0; v < GPT_VOCAB; v++) p_ref[v] /= sum;
  const int draws = 200000;
  std::vector<int> hist(GPT_VOCAB, 0);
  g_rng = 12345;
  for (int i = 0; i < draws; i++) hist[gpt.sample(1.0f, 0, rndFloat())]++;
  double worst = 0;
  for (int v = 0; v < GPT_VOCAB; v++)
    worst = std::max(worst, fabs(hist[v] / (double)draws - p_ref[v]));
  char msg[96];
  snprintf(msg, sizeof(msg), "largest probability error %.4f over %d draws", worst, draws);
  check(worst < 0.01, "sampling matches the softmax distribution", msg);

  // ---- generation end to end ----
  g_rng = 99;
  std::string a = generateOnce(std::vector<int>(), false);
  g_rng = 99;
  std::string b = generateOnce(std::vector<int>(), false);
  check(a == b, "the same seed gives the same fortune");
  std::string greedy = generateOnce(std::vector<int>(), true);
  check(!greedy.empty(), "greedy decoding produces text", greedy.c_str());

  printf("\n%s\n", g_failures ? "SELF-TEST FAILED" : "all checks passed");
  return g_failures ? 1 : 0;
}

static int modeBench(int steps) {
  gpt.reset();
  gpt.step(0);
  clock_t t0 = clock();
  for (int i = 0; i < steps; i++) {
    if (gpt.position() >= GPT_CTX - 1) gpt.reset();
    gpt.step(i % GPT_VOCAB);
  }
  double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
  printf("%d steps in %.1f ms  (%.4f ms/token, %.0f tokens/s on this machine)\n", steps, ms,
         ms / steps, steps * 1000.0 / ms);
  return 0;
}

// ---------------------------------------------------------------- CLI ----
static void usage() {
  printf(
      "Usage: tinygpt [mode] [options]\n"
      "\n"
      "Modes:\n"
      "  (default)        write fortunes the way the board does\n"
      "  --selftest       run consistency checks on the inference engine\n"
      "  --logits         print the logits after each step, for reference checks\n"
      "  --bench [N]      time N forward passes (default 2000)\n"
      "\n"
      "Options:\n"
      "  --count N        fortunes to write (default 1)\n"
      "  --seed N         seed for the sampler (default 1)\n"
      "  --temp F         temperature (default %.2f, the sketch's value)\n"
      "  --topk N         sample from the N likeliest tokens, 0 for all (default %d)\n"
      "  --max N          hard cap on characters per fortune (default %d)\n"
      "  --greedy         always take the likeliest token, ignoring the seed\n"
      "  --prompt TEXT    start the fortune with TEXT\n"
      "  --filter         apply the sketch's acceptance rules and retry\n"
      "  --skip-copies    with --filter, reject copies of training fortunes\n"
      "  --skip-made-up   with --filter, reject fortunes with unknown words\n"
      "  --verbose        with --filter, show rejected attempts\n",
      g_temperature, g_topK, g_maxGen);
}

int main(int argc, char **argv) {
  enum { GENERATE, SELFTEST, LOGITS, BENCH } mode = GENERATE;
  int count = 1, benchSteps = 2000;
  bool greedy = false, filter = false, verbose = false;
  std::string prompt;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
    if (a == "--help" || a == "-h") { usage(); return 0; }
    else if (a == "--selftest") mode = SELFTEST;
    else if (a == "--logits") mode = LOGITS;
    else if (a == "--bench") {
      mode = BENCH;
      if (next && next[0] != '-') benchSteps = atoi(argv[++i]);
    }
    else if (a == "--count" && next) count = atoi(argv[++i]);
    else if (a == "--seed" && next) g_rng = (uint32_t)strtoul(argv[++i], NULL, 10);
    else if (a == "--temp" && next) g_temperature = (float)atof(argv[++i]);
    else if (a == "--topk" && next) g_topK = atoi(argv[++i]);
    else if (a == "--max" && next) g_maxGen = atoi(argv[++i]);
    else if (a == "--tries" && next) g_maxTries = atoi(argv[++i]);
    else if (a == "--max-chars" && next) g_maxChars = atoi(argv[++i]);
    else if (a == "--prompt" && next) prompt = argv[++i];
    else if (a == "--greedy") greedy = true;
    else if (a == "--filter") filter = true;
    else if (a == "--skip-copies") { filter = true; g_skipCopies = true; }
    else if (a == "--skip-made-up") { filter = true; g_skipMadeUp = true; }
    else if (a == "--verbose") verbose = true;
    else {
      fprintf(stderr, "error: unknown option '%s'\n\n", a.c_str());
      usage();
      return 2;
    }
  }
  if (g_rng == 0) g_rng = 1;  // xorshift32 cannot escape zero

  if (!gpt.begin()) {
    fprintf(stderr, "error: could not allocate the key/value cache (%zu bytes)\n",
            (size_t)2 * sizeof(float) * GPT_LAYERS * GPT_CTX * GPT_DIM);
    return 1;
  }

  std::vector<int> tokens;
  if (!encode(prompt, tokens)) return 2;

  switch (mode) {
    case SELFTEST: return modeSelftest();
    case LOGITS:   return modeLogits(tokens);
    case BENCH:    return modeBench(benchSteps);
    default:       return modeGenerate(count, tokens, greedy, filter, verbose);
  }
}
