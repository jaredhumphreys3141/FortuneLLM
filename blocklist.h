// ============================================================================
//  Blocked-term filter - keeps unpleasant words out of the fortunes.
//
//  The model writes one character at a time, so nothing stops it from spelling
//  a word nobody wants on the screen. A fortune is checked before it is shown
//  and regenerated if it contains a blocked term.
//
//  The terms live in blocklist_data.h as sorted 32-bit hashes, searched the
//  same way FortuneLLM.ino searches GPT_SEEN and GPT_WORDS. A term is matched
//  on whole words, so "anal" is blocked but "analysis" is not, and a term of
//  several words ("zippo cat") matches however it is punctuated ("Zippo-Cat").
//  Ordinary words the fortunes are allowed to use, like "hell" and "snatch",
//  are listed in tools/blocklist_allow.txt and left out of the table.
//  Regenerate the table with tools/make_blocklist.py.
// ============================================================================
#pragma once
#include <algorithm>
#include <ctype.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "blocklist_data.h"

namespace blocklist {

// Letters, digits and apostrophes make up a word; everything else separates them.
inline bool isWordChar(char c) {
  return isalnum((unsigned char)c) || c == '\'';
}

inline uint32_t hash(const std::string &s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) h = (h ^ c) * 16777619u;
  return h;
}

inline bool isTerm(const std::string &phrase) {
  return std::binary_search(BLOCK_HASH, BLOCK_HASH + BLOCK_COUNT, hash(phrase));
}

// Split text into lower-case words, dropping all punctuation and spacing.
inline void split(const std::string &text, std::vector<std::string> &out) {
  std::string w;
  for (size_t i = 0; i <= text.size(); i++) {
    char c = i < text.size() ? text[i] : ' ';
    if (isWordChar(c)) {
      w += (char)tolower((unsigned char)c);
    } else if (!w.empty()) {
      out.push_back(w);
      w.clear();
    }
  }
}

// True if any run of words ending at word `last` is a blocked term.
inline bool endingAt(const std::vector<std::string> &words, size_t last) {
  std::string phrase;
  for (size_t n = 0; n < BLOCK_MAX_WORDS && n <= last; n++) {
    const std::string &first = words[last - n];
    phrase = n ? first + ' ' + phrase : first;
    if (isTerm(phrase)) return true;
  }
  return false;
}

// True if the text contains a blocked term anywhere.
inline bool contains(const std::string &text) {
  std::vector<std::string> words;
  split(text, words);
  for (size_t i = 0; i < words.size(); i++)
    if (endingAt(words, i)) return true;
  return false;
}

// True if the text *ends* with a blocked term. For checking a fortune while it
// is still being written: everything before is already known to be clean, so
// only the terms ending at the last word are worth looking at. Call it when the
// text has just gained a separator, so that last word is finished - checking
// mid-word would reject "analysis" the moment it looked like something shorter.
inline bool endsWithTerm(const std::string &text) {
  std::vector<std::string> words;
  split(text, words);
  return !words.empty() && endingAt(words, words.size() - 1);
}

}  // namespace blocklist
