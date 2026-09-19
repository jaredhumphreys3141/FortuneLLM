// Checks the blocked-term filter off the board. Nothing here touches the
// Arduino libraries, so it builds anywhere:
//
//   g++ -std=c++11 -I. tools/test_blocklist.cpp -o /tmp/test_blocklist && /tmp/test_blocklist
//
// Run it after regenerating blocklist_data.h with make_blocklist.py.
#include <cstdio>
#include <string>
#include <vector>
#include "blocklist.h"

int fails = 0;
void check(bool got, bool want, const std::string &text, const char *what) {
  if (got != want) { printf("FAIL [%s] %-46s got %d want %d\n", what, text.c_str(), got, want); fails++; }
}
void blocked(const std::string &t)   { check(blocklist::contains(t), true,  t, "contains"); }
void allowed(const std::string &t)   { check(blocklist::contains(t), false, t, "contains"); }

int main() {
  printf("%d terms, up to %d words each\n", BLOCK_COUNT, BLOCK_MAX_WORDS);

  // --- terms inside a fortune, not just as a whole line ---
  blocked("Fortune favours the anal traveller.");
  blocked("anal");
  blocked("Beware the alabama hot pocket ahead.");   // 3-word term
  blocked("Beware the Alabama-Hot-Pocket ahead.");   // punctuated the same term
  blocked("Beware the ALABAMA   HOT  POCKET.");      // case and spacing
  blocked("Luck follows the zippo-cat.");
  blocked("Luck follows the zippocat.");
  blocked("Your path ends in anus.");                // term at the very end
  blocked("Cunt, said the oracle.");                 // followed by a comma

  // --- whole words only: these must survive ---
  allowed("A shell on the beach holds your answer.");
  allowed("Hello traveller, fortune smiles.");
  allowed("Your analysis will prove correct.");
  allowed("Assist a stranger and be repaid.");
  allowed("The cockpit of your life is yours.");
  allowed("Grass grows where you have walked.");
  allowed("A classic move will win the day.");
  allowed("Buttons and thread mend more than cloth.");
  allowed("Titan among men, you will rise.");
  allowed("Scunthorpe is a fine place to visit.");
  allowed("A cocktail of luck and timing.");
  allowed("Happiness is a warm passage home.");
  allowed("");
  allowed("Your patience will be rewarded soon.");
  allowed("Do not count the days, make them count.");

  // --- ordinary words left unblocked on purpose (tools/blocklist_allow.txt) ---
  allowed("Today you will go to hell and back.");
  allowed("A damn fine day awaits you.");
  allowed("Snatch victory from the jaws of defeat.");
  allowed("Your spunk will carry you further than your plans.");
  allowed("A pansy will bloom where you least expect it.");
  allowed("World domination is not the path for you.");
  allowed("Grope for the switch and the room will light up.");
  allowed("A bloody nose today, a crown tomorrow.");
  allowed("The nude of the painting is you.");

  // --- the live check used while a fortune is being written ---
  check(blocklist::endsWithTerm("Kiss my arse "),  true,  "Kiss my arse _",  "endsWith");
  check(blocklist::endsWithTerm("Kiss my arse."),  true,  "Kiss my arse._",  "endsWith");
  check(blocklist::endsWithTerm("An arse of a "),  false, "An arse of a _",  "endsWith");  // term is behind us
  check(blocklist::endsWithTerm("An analysis "),   false, "An analysis _",   "endsWith");
  check(blocklist::endsWithTerm("Go to hell "),    false, "Go to hell _",    "endsWith");
  check(blocklist::endsWithTerm("An anal"),        true,  "An anal (no separator yet)", "endsWith");
  check(blocklist::endsWithTerm("A "),             false, "A _",             "endsWith");

  printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails != 0;
}
