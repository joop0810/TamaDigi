// Chinese: every string can be drawn, and the canvas draws it.
//
// The bitmap font has no glyph above ASCII, so Chinese rides on a generated
// 12x12 pixel font (cjk_font.h) that holds ONLY the characters the strings
// use. This pins the promises that makes:
//
//  * every character of every Chinese string -- UI table, attacks, Digimon,
//    items, elements, areas -- is in the font (a missing one is a blank on the
//    panel, not an error anywhere);
//  * the other six languages are still pure ASCII (their font has no accents);
//  * tcols() counts a CJK character as two columns, and the canvas advances
//    exactly that far, so centring by tcols() centres;
//  * a Chinese battle line wraps between characters and never splits one.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "i18n.h"
#include "dex.h"
#include "items.h"
#include "types.h"
#include "moves.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup();
int wrapTake(const char *p, int maxCols);
extern Arduino_Canvas *gfx;
extern uint32_t gTextBroken;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// every code point of s has a glyph; counts what it checked
static bool drawable(const char *s, int &chars) {
  const uint8_t *p = (const uint8_t *)s;
  while (*p) {
    if (*p < 0x80) { p++; continue; }
    uint32_t cp; int n;
    if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; n = 1; }
    else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; n = 2; }
    else return false;
    p++;
    for (int i = 0; i < n; i++, p++) { if ((*p & 0xC0) != 0x80) return false; cp = (cp << 6) | (*p & 0x3F); }
    if (!cjkGlyph(cp)) { printf("      no glyph for U+%04X in \"%s\"\n", (unsigned)cp, s); return false; }
    chars++;
  }
  return true;
}
static bool ascii(const char *s) { for (; *s; s++) if ((uint8_t)*s > 0x7F) return false; return true; }

int main(){
  setup();

  // --- the data: every Chinese string is drawable, every other one is ASCII
  int chars = 0, strings = 0;
  bool allDrawable = true, latinAscii = true, zhIsChinese = true;
  for (int l = 0; l < LANG_COUNT; l++) {
    gLang = (Lang)l;
    auto check = [&](const char *s) {
      strings++;
      if (gLang == LANG_ZH) { if (!drawable(s, chars)) allDrawable = false; }
      else if (!ascii(s)) { latinAscii = false; printf("      not ASCII in lang %d: %s\n", l, s); }
    };
    for (int i = 0; i < STR_COUNT; i++) check(T((StrId)i));
    for (int m = 0; m < MOVE_COUNT; m++) check(moveName(m));
    for (int d = 1; d <= DEX_COUNT; d++) check(L(DEX_TBL[d].name));
    for (int k = 1; k < ITEM_COUNT; k++) check(L(ITEM_TBL[k].name));
    for (int t = 0; t < TYPE_COUNT; t++) check(typeName(t));
    for (int a = 0; a < ATTR_COUNT; a++) check(attrName(a));
    for (int i = 0; i < MED_COUNT; i++) { check(medalName(i)); check(medalLabel(i)); check(medalDesc(i)); }
  }
  printf("      %d strings, %d Chinese characters checked\n", strings, chars);
  ck(allDrawable, "every character of every Chinese string has a glyph");
  ck(latinAscii, "the six Latin-script languages stay ASCII (their font has no accents)");
  // and Chinese really is Chinese: a name L() does not know would stay English
  gLang = LANG_ZH;
  for (int d = 1; d <= DEX_COUNT; d++) if (ascii(L(DEX_TBL[d].name))) { zhIsChinese = false; printf("      untranslated: %s\n", DEX_TBL[d].name); }
  for (int k = 1; k < ITEM_COUNT; k++) if (ascii(L(ITEM_TBL[k].name))) { zhIsChinese = false; printf("      untranslated: %s\n", ITEM_TBL[k].name); }
  for (int m = 1; m < MOVE_COUNT; m++) if (ascii(moveName(m))) { zhIsChinese = false; printf("      untranslated move %d\n", m); }
  ck(zhIsChinese, "every Digimon, item and attack has a Chinese name");
  ck(!strcmp(L("NOT A NAME"), "NOT A NAME"), "L() leaves what it does not know (a nickname) alone");
  gLang = LANG_EN;
  ck(!strcmp(L(DEX_TBL[3].name), DEX_TBL[3].name), "and in English it changes nothing");

  // --- widths: tcols() and the canvas agree
  ck(tcols("ABC") == 3 && tcols("亚古兽") == 6 && tcols("Lv.5 亚古兽") == 11, "tcols(): ASCII one column, a CJK character two");
  gLang = LANG_ZH;
  for (int size = 1; size <= 3; size++) {
    gfx->setTextSize(size);
    gfx->setCursor(100, 100);
    gfx->print("Lv.5 亚古兽");
    char w[80];
    snprintf(w, sizeof(w), "size %d: the canvas advances 6 px x size per column", size);
    ck(gfx->getCursorX() - 100 == tcols("Lv.5 亚古兽") * 6 * size, w);
  }
  {
    gfx->fillScreen(0);
    gfx->setTextColor(0xFFFF);
    gfx->setTextSize(2);
    gfx->setCursor(100, 100);
    uint32_t before = gTextBroken;
    gfx->print("兽");
    int lit = 0;
    for (int y = 90; y < 130; y++) for (int x = 100; x < 124; x++) if (gfx->fb[y * 466 + x]) lit++;
    ck(lit > 40, "a CJK character actually lights pixels");
    ck(gTextBroken == before, "and nothing was counted as broken");
    char cut[8]; memcpy(cut, "兽", 2); cut[2] = 'A'; cut[3] = 0;   // a sequence cut short
    gfx->print(cut);
    ck(gTextBroken > before, "a UTF-8 sequence cut mid-character IS counted (buffers too small for Chinese)");
  }

  // --- wrapping a battle line
  {
    const char *msg = "门左卫门兽 使用了 爱之小夜曲";   // 31 columns
    int take = wrapTake(msg, 24);
    char line[64]; memcpy(line, msg, take); line[take] = 0;
    ck(tcols(line) <= 24 && tcols(line) >= 20, "a long Chinese line fills up to the width");
    bool whole = ((uint8_t)msg[take] & 0xC0) != 0x80;
    ck(whole, "and breaks between characters, never inside one");
    ck(wrapTake("A wild AGUMON appeared!", 24) == 23 && wrapTake("Super effective! And more", 24) == 20,
       "Latin text still breaks at the last space that fits");
  }

  printf("%s\n", bad ? "FAILURES" : "Chinese draws, fits and wraps");
  return bad ? 1 : 0;
}
