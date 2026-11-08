// Checks the positional STRINGS table: a language row with too few entries is
// zero-padded by the compiler with no diagnostic, which silently shifts every
// string after the gap. Also enforces the ASCII-only rule (the bitmap font has
// no glyphs for accents).
#include "Arduino.h"
#include "Preferences.h"
// linked against the same core as every other suite, so it needs the same
// hardware stubs even though it only exercises the string table
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif
#include "i18n.h"
#include "moves.h"
#include <cstdio>
#include <cstring>

// STRINGS is file-static, so go through the same accessor the firmware uses.
static const char *LANG[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "ZH" };
// Chinese is the one language allowed past ASCII (its own CJK font draws it;
// zh_test checks every character has a glyph). The other six stay ASCII.
static bool mayBeNonAscii(int l) { return l == LANG_ZH; }

int main() {
  int bad = 0;
  for (int l = 0; l < LANG_COUNT; l++)
    for (int s = 0; s < STR_COUNT; s++) {
      setLang((Lang)l);
      const char *v = T((StrId)s);
      if (!v) { printf("NULL  %s index %d\n", LANG[l], s); bad++; continue; }
      if (!mayBeNonAscii(l))
        for (const unsigned char *p = (const unsigned char *)v; *p; p++)
          if (*p > 0x7F) { printf("NON-ASCII  %s index %d: \"%s\"\n", LANG[l], s, v); bad++; break; }
    }
  for (int l = 0; l < LANG_COUNT; l++) {
    int translated = 0;
    setLang((Lang)l);
    for (int move = 0; move < MOVE_COUNT; move++) {
      const char *value = moveName((uint8_t)move);
      if (!value) { printf("NULL MOVE  %s index %d\n", LANG[l], move); bad++; continue; }
      if (!mayBeNonAscii(l))
        for (const unsigned char *p = (const unsigned char *)value; *p; p++)
          if (*p > 0x7F) { printf("NON-ASCII MOVE  %s index %d: \"%s\"\n", LANG[l], move, value); bad++; break; }
      if (move && strcmp(value, MOVE_TBL[move].name)) translated++;
      if (tcols(value) > 23) { printf("LONG MOVE  %s index %d: \"%s\"\n", LANG[l], move, value); bad++; }
    }
    // EN may differ from the internal <=12-char key: a signature attack shows
    // its full name ("PEPPER BREATH" for PEPPER BRTH). Other languages must
    // still localize the generic attacks rather than fall back wholesale.
    if (l != LANG_EN && !translated) { printf("%s move table silently fell back to English\n", LANG[l]); bad++; }
  }
  static const char *EMBER[LANG_COUNT] = {
    "PLACAJE", "TACKLE", "CHARGE", "TACKLE", "AZIONE", "INVESTIDA", "撞击"
  };
  for (int l = 0; l < LANG_COUNT; l++) {
    setLang((Lang)l);
    if (strcmp(moveName(MV_TACKLE), EMBER[l])) {
      printf("MOVE ORDER  %s expected %s, got %s\n", LANG[l], EMBER[l], moveName(MV_TACKLE));
      bad++;
    }
  }
  printf("%s: %d languages x %d strings + %d x %d move names\n",
         bad ? "FAIL" : "PASS", LANG_COUNT, STR_COUNT, LANG_COUNT, MOVE_COUNT);
  return bad ? 1 : 0;
}
