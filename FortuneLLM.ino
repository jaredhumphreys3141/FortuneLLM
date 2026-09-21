/*
  ============================================================================
   SYNTHWAVE LLM FORTUNE COOKIE
   A ~200K-parameter character-level GPT, running entirely on the ESP32-S3,
   shown in an original synthwave stroke font.
   For: ESP32-S3-LCD-1.47B (USB-C, ST7789 172x320)
  ============================================================================

   Alternates every 8 seconds between:
     - a fortune written by the model, on the synthwave grid background, and
     - a randomly generated synthwave landscape (scene.h).
   The next fortune is written in the background while each landscape is on
   screen, so fortunes appear instantly. At power-up, a live status screen
   shows the model writing the first fortune character by character.
   Press BOOT to skip ahead to the next screen right away.

   Files in this sketch folder:
     FortuneLLM.ino     - this file (button, generation, screens)
     gpt.h              - the transformer inference engine
     model_weights.h    - trained weights (made by fortune_llm_training/train.py)
     synthfont.h        - the synthwave font and screen renderer
     scene.h            - the random synthwave landscape generator
     blocklist.h        - the blocked-term filter (blocklist_data.h holds the terms)

   Arduino IDE setup:
     Tools > Board:            ESP32S3 Dev Module
     Tools > USB CDC On Boot:  Enabled
     Tools > PSRAM:            OPI PSRAM                           <- required
     Tools > Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)   <- required
     Library: "GFX Library for Arduino" by Moon On Our Nation
   The first compile takes a few minutes because of the large weights file.
  ============================================================================
*/

#include <Arduino_GFX_Library.h>
#include <string>
#include <algorithm>
#include <ctype.h>
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "gpt.h"
#include "blocklist.h"
#include "synthfont.h"
#include "scene.h"

// ---------------------------------------------------------------- Pins ----
#define PIN_LCD_MOSI 45
#define PIN_LCD_SCLK 40
#define PIN_LCD_CS   42
#define PIN_LCD_DC   41
#define PIN_LCD_RST  39
#define PIN_LCD_BL   46   // 1.47B (USB-C) board
#define PIN_BUTTON   0    // BOOT button (active LOW)
#define PIN_RGB_LED  38

#define SCREEN_ROTATION 3
#define IPS_PANEL       true

// ------------------------------------------------------------ Settings ----
const float TEMPERATURE = 0.7f;   // 0.6 = safe and repetitive, 1.1 = wild but typo-prone
const int   TOP_K       = 8;      // only sample from the n likeliest characters
const int   MAX_GEN     = 110;    // hard cap on characters per fortune
const int   MAX_CHARS   = 90;     // longer fortunes are rejected (they get tiny)
const int   MAX_TRIES   = 10;     // regenerate if a fortune is unusable - raised from 6
                                   // alongside CHECK_TRIGRAMS and RECENT_MEMORY, which
                                   // reject more attempts and need the extra tries
const bool  SKIP_COPIES = false;   // regenerate exact copies of training fortunes
const bool  SKIP_MADE_UP_WORDS = true;  // regenerate fortunes containing non-words
const bool  CHECK_TRIGRAMS = true;   // regenerate fortunes with a 3-word run never
                                      // seen in training - real words, wrong order
const int   RECENT_MEMORY = 100;   // don't show a fortune that matches one of the
                                    // last N shown, so the model's favorites don't
                                    // crowd out the rest of what it knows
// Fortunes containing a blocked term (blocklist.h) are always regenerated; there
// is no setting for that one.

const unsigned long SHOW_MS = 5000;   // how long each fortune / landscape stays up
const uint8_t LED_LEVEL = 30;

// ------------------------------------------------------------- Display ----
Arduino_DataBus *bus = new Arduino_HWSPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCLK,
                                         PIN_LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_LCD_RST, SCREEN_ROTATION,
                                      IPS_PANEL, 172, 320, 34, 0, 34, 0);
const int SCREEN_W = 320;
const int SCREEN_H = 172;

uint16_t *frame = nullptr;   // full-screen RGB565 image, drawn off-screen then pushed
float *distBuf = nullptr;    // renderer scratch space

TinyGPT gpt;
bool modelReady = false;

// --------------------------------------------------------------- Helpers ----
float rndFloat() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }
void led(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(PIN_RGB_LED, r, g, b); }

void *bigAlloc(size_t bytes) {
  void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

void fallbackText(const std::string &text) {  // if screen buffers failed to allocate
  gfx->fillScreen(0);
  gfx->setTextSize(2);
  gfx->setTextColor(0xF81F);
  gfx->setCursor(4, 70);
  gfx->print(text.c_str());
}

// Render text in the synthwave style and push it to the screen.
void showText(const std::string &text) {
  if (!frame || !distBuf) { fallbackText(text); return; }
  unsigned long t0 = millis();
  synth::render(frame, distBuf, SCREEN_W, SCREEN_H, text);
  gfx->draw16bitRGBBitmap(0, 0, frame, SCREEN_W, SCREEN_H);
  Serial.printf("  (fortune screen rendered in %lu ms)\n", millis() - t0);
}

void newScene() {
  if (!frame || !distBuf) return;
  unsigned long t0 = millis();
  scene::render(frame, distBuf, SCREEN_W, SCREEN_H, esp_random());
  gfx->draw16bitRGBBitmap(0, 0, frame, SCREEN_W, SCREEN_H);
  Serial.printf("Landscape (rendered in %lu ms)\n", millis() - t0);
}

uint32_t fnv1a(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) h = (h ^ c) * 16777619u;
  return h;
}

bool isTrainingCopy(const std::string &s) {
  return std::binary_search(GPT_SEEN, GPT_SEEN + GPT_SEEN_COUNT, fnv1a(s));
}

// True if any word (letters and apostrophes) never appeared in the training text.
bool hasMadeUpWord(const std::string &s) {
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

// True if any 3 consecutive words never appeared in that order in the training
// text - real words individually, but a combination the model invented. Words
// are collected across the whole fortune the same way hasMadeUpWord() does (a
// sentence break inside a fortune does not reset the window), and each window
// is hashed as "w1 w2 w3" with a single space, matching export_header.py's
// corpus_trigrams() exactly. A fortune under 3 words has no window to check
// and passes.
bool hasBadTrigram(const std::string &s) {
  std::string words[3];
  int have = 0;
  std::string w;
  for (size_t i = 0; i <= s.size(); i++) {
    char c = i < s.size() ? s[i] : ' ';
    if (isalpha((unsigned char)c) || c == '\'') {
      w += (char)tolower((unsigned char)c);
      continue;
    }
    if (w.empty()) continue;
    words[0] = words[1];
    words[1] = words[2];
    words[2] = w;
    w.clear();
    if (++have < 3) continue;
    std::string phrase = words[0] + " " + words[1] + " " + words[2];
    if (!std::binary_search(GPT_TRIGRAMS, GPT_TRIGRAMS + GPT_TRIGRAM_COUNT, fnv1a(phrase)))
      return true;
  }
  return false;
}

// Remembers the last RECENT_MEMORY fortunes actually shown, so the same
// handful of favorites the model keeps returning to don't crowd out the rest
// of what it knows. Plain RAM, reset on reboot - it only needs to smooth out
// one sitting in front of the board, not survive power loss.
uint32_t recentHashes[RECENT_MEMORY];
int recentCount = 0, recentPos = 0;

bool isRecentRepeat(const std::string &s) {
  uint32_t h = fnv1a(s);
  for (int i = 0; i < recentCount; i++)
    if (recentHashes[i] == h) return true;
  return false;
}

void rememberRecent(const std::string &s) {
  recentHashes[recentPos] = fnv1a(s);
  recentPos = (recentPos + 1) % RECENT_MEMORY;
  if (recentCount < RECENT_MEMORY) recentCount++;
}

// Returns nullptr if usable, otherwise the reason it was rejected.
const char *rejectReason(const std::string &s) {
  if (blocklist::contains(s)) return "blocked word";
  if (s.size() < 12) return "too short";
  if ((int)s.size() > MAX_CHARS) return "too long";
  char last = s.back();
  if (last != '.' && last != '!' && last != '?') return "unfinished";
  if (SKIP_MADE_UP_WORDS && hasMadeUpWord(s)) return "made-up word";
  if (CHECK_TRIGRAMS && hasBadTrigram(s)) return "not a sensible order";
  return nullptr;
}

// ------------------------------------------------- Live status screen ----
// Shown at power-up while the first fortune is written: the model's
// progress, speed, and the text appearing as it is generated.
namespace status {
const uint16_t CYAN = synth::to565({110, 230, 255});
const uint16_t PINK = synth::to565({255, 90, 210});
const uint16_t GOLD = synth::to565({255, 215, 100});
const uint16_t DIM  = synth::to565({190, 150, 210});
const int TEXT_Y = 76, TEXT_LINES = 4, LINE_H = 18, WRAP = 25;
bool active = false;
unsigned long lastDraw = 0;

// Restore a rectangle of the background from the frame buffer (erases old text).
void restore(int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h && y < SCREEN_H; y++)
    gfx->draw16bitRGBBitmap(x0, y, frame + y * SCREEN_W + x0, w, 1);
}

void begin() {
  active = (frame != nullptr);
  if (!active) return;
  synth::drawBackground(frame, SCREEN_W, SCREEN_H);
  gfx->draw16bitRGBBitmap(0, 0, frame, SCREEN_W, SCREEN_H);
  const long params = (long)GPT_VOCAB * GPT_DIM + (long)GPT_CTX * GPT_DIM +
                      (long)GPT_LAYERS * (12L * GPT_DIM * GPT_DIM + 4L * GPT_DIM) + 2L * GPT_DIM;
  gfx->setTextSize(2);
  gfx->setTextColor(CYAN);
  gfx->setCursor(8, 6);
  gfx->print("WRITING A FORTUNE");
  gfx->setTextSize(1);
  gfx->setTextColor(DIM);
  gfx->setCursor(8, 26);
  gfx->printf("TinyGPT: %ldK params, %d layers, on-chip", params / 1000, GPT_LAYERS);
}

// Update the live readout. force = draw even if we drew very recently.
void update(int attempt, const std::string &text, unsigned long attemptStart,
            const char *note, bool force) {
  if (!active) return;
  if (!force && millis() - lastDraw < 80) return;
  lastDraw = millis();

  // stats line
  restore(0, 40, SCREEN_W, 34);
  unsigned long ms = millis() - attemptStart;
  gfx->setTextSize(1);
  gfx->setTextColor(GOLD);
  gfx->setCursor(8, 42);
  gfx->printf("Try %d of %d   Characters: %d", attempt, MAX_TRIES, (int)text.size());
  gfx->setCursor(8, 54);
  if (text.size())
    gfx->printf("Speed: %.1f ms/char   Elapsed: %.1f s", (float)ms / text.size(), ms / 1000.0f);
  else
    gfx->print("Starting...");
  if (note) {
    gfx->setTextColor(PINK);
    gfx->setCursor(8, 64);
    if (strcmp(note, "Done!") == 0) gfx->print("Done!");
    else gfx->printf("Rejected (%s) - trying again", note);
  }

  // the fortune as it's being written (last few wrapped lines)
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    line += c;
    if ((int)line.size() >= WRAP) {
      size_t sp = line.rfind(' ');
      if (sp != std::string::npos && sp > 0) { lines.push_back(line.substr(0, sp)); line = line.substr(sp + 1); }
      else { lines.push_back(line); line.clear(); }
    }
  }
  lines.push_back(line + "_");   // cursor
  size_t first = lines.size() > TEXT_LINES ? lines.size() - TEXT_LINES : 0;
  restore(0, TEXT_Y, SCREEN_W, TEXT_LINES * LINE_H);
  gfx->setTextSize(2);
  gfx->setTextColor(PINK);
  for (size_t i = first; i < lines.size(); i++) {
    gfx->setCursor(8, TEXT_Y + (i - first) * LINE_H);
    gfx->print(lines[i].c_str());
  }
}

void end() { active = false; }
}  // namespace status

// ----------------------------------------------------------- Generation ----
// Runs the model from the start-of-fortune token until it emits end-of-fortune.
// Pulses the LED; if the status screen is active, shows progress live.
std::string generateOnce(int attempt, bool &blocked) {
  gpt.reset();
  blocked = false;
  std::string out;
  unsigned long t0 = millis();
  status::update(attempt, out, t0, nullptr, true);
  int tok = 0;  // token 0 = newline = start/end marker
  for (int i = 0; i < MAX_GEN; i++) {
    gpt.step(tok);
    tok = gpt.sample(TEMPERATURE, TOP_K, rndFloat());
    if (tok == 0) break;
    out += GPT_CHARS[tok];
    // A finished word that is on the blocklist ends the attempt right away, so
    // the rest of the fortune is not written and no more of it reaches the screen.
    if (!blocklist::isWordChar(out.back()) && blocklist::endsWithTerm(out)) {
      blocked = true;
      break;
    }
    float pulse = 0.5f + 0.5f * sinf(millis() / 120.0f);
    led(LED_LEVEL * pulse, 0, LED_LEVEL * pulse * 0.6f);
    status::update(attempt, out, t0, nullptr, false);
  }
  return out;
}

// Writes one fortune, retrying if a result is unusable.
std::string makeFortune() {
  unsigned long t0 = millis();
  int chars = 0;
  std::string text;
  bool ok = false;
  for (int attempt = 1; attempt <= MAX_TRIES && !ok; attempt++) {
    unsigned long ta = millis();
    bool blocked = false;
    text = generateOnce(attempt, blocked);
    chars += text.size() + 1;
    bool copy = SKIP_COPIES && isTrainingCopy(text);
    bool repeat = !blocked && isRecentRepeat(text);
    const char *why = blocked ? "blocked word" : rejectReason(text);
    if (!why && copy && attempt < MAX_TRIES) why = "copy of training";
    if (!why && repeat && attempt < MAX_TRIES) why = "repeat of a recent fortune";
    ok = (why == nullptr);
    // A blocked fortune is never redrawn or logged - the reason alone is enough.
    status::update(attempt, blocked ? std::string() : text, ta, ok ? "Done!" : why, true);
    Serial.printf("  try %d: %s%s%s\n", attempt, blocked ? "(withheld)" : text.c_str(),
                  why ? "  -> rejected: " : "", why ? why : "");
    if (!ok && status::active) delay(600);   // let the rejection be seen
  }
  if (!ok) text = "AI FAILURE";
  else rememberRecent(text);
  unsigned long ms = millis() - t0;
  Serial.printf("Fortune: %s\n  (%d chars generated in %lu ms, %.1f ms/char)\n",
                text.c_str(), chars, ms, chars ? (float)ms / chars : 0.0f);
  led(0, 0, 0);
  return text;
}

// ------------------------------------------------ Fortune / scene cycle ----
// fortune -> landscape (next fortune is written while it's showing) -> ...
bool nextIsFortune = true;
bool havePending = false;
std::string pendingFortune;
unsigned long shownAt = 0;

void showNext() {
  if (nextIsFortune) {
    if (!havePending) pendingFortune = modelReady ? makeFortune() : "Model failed to load";
    showText(pendingFortune);
    havePending = false;
    shownAt = millis();
  } else {
    newScene();
    shownAt = millis();   // countdown starts now; the fortune is written behind the scenes
    if (modelReady) { pendingFortune = makeFortune(); havePending = true; }
  }
  nextIsFortune = !nextIsFortune;
}

// ------------------------------------------------------- Setup / loop ----
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LCD_BL, OUTPUT);
  led(0, 0, 0);

  if (!gfx->begin()) Serial.println("Display init failed!");
  gfx->setTextWrap(false);
  gfx->fillScreen(0);
  digitalWrite(PIN_LCD_BL, HIGH);

  frame = (uint16_t *)bigAlloc(sizeof(uint16_t) * SCREEN_W * SCREEN_H);
  distBuf = (float *)bigAlloc(sizeof(float) * SCREEN_W * SCREEN_H);
  if (!frame || !distBuf) Serial.println("Screen buffers failed to allocate (enable OPI PSRAM)");

  modelReady = gpt.begin();
  Serial.printf("TinyGPT: %d layers, dim %d, vocab %d, context %d -> %s\n",
                GPT_LAYERS, GPT_DIM, GPT_VOCAB, GPT_CTX,
                modelReady ? "ready" : "FAILED to allocate KV cache (enable OPI PSRAM)");

  if (modelReady) {  // write the first fortune with the live status screen
    status::begin();
    pendingFortune = makeFortune();
    havePending = true;
    delay(700);      // a moment to see the finished result
    status::end();
  }
  showNext();
}

void loop() {
  static bool wasPressed = false;
  static unsigned long pressStart = 0;
  unsigned long now = millis();
  bool pressed = (digitalRead(PIN_BUTTON) == LOW);

  if (pressed && !wasPressed) pressStart = now;
  if (!pressed && wasPressed && now - pressStart >= 30) showNext();
  wasPressed = pressed;

  if (millis() - shownAt >= SHOW_MS) showNext();
  delay(5);
}
