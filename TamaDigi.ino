// TamaDigi - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "types.h"
#include "moves.h"
#include "battle.h"
#include "trainers.h"
#include "link.h"
#include "linknow.h"
#include "backs.h"
#include "badges.h"
#include "avatars.h"
#include <stdarg.h>
#include "party.h"
#include "save.h"
#include "pet.h"
#include "items.h"
#include "inventory.h"
#include "wild.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "nvsinfo.h"
#include "i18n.h"
#include "audio.h"

// Version del firmware. Subir este numero en cada release (y manifest.json para
// el instalador web). Se muestra en la pantalla de ajustes y por serie al arrancar.
#define FW_VERSION "1.1.0"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// How many BYTES of p fit on a line of maxCols columns. Latin text breaks at
// the last space that fits; CJK breaks between any two characters (it has no
// spaces); a UTF-8 sequence is never split. Always takes at least one character.
int wrapTake(const char *p, int maxCols) {
  int cols = 0, i = 0, lastSpace = -1, lastCjk = -1;
  while (p[i]) {
    uint8_t c = (uint8_t)p[i];
    int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    int w = c < 0x80 ? 1 : (len == 2 ? 1 : 2);
    if (cols + w > maxCols) break;
    if (c == ' ') lastSpace = i;
    cols += w;
    i += len;
    if (len >= 3) lastCjk = i;       // a break after a CJK character is fine
  }
  if (!p[i]) return i;                                 // the rest fits
  if (lastCjk >= i - 3 && lastCjk > 0) return i;       // stopped inside CJK text
  if (lastSpace > 0) return lastSpace;
  return i > 0 ? i : 1;
}

// The canvas with UTF-8 text. Every gfx->print() reaches write() one byte at a
// time; ASCII goes to the library's 6x8 font exactly as before, and a multi-byte
// sequence is decoded and drawn from the 12x12 CJK pixel font (cjk_font.h) --
// so Chinese needs no change at any of the ~230 print sites. A CJK character
// advances two columns (12 px x size), which is what tcols() counts, and sits
// 2 px x size higher than the Latin line so its 12 rows centre on the 8.
// Text that could not be drawn as written: a UTF-8 sequence cut short (a buffer
// too small for a Chinese string truncates mid-character), a stray byte, or a
// character the font lacks. zh_test and the emulator's --lang sweep read it.
uint32_t gTextBroken = 0;

class TextCanvas : public Arduino_Canvas {
public:
  using Arduino_Canvas::Arduino_Canvas;
  size_t write(uint8_t c) override {
    if (c < 0x80) {
      if (u8need) { gTextBroken++; u8need = 0; }
      return Arduino_Canvas::write(c);
    }
    if ((c & 0xC0) == 0x80) {                  // continuation byte
      if (!u8need) { gTextBroken++; return 1; }  // stray: drop it
      u8cp = (u8cp << 6) | (c & 0x3F);
      if (--u8need == 0) drawCjk(u8cp);
      return 1;
    }
    if (u8need) gTextBroken++;                 // a new character before the last ended
    if ((c & 0xE0) == 0xC0)      { u8cp = c & 0x1F; u8need = 1; }
    else if ((c & 0xF0) == 0xE0) { u8cp = c & 0x0F; u8need = 2; }
    else if ((c & 0xF8) == 0xF0) { u8cp = c & 0x07; u8need = 3; }
    else u8need = 0;
    return 1;
  }
private:
  uint32_t u8cp = 0;
  uint8_t u8need = 0;
  void drawCjk(uint32_t cp) {
    const int s = textsize_x ? textsize_x : 1;
    const uint8_t *g = cjkGlyph(cp);
    if (!g) gTextBroken++;
    if (g) {
      const int y0 = cursor_y - 2 * s;
      for (int r = 0; r < CJK_CELL; r++) {
        int c = 0;
        while (c < CJK_CELL) {
          int i = r * CJK_CELL + c;
          if (!(g[i >> 3] & (0x80 >> (i & 7)))) { c++; continue; }
          int run = 1;
          while (c + run < CJK_CELL) {
            int j = r * CJK_CELL + c + run;
            if (!(g[j >> 3] & (0x80 >> (j & 7)))) break;
            run++;
          }
          fillRect(cursor_x + c * s, y0 + r * s, run * s, s, textcolor);
          c += run;
        }
      }
    }
    // a 2-byte sequence (an accent) has no glyph and takes one column, as in tcols()
    cursor_x += (cp < 0x800 ? 6 : 12) * s;
  }
};
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new TextCanvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monVariantFor = false;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
} beh;
#define PET_GROUND 304  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria digidex
bool galleryOpen = false;
bool galleryDirty = false;
// 16 to a page, and the Digidex is browsed ONE REGION AT A TIME. Three
// generations flat is 25 pages of swiping to reach Ver.3, which is not a
// Digidex, it is a scroll. A vertical swipe changes version and a horizontal one
// pages within it, so nothing is ever more than ten pages from the front.
// ALL is deliberately not offered here -- it is the thing being replaced.
#define GAL_PER_PAGE 16
#define GAL_REGIONS (REGION_COUNT - 1)          // the real versions, not ALL
#define GAL_LO (REGIONS[galleryRegion % GAL_REGIONS].lo)
#define GAL_HI (REGIONS[galleryRegion % GAL_REGIONS].hi)
#define GAL_SPAN (GAL_HI - GAL_LO + 1)
#define GAL_PAGES ((GAL_SPAN + GAL_PER_PAGE - 1) / GAL_PER_PAGE)
uint8_t galleryRegion = 0;
// Both the Digidex and the boss ladder now open on a REGION CHOOSER rather than
// dropping you into whichever version was last viewed. The vertical swipe that
// changes version still works, but it is invisible, so on its own it meant the
// Ver.2 and Ver.3 content looked absent.
bool galleryPick = false;
bool gymPick = false;
uint8_t rpickPage = 0;      // the version chooser is paged; shared by all 3 modes
uint8_t galleryPage = 0;    // GAL_PAGES paginas de GAL_PER_PAGE
// ---------------------------------------------------------------------------
// DIGIDEX FILTERS. A view setting, so RAM only -- deliberately NO save field:
// every one of these is derived from data the save already holds, and a filter
// somebody left on is not worth a migration or a line in the backup.
//
// RAISED and CAUGHT ARE GENUINELY DIFFERENT SETS HERE, which is what makes both
// worth offering rather than being two names for one bit. registerSpecies() only
// ever fires for the LIVE creature -- on hatch, on a focus swap, on revive -- so
// a wild capture that went straight into the box has never touched dexReg. The
// dex knows what you have RAISED; the party and box know what you HAVE.
enum : uint8_t { GFILT_ALL = 0, GFILT_RAISED, GFILT_CAUGHT, GFILT_VARIANT, GFILT_COUNT };
uint8_t galleryFilter = GFILT_ALL;
// Published so view_test can walk every filter without keeping its own copy of
// how many there are -- a test that restates a count silently stops covering the
// mode somebody adds next.
uint8_t galleryFilterCount() { return GFILT_COUNT; }
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // pulsacion corta del boton PWR
// Cached from nvsLowOnSpace(), refreshed at boot and on the HEALTH heartbeat
// rather than every frame -- nvs_get_stats() is not free, and drawSaveWarning()
// runs on every render. See nvsinfo.cpp: a full partition is erased WHOLE on
// the next boot, so this is the only advance notice a player ever gets.
bool gNvsLow = false;
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;
enum : uint8_t { KB_PET = 0, KB_TRAINER };
uint8_t kbTarget = KB_PET;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_PAGES 4   // profile, stats, moves, progress -- medals moved to
                       // the player card, where the totals already live
uint8_t cardPage = 0;         // 0 perfil, 1 stats+medallas
// Menu overlay: opened by tapping the pet's name on the main screen. The
// horizontal swipe is already taken by the Digidex (it pages through 10 pages
// internally), so a hub on that axis would be ambiguous; the header was inert
// and is the only free surface left.
bool menuOpen = false;
#define MENU_X 73
// Four rows: PARTY and GYMS came out, since a swipe right and a swipe left now
// reach them directly. Sized to the bezel -- the panel is 320 wide, so 160 from
// the centre, and sqrt(233^2 - 160^2) = 169 means it can only span y 64..402.
#define MENU_Y 70
#define MENU_W 320
#define MENU_H 326
#define MENU_ROW_H 44
#define MENU_ROW_GAP 6
// 6 rows: STATS / BAG / DIGIDEX / SETTINGS / RETIRE / CLOSE. The row height came
// down from 52 to fit the sixth: the panel spans 70..396, and the round display
// gives a half-width of 166 there against the 160 a row needs, so the corners
// stay on glass. Anything taller does not -- at 6 rows of 52 it would reach 413
// and the bottom corners would fall off the bezel.
#define MENU_ROWS 6
#define MENU_ROW_Y(i) (MENU_Y + 16 + (i) * (MENU_ROW_H + MENU_ROW_GAP))

// Party screen. partyPick != 0 means the newcomer needs a slot: the player
// either taps someone to replace or lets it go.
bool partyOpen = false;
bool partyPick = false;
uint8_t partyDetail = 0;   // 0 = the grid, else slot + 1
// The box, reached from the party screen. `boxSwapFrom` is the party slot
// waiting for something to trade with, 0 when nothing is pending.
bool boxOpen = false;
uint8_t boxPage = 0;
uint8_t boxSwapFrom = 0;   // party slot + 1, armed from the party side
uint8_t boxSel = 0;        // box slot + 1, armed from the box side
// How the box is ORDERED ON SCREEN. RAM only, like the Digidex filter, and for a
// stronger reason: see boxSlotFor(). Sorting the array itself would rewrite half
// of a CRC'd checkpoint every time somebody glanced at the box.
enum : uint8_t { BSORT_SLOT = 0, BSORT_DEX, BSORT_LEVEL, BSORT_COUNT };
uint8_t boxSort = BSORT_SLOT;
uint8_t boxSortCount() { return BSORT_COUNT; }
// The box gets the same detail sheet the party has. Tapping a box slot used to
// yank the creature into the party immediately, which is a surprising amount to
// happen from one tap -- and left nowhere to put a RELEASE button.
uint8_t boxDetail = 0;        // box slot + 1 whose sheet is open
// The RELEASE confirm, on whichever sheet is up. One flag rather than two,
// because only one sheet can be open at a time and partyDetail/boxDetail
// already say which.
bool releaseConfirm = false;
#define BOX_PER_PAGE 6
uint32_t partyBannerUntil = 0;   // "<name> joined the party!"
char partyBannerName[14] = "";
// The party and box moved from a 2x3 grid to a RING; see partySlotPos(). A grid
// on a circle spends its corners on glass that is not there -- the old 150x70
// cells put the outer corners of the top and bottom rows within a few pixels of
// the bezel, and 70 px is 6.7 mm, under the finger floor, on a screen whose
// controls have been reported as hard to hit twice already. Six 104 px discs fit
// inside radius 181 with room to spare, and hand the middle of the panel to the
// hub instead of to two more cell corners.
#define PSLOT_CY 224       // the ring leaves a prompt band under the title and
#define PSLOT_RING 112     // clears the CLOSE bar below
#define PSLOT_R 50         // 100 px across -- 9.5 mm
// 94 px: the hub is the one control here that had room to reach the finger
// floor outright. The slots' inner edge is 62 px from the ring centre, so it
// can grow to 47 without touching them.
#define PSLOT_HUB_R 47
#define PSLOT_PROMPT_Y 68

bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion
// Settings is paged: 0 is the clock, 1 is sound/volume/brightness/language and
// the reset. Declared up here with the other screen state because openClock()
// resets both, and uiRimTarget() pages it -- both of which sit above the
// settings screen's own section.
#define CLOCK_PAGES 2
uint8_t clockPage = 0;
// The reset confirm, on the settings screen. Its own flag rather than another
// choiceKind: that dialog is drawn over the MAIN screen, and this one is modal
// over settings.
bool resetConfirm = false;

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis

// minijuego "toques": mantener la scan chip en el aire
bool gameOpen = false;
uint32_t gameOverUntil = 0;
// The ball game used to run until you missed three times or walked away, so a
// session had no length and the reward no shape. The bag is 10 s and the
// reaction test 15 s; this sits between them, and whichever comes first --
// the clock or three misses -- ends it.
#define GAME_MS 20000UL
uint32_t gameUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
bool gameNewHi = false;

// The memory game. Declared here with the other minigame flags because loop()
// and uiCurrentScreen() read it thousands of lines before its own section.
bool memoOpen = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// training submenu (the 5th icon): routes to the tamer for each stat.
// DEF has no minigame -- it rises on its own from good wellbeing -- so its row
// is informational and does not respond to a tap.
bool trainOpen = false;

// move picker, opened from the MOVES card page. Most learnsets are level 0, so
// a level-up "you learned a move" prompt would almost never fire -- the moveset
// is edited on demand instead.
bool movePickOpen = false;
uint8_t movePickSlot = 0;   // which of the 4 slots is being replaced
uint8_t movePickParty = 0;  // 0 = the live pet, else the party slot + 1
uint8_t movePickPage = 0;
#define MOVE_ROW_Y(i) (96 + (i) * 58)
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)
// level-up learn prompt: modal, and deliberately without a timeout -- it
// decides what the creature is for the rest of its life, and once banked into
// the party, forever.
#define LEARN_ROW_Y(i) (104 + (i) * 56)
#define LEARN_SKIP_Y 334

// ---------- battle ----------
// The move menu is a 2x2 grid rather than four stacked rows: the round panel
// has to fit both creatures, both HP bars and the menu, and four full-width
// rows do not leave room for the sprites.
bool battleOpen = false;

enum : uint8_t {
  SCR_STARTER = 0, SCR_REGION, SCR_GALLERY, SCR_DEXPICK, SCR_MOVEPICK, SCR_BOX,
  SCR_PARTY, SCR_KEYBOARD, SCR_CARD, SCR_PLAYER, SCR_CLOCK, SCR_GYM, SCR_GYMPICK,
  SCR_EXPLORE, SCR_LAN, SCR_PICK, SCR_BATTLE, SCR_WIN, SCR_LEARN, SCR_TRAIN, SCR_MENU,
  SCR_BAGSCR, SCR_GAME, SCR_MAIN, SCR_TRANSFER, SCR_COUNT
};
extern const char *const SCREEN_NAME[SCR_COUNT];   // const is internal linkage in C++

// Declared HERE, above every use. tools/emu/build.sh generates a proto.h with
// every prototype at the top, so the emulator compiled these happily while
// arduino-cli -- which relies on the IDE's auto-prototyping and does not always
// produce one -- did not. A green emulator build is not proof the firmware
// builds; only arduino-cli is.
void bootReport();
uint8_t uiCurrentScreen();
// The gesture model, defined next to uiCurrentScreen() but used by handleTouch()
// ~1000 lines above it.
int uiTileIndex();
void uiTileGo(int i);
void onRim(int dir);
uint8_t uiRimPages();
uint8_t uiRimPage();
bool uiOnRim(int16_t x, int16_t y);
float uiRimAngle(int16_t x, int16_t y);
void uiDrawRimBar();
void uiChrome();
uint8_t learnableList(uint8_t *out, uint8_t max);
uint8_t pickCandidates();
// Declared here because renderMonSheet() and the sheet's tap handlers use both
// ~4000 lines above where they are defined. The emulator generates a proto.h and
// would compile it either way; arduino-cli would not, and that has shipped once.
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
void renderParty();
void renderBox();
// The bag and wild encounters are defined ~3000 lines below render() and the
// touch handler that reach them. arduino-cli does not always auto-prototype,
// and the emulator's generated proto.h would hide that -- see the note above.
void renderBag();
void bagTap(int16_t x, int16_t y);
uint8_t bagPages();
bool startWildBattle(uint8_t region, bool hard);
PartyMon wildToPartyMon();
void focusSwap(uint8_t slot);
// Declared here because setup() calls it ~5000 lines above its definition, and
// arduino-cli does not always auto-prototype -- see the note at the top of this
// block. The emulator's generated proto.h would have hidden it.
void focusRecover();
// The Digidex filter and the box's on-screen order. Both are reached by
// uiRimTarget(), ~4000 lines above where they are defined, because the number of
// PAGES now depends on them -- and both are non-static so a test can drive the
// firmware's own answer instead of recomputing it.
bool galleryPass(int16_t dex);
uint16_t galleryCount();
int16_t galleryAt(uint16_t visual);
uint8_t galleryPages();
const char *galleryFilterName();
void galleryClampPage();
// The memory game. render() and onTap() reach these ~2000 lines above where they
// are defined, and arduino-cli does not always auto-prototype.
void startMemoGame();
void renderMemo();
void memoTap(int16_t x, int16_t y);
void leaveMemo();
void memoPadPos(int i, int *cx, int *cy);
int memoPadAt(int16_t x, int16_t y);
uint8_t boxSlotFor(uint8_t visual);
const char *boxSortName();
uint8_t galleryFilterCount();
uint8_t boxSortCount();
// The derived personality's name, and today's checklist. Both are read by the
// card and the player screen, which sit either side of where they are defined.
const char *personalityName();
int uiGoalCount();
bool uiGoalMet(int i);
int uiGoalsMet();
void drawConfirmPanel(const char *q, const char *sub1, const char *sub2,
                      uint16_t subCol, const char *o1, uint16_t c1, uint16_t t1,
                      const char *o2, uint16_t c2, uint16_t t2);
const char *const SCREEN_NAME[SCR_COUNT] = {
  "starter", "region", "gallery", "dexpick", "movepick", "box",
  "party", "keyboard", "card", "player", "clock", "gym", "gympick",
  "explore", "lan", "pick", "battle", "win", "learn", "train", "menu",
  "bag", "minigame", "main", "transfer"
};

// Badge art for a version, or nullptr when that version has none yet.
//
// NEVER `BADGES_ART[version % BADGE_REGIONS]`. The moment GYM_REGIONS outgrew
// BADGE_REGIONS -- which happened when Ver.4 landed and the upstream badge set
// stops at Ver.4 -- 5 % 5 = 0 silently dressed Ver.4 in VER.1's badges, on both
// the win screen and the player card. A wrong badge is worse than an honest
// blank: it claims you won something you did not. The callers draw a
// placeholder instead, and static_assert below stops the array being indexed
// out of range whatever the two counts are.
static_assert(BADGE_REGIONS <= GYM_REGIONS,
              "more badge sheets than ladders -- BADGES_ART would be indexed by a region that does not exist");
static const BadgeArt *badgeArtFor(uint8_t region, uint8_t i) {
  if (region >= BADGE_REGIONS || i >= TRAINER_GYMS) return nullptr;
  return &BADGES_ART[region][i];
}

// Does this version have its own badge art? Split out with a primitive return
// type so the tests can ask: the emulator's genproto.py emits every prototype
// ABOVE the includes, where the BadgeArt struct does not exist yet.
bool badgeArtExists(uint8_t region, uint8_t i) { return badgeArtFor(region, i) != nullptr; }

Combatant btlYou, btlFoe;
bool btlOver = false;
bool btlWon = false;
bool btlNewBadge = false;
uint32_t btlWinUntil = 0;   // the win screen is up
// A tamer fight is a run of 1v1s: both sides queue their squad and the next
// one steps up when the current one faints. This is the whole difficulty curve
// -- no gating, just attrition, so one strong creature sweeps the first boss and dies
// four deep into Lance.
// The ladder is now sequential: a boss opens once the previous one is beaten,
// tracked per difficulty so hard mode is its own run. This replaces the earlier
// "no gating, attrition is the gate" rule -- with both ladders level-capped,
// nothing stopped you opening with Lance and simply losing, which read as a
// dead end rather than a challenge.
// reaction test (trains SPEED)
bool spdOpen = false;
uint32_t spdUntil = 0, spdOverUntil = 0, spdBorn = 0;
int16_t spdX = 0, spdY = 0;
uint16_t spdHits = 0, spdMisses = 0;
uint8_t spdGain = 0;
bool spdNewHi = false;

bool gymOpen = false;
bool gymHard = false;   // which ladder the list is showing

// LAN battle. `lanOpen` is the pairing screen; once both squads are known the
// normal battle screen takes over with btlLink set.
bool lanOpen = false;
Link lan;
// What the shared version chooser is being used FOR. It only changes the
// subtitle and whether there is a way back: at first boot every count would
// read zero, which tells the player nothing, and there is nowhere to go back to.
#define RPICK_FOR_GYMS  0
#define RPICK_FOR_DEX   1
#define RPICK_FOR_START 2
// Three rows is what the round panel fits above the BACK label. The dex now
// lists four versions and will list more, so the chooser PAGES rather than
// growing -- and every paged screen in this sketch has to be driven by
// swipe_test, which is why that test exists at all.
#define RPICK_PER_PAGE  3
extern uint8_t rpickPage;
static void renderRegionPick(uint8_t mode);   // the version chooser, defined below
// Not static: swipe_test drives these so it asks the FIRMWARE for the page
// count instead of recomputing it from its own copy of RPICK_PER_PAGE, which
// would prove the transcription rather than the screen.
uint8_t rpickRegions(uint8_t mode);           // rows this mode lists (boss ladders: 3)
uint8_t rpickPageCount(uint8_t mode);
uint8_t rpickModeNow();                       // which chooser is up, 0xFF for none
uint8_t rpickModeNow();                       // which chooser is up, or 0xFF
static bool rpickSwipe(int dir);              // true if it handled the gesture
// NOT static: explore_test drives it, because the sprite-pack gate it enforces
// now covers the boss ladders as well and a greyed row that still answered a tap
// is exactly the fault that change fixes.
int regionPickTap(int16_t x, int16_t y, uint8_t mode);
static void drawEggRegion();          // defined with the egg screen helpers
static int eggRegionTap(int16_t x, int16_t y);
static void drawBtlBack();
static void btlLinkPoll();   // defined with the battle code, called from render()
static void btlSwitchTo(uint8_t i);
static void btlResolve(uint8_t yourMove);
// The peer's whole team, kept live. A tamer's replacements are built fresh
// from TRAINERS[] because they only ever arrive once; a linked opponent can
// switch OUT and back IN, so its creatures have to remember how battered they
// are. Host side only -- the guest takes absolute health off the wire.
Combatant btlFoeSquad[TRAINER_TEAM_MAX];
uint8_t btlFoeSquadN = 0;
uint8_t btlMyAct = 0;        // host: our own action, latched until theirs lands
// Which ladder the boss screen and the current fight belong to. The battle keeps
// its own copy so that leaving the boss list mid-fight cannot retarget the badge.
// The BOX button on the party screen. It was 150x32 with a pixel-exact hit
// test, which is a small target on a round panel -- reported as hard to press,
// same complaint as the battle grid. Now bigger AND padded: the drawn size grew
// too, so the button looks like the size it actually is rather than hiding a
// generous hit area behind a small graphic.
// The smallest a button may be. Three separate "hard to hit" reports -- the
// battle grid's bottom row, the party screen's BOX, and the LAN button -- were
// all the same mistake: a control sized to fit its label rather than a finger.
//
// 44 is the number everyone quotes, but it is 44 POINTS. This panel is 466 px
// across 1.75 in = 266 ppi, so a pixel is 0.095 mm and 44 px is 4.2 mm, under
// half a fingertip. It survives as the HARD FLOOR because several laid-out
// screens cannot grow without being redesigned, but nothing new may use it.
#define UI_TAP_MIN 44
// 9 mm -- what watchOS and Wear OS both land on -- in this panel's pixels.
// Every control laid out from here on is held to THIS by hit_test.
#define UI_TAP_FINGER 94

// The LAN battle button on the boss version chooser.
#define LANBTN_W 190
#define LANBTN_H UI_TAP_MIN
#define LANBTN_X (233 - LANBTN_W / 2)
#define LANBTN_Y 336

// The BOX button is the hub in the middle of the party ring now (PSLOT_HUB_R),
// so it has no rectangle of its own any more.
// CLOSE sits below BOX with a real gap between them. They used to touch at
// y=372, and BOX's padding then reached to 380 -- so the top of CLOSE was
// inside BOX's hit area and taps there opened the box instead of closing the
// screen. Padding one button into its neighbour just moves the problem along.
#define PARTYCLOSE_Y 376
#define PARTYCLOSE_H UI_TAP_MIN
#define PARTYCLOSE_X 133
#define PARTYCLOSE_W 200

// The confirm panel, in ONE place. Three callers draw it -- the digivolve/farewell/
// retire dialog on the main screen, and letting a banked creature go from the
// party or box sheet -- and hit_test asserts against the same numbers through
// uiConfirmRects(). Copies of this geometry in the tap handler are exactly how
// a YES button ends up somewhere the drawing is not.
#define CONFIRM_X 73
#define CONFIRM_Y 156
#define CONFIRM_W 320
#define CONFIRM_H 188
#define CONFIRM_BTN_X 93
#define CONFIRM_BTN_W 280
#define CONFIRM_BTN_H 52
#define CONFIRM_B1_Y 206
#define CONFIRM_B2_Y 268

// The two buttons on the party/box detail sheet. The primary action owns the
// panel centre. Party: MAKE ACTIVE / TO BOX. Box: TO PARTY / RELEASE.
//
// THEY ARE NOT EQUAL HALVES, and that is the whole point. The first version
// centred the pair on 233 with a dead gap between them -- which put the gap at
// the CENTRE OF THE PANEL, the one place a thumb naturally lands, so BRING BACK
// "did not work" and was reported as broken. Worse, every build up to v3.5 drew
// BRING BACK as a FULL-WIDTH button centred exactly there, so muscle memory
// aimed straight at the dead zone.
//
// The primary action now owns the centre. RELEASE is narrower, offset right,
// and still over UI_TAP_MIN -- it is irreversible, so it should be reachable
// but never the thing you hit by aiming at the middle. The gap between them is
// wider than before, not narrower.
#define PDET_BTN_Y 326
#define PDET_BTN_H 64
#define PDET_L_X 64
#define PDET_L_W 200
#define PDET_R_X 282
#define PDET_R_W 120
static_assert(PDET_L_X < 233 && 233 < PDET_L_X + PDET_L_W,
              "the panel centre must land on the sheet's PRIMARY button, not between the two");
static_assert(PDET_R_X > PDET_L_X + PDET_L_W + 8,
              "the destructive button needs a real dead gap before it");

uint8_t gymRegion = 0;
uint8_t btlRegion = 0;
#define TRAINERS (TRAINER_SETS[gymRegion % GYM_REGIONS].list)
#define BTL_TRAINERS (TRAINER_SETS[btlRegion % GYM_REGIONS].list)
bool gShowAllAvatars = false;  // emulator screenshot aid, never set on hardware
// Emulator-only console commands. A RUNTIME FLAG, not a #ifdef, and deliberately:
// tests/run.sh compiles these same sources twice -- once with g++ for the
// emulator and once with arduino-cli for the board -- and treats any divergence
// between the two as the bug it is hunting. A -D would give the firmware build
// different code to compile, which is exactly what that check exists to catch.
// So the board COMPILES this and simply never sets the flag; main_sdl.cpp does.
// Same shape as gShowAllAvatars above, for the same reason.
bool gEmuDebug = false;
bool btlPetIn = false;       // was the live pet in the squad?
uint8_t btlTrainGain = 0;    // what the win trained, for the win screen
uint8_t btlTrainWhich = 0;
bool btlLink = false;      // this fight is against another device
bool btlLinkHost = false;
static bool gymUnlocked(uint8_t idx, bool hard) {
  return idx == 0 || pet.hasBadge(gymRegion, idx - 1, hard);
}

// Team select. Candidate 0 is the live pet, 1..PARTY_SLOTS are the banked
// members, so one bitmask covers the whole pool.
bool pickOpen = false;
// The team picker serves the boss ladder and the LAN screen both. PICK_LAN is
// not a tamer index: squadCap() already returns an uncapped six for anything
// past the roster, which is what a LAN battle wants -- two players who know
// each other can bring what they like.
#define PICK_LAN 0xFF
static void lanOffer(bool host);
uint8_t pickTrainer = 0;
bool pickHard = false;
// Which version the picker was opened FOR, snapshotted when it opens -- the same
// arrangement as btlRegion, and for the same reason. gymRegion belongs to the
// chooser and the player can move it; anything the picker decides has to keep
// pointing at the ladder it was opened from. Forgetting to fill in btlRegion is
// what sent the first boss's Onix into every version's boss (issue #4), so the two reads
// this alias replaces are the same shape one screen earlier.
uint8_t pickRegion = 0;
#define PICK_TRAINERS (TRAINER_SETS[pickRegion % GYM_REGIONS].list)
bool lanWantHost = true;   // which button opened the picker
uint16_t squadMask = 0xFFFF;   // everything, until the player says otherwise
uint8_t pickPage = 0;
#define PICK_PER_PAGE 6
#define PICK_CELL_W 150
#define PICK_CELL_H 74
#define PICK_X(i) (78 + ((i) % 2) * (PICK_CELL_W + 10))
#define PICK_Y(i) (86 + ((i) / 2) * (PICK_CELL_H + 6))
#define PICK_GO_Y 350
// BACK beside FIGHT on the team-select screen. There was no way out of it but a
// swipe, which is invisible -- the same complaint as everywhere else.
#define PICK_BTN_W 155
#define PICK_BTN_H UI_TAP_MIN
#define PICK_BACK_X (233 - PICK_BTN_W - 7)
#define PICK_GO_X (233 + 7)
bool playerOpen = false;
// One badge page per boss version, then the medals. Three ladders will not fit on
// one page, and the page you are on IS the version -- no extra control needed,
// and horizontal paging already works everywhere else.
uint8_t playerPage = 0;
// One page per boss version, then the medals, then TODAY's checklist. Derived from
// GYM_REGIONS rather than written out, so adding a ladder adds its own page.
#define PLAYER_PAGES (GYM_REGIONS + 2)
#define playerBadgeRegion (playerPage % GYM_REGIONS)
uint8_t gymPage = 0;
#define GYM_ROWS 5
#define GYM_ROW_Y(i) (110 + (i) * 50)
#define GYM_ROW_H 44
#define GYM_ROW_INSET 18   // how far a row stops short of the glass, so the
                           // arc scrollbar at RIM_BAR_R has its own lane
void gymRowRect(int i, int *x, int *y, int *w, int *h);
// LAN remains attached to the tamer ladder; Explore is a peer destination on
// the tile axis and no longer hides behind GYMS.
#define GYMBTN_Y 380
#define GYMBTN_W 190
#define GYMBTN_H UI_TAP_MIN
#define GYMBTN_X (CX - GYMBTN_W / 2)
int8_t btlTrainer = -1;      // index into TRAINERS, -1 = a one-off fight
bool btlHard = false;
Combatant btlSquad[TRAINER_TEAM_MAX + 1];
uint8_t btlSquadN = 0, btlSquadAt = 0;
uint8_t btlFoeAt = 0;

// ---------------------------------------------------------------------------
// The bag, and wild encounters.
bool exploreOpen = false;
bool exploreHard = false;
uint8_t exploreRegion = 0xFF;
#define EXPLORE_REGION_X 78
#define EXPLORE_REGION_Y 170
#define EXPLORE_REGION_W 310
#define EXPLORE_REGION_H UI_TAP_FINGER
#define EXPLORE_REGION_ARROW_W UI_TAP_FINGER
#define EXPLORE_DIF_Y 272
#define EXPLORE_DIF_H UI_TAP_MIN
#define EXPLORE_BTN_X 78
#define EXPLORE_BTN_Y 326
#define EXPLORE_BTN_W 310
#define EXPLORE_BTN_H 72
#define EXPLORE_BTN_PAD 11
bool bagOpen = false;
uint8_t bagPage = 0;
#define BAG_PER_PAGE 5
#define BAG_ROW_Y(i) (112 + (i) * 58)

// A wild fight is a different fight, not a tamer fight with a flag: a ball
// works, the foe can run, and RUN is a roll rather than a certainty. Every one
// of those is asked through btlWild, so there is no second opinion about what
// kind of battle is on screen.
bool btlWild = false;
// The wild creature's durable state, kept BESIDE the Combatant because a
// Combatant carries no IVs and none of what a stored record needs. Without it a
// capture would have to re-roll the individual it just caught, and the creature
// you fought would not be the creature you kept.
int16_t wildDex = 0;
uint8_t wildLvl = 1;
uint8_t wildIv[4] = { 0, 0, 0, 0 };   // atk, def, spe, hp
bool wildVariant = false;
bool wildCaught = false;
// What a win handed over, for the settlement lines on the win screen.
ItemKey wildDrops[4] = { ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE };
uint8_t wildDropN = 0;
uint8_t btlBagPage = 0;

// Animation. Deliberately built on the thumbnails the screen already draws
// rather than on PmdMon: three PmdMon blobs are live already, and the battle
// has to stay graceful on a board with no SD at all (S_NO_SPRITES). Index 0 is
// you, 1 is the foe.
uint32_t btlLungeUntil[2] = { 0, 0 };   // acted: leans toward the opponent
uint32_t btlHitUntil[2] = { 0, 0 };     // was hit: jitters and flashes
uint16_t btlHpShown[2] = { 0, 0 };      // bars ease toward the real value
// Two streamed sprites, so the creatures can actually swing and flinch. They
// cost ~135 KB of PSRAM each on average and are freed when the fight ends. The
// player's side is NOT the global `pmd`: the active creature may be a banked
// party member rather than the live pet.
PmdMon btlPmd[2];
int16_t btlPmdDex[2] = { 0, 0 };
// A faint used to swap the next creature in instantly, inside the same call
// that resolved the turn -- which is why it felt like a jump cut. The swap is
// now deferred: the fainted one drops out of frame, and the replacement slides
// in only once the player dismisses that message.
uint32_t btlFaintUntil[2] = { 0, 0 };
uint32_t btlEnterUntil[2] = { 0, 0 };
int8_t btlSwapWho = -1;        // 0 = your side, 1 = the foe's, -1 = nothing due
#define BTL_FAINT_MS 700
#define BTL_ENTER_MS 420
// battle menu: 0 = FIGHT/DIGIMON, 1 = the moves, 2 = the switch list
uint8_t btlMenu = 0;
#define BTL_LUNGE_MS 260
#define BTL_HIT_MS 420
char btlMsg[6][80];     // 80: a Chinese line is ~3 bytes a character
uint8_t btlMsgCount = 0;   // queued lines; a tap shows the next
#define BTL_CELL_W 160
#define BTL_CELL_H 54
#define BTL_GRID_X 69
#define BTL_GRID_Y 270
#define BTL_GRID_H (BTL_CELL_H * 2 + 8)
#define BTL_CELL_X(i) (BTL_GRID_X + ((i) % 2) * (BTL_CELL_W + 8))
#define BTL_CELL_Y(i) (BTL_GRID_Y + ((i) / 2) * (BTL_CELL_H + 8))

// A cell's HIT area is bigger than the cell that is drawn. On the board the two
// bottom buttons were much harder to hit than the top two: the drawn cells are
// only 44 px tall, there was an 8 px dead gap between the rows, and everything
// below the bottom row was dead too -- so a finger landing low, or a touch panel
// reading a few pixels high, missed entirely. Nothing else is tappable in this
// area while the grid is open, so the slop costs nothing.
//
// The gap between the two rows and the two columns is split down the middle, and
// the bottom row additionally claims the empty space beneath it.
#define BTL_HIT_PAD 4
// The bottom row keeps extra room downward, but not so much that it reaches the
// BACK bar below it -- the mistake made once already with BOX and CLOSE.
#define BTL_HIT_BOTTOM 6
// BACK, under the grid: the move and switch screens had no way out except
// choosing something.
// The boss list's EASY/HARD pill. It was 24 px tall, which is half a fingertip.
// Sits between the title and the first boss row. At 72 with a 44 px height it
// ran to 116 and overlapped the first row, which begins at 110 -- introduced
// when the pill was enlarged to a real tap target.
#define GYMDIF_Y 60
#define GYMDIF_H UI_TAP_MIN
#define BTL_BACK_W 190
#define BTL_BACK_H UI_TAP_MIN
#define BTL_BACK_X (233 - BTL_BACK_W / 2)
#define BTL_BACK_Y 396
#define BTL_HIT_X0(i) (BTL_CELL_X(i) - BTL_HIT_PAD)
// The far edges stop one pixel short so the four boxes TILE: the gap between
// two cells is split down the middle with no pixel left over and none shared.
#define BTL_HIT_X1(i) (BTL_CELL_X(i) + BTL_CELL_W + BTL_HIT_PAD - 1)
#define BTL_HIT_Y0(i) (BTL_CELL_Y(i) - BTL_HIT_PAD)
#define BTL_HIT_Y1(i) (BTL_CELL_Y(i) + BTL_CELL_H - 1 + \
                       ((i) / 2 ? BTL_HIT_BOTTOM : BTL_HIT_PAD))

// Which cell a point falls in, or -1. Exposed (not static) so a test can sweep
// the panel and prove there are no dead pixels between the cells -- the bug that
// made the bottom row hard to press was a gap, not a wrong rectangle.
int btlCellIndexAt(int16_t x, int16_t y);

// Used by BOTH the move grid and the switch grid. They had a copy each of the
// same rectangle test, which is exactly how two halves of one control drift.
static inline bool btlCellHit(int i, int16_t x, int16_t y) {
  return x >= BTL_HIT_X0(i) && x <= BTL_HIT_X1(i) &&
         y >= BTL_HIT_Y0(i) && y <= BTL_HIT_Y1(i);
}
// The training menu, now FOUR rows: the memory game joined the three stat
// tamers. Retuned rather than stretched -- at the old 56 px pitch a fourth row
// would end at y 442, and the panel is bounded by the glass, not by taste.
//
// The panel is 312 wide, so its corners sit 156 from the centre, and the BOTTOM
// pair is what binds: sqrt(233^2 - 156^2) = 173, so the panel cannot reach past
// y = 233 + 173 = 406. At 326 tall it ends at 404 and its worst corner is at
// radius 231 of 233. The first attempt at this was 332 tall and put those corners
// at 236 -- off the glass, which is the mistake the boss ladder made before
// uiSafeHalfWidthFor() existed, so the numbers above are checked rather than
// judged. Rows are UI_TAP_MIN tall with a 10 px gap.
#define TRAIN_X (CX - 156)
#define TRAIN_Y 78
#define TRAIN_W 312
#define TRAIN_H 326
#define TRAIN_ROW_H UI_TAP_MIN
#define TRAIN_ROW_GAP 10
#define TRAIN_ROW_Y(i) (TRAIN_Y + 46 + (i) * (TRAIN_ROW_H + TRAIN_ROW_GAP))
// FOUR, and referred to by name: the tap handler used to compare `i` against
// literals, which is how removing an icon once silently made BATH the wake button.
#define TRAIN_ROWS 4
#define TRAIN_ROW_ATK 0
#define TRAIN_ROW_SPE 1
#define TRAIN_ROW_DEF 2
#define TRAIN_ROW_MEMO 3

// Settings page 2's rows. See the essay above renderClockOptions().
#define SET_ROWS 5
#define SET_ROW_H UI_TAP_MIN
#define SET_ROW_GAP 10
#define SET_ROW_Y(i) (92 + (i) * (SET_ROW_H + SET_ROW_GAP))
#define SET_ROW_INSET 16
#define SET_SOUND 0
#define SET_VOL   1
#define SET_BRIGHT 2
#define SET_LANG  3
#define SET_RESET 4

// The memory game. See the essay above startMemoGame().
#define MEMO_PADS 4
#define MEMO_MAX 16          // sixteen is far past what anyone will reach
#define MEMO_FLASH_MS 420    // how long each pad lights while showing
#define MEMO_GAP_MS 180      // and the dark gap between them
#define MEMO_R 62            // pad radius
#define MEMO_RING 116        // how far the pads sit from the centre
#define MEMO_INPUT_MS 4000   // per-tap patience: a pause is not a wrong answer

// Today's care checklist rows. See the essay above renderPlayerGoals().
#define GOAL_ROW_H UI_TAP_MIN
#define GOAL_ROW_GAP 8
#define GOAL_ROW_Y(i) (104 + (i) * (GOAL_ROW_H + GOAL_ROW_GAP))

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice

#define CX 233  // centro de la pantalla redonda
#define CY 233
#define UI_R 233   // the glass is a CIRCLE, and every full-width layout must ask
#define HEADER_NAME_Y 52
#define HEADER_NAME_H 24
#define PET_CY 202  // centro vertical del sprite

// Half the usable width at row y: the chord of the panel at that height.
//
// A layout that picks ONE width for every row is wrong twice -- it wastes the
// middle, where the full 466 px is available, and it hangs the corners of its
// top and bottom rows off the glass. Both had shipped: the boss ladder drew five
// identical 326 px rows over y 110..374, where the real chord runs 448 down to
// 372. Derive it, never restate it; this is the only place the circle is solved.
int uiSafeHalfWidth(int y) {
  int dy = y - CY;
  if (dy <= -UI_R || dy >= UI_R) return 0;
  return (int)sqrtf((float)(UI_R * UI_R - dy * dy));
}

// The narrower of the two chords a box spanning y0..y1 must fit inside. A
// rounded row is widest at its vertical middle, but its CORNERS are what leave
// the circle, so it has to be sized by its worst edge rather than its centre.
int uiSafeHalfWidthFor(int y0, int y1) {
  int a = uiSafeHalfWidth(y0), b = uiSafeHalfWidth(y1);
  return a < b ? a : b;
}

// An arc segment: Arduino_GFX has no arc primitive, so it is stamped from
// overlapping dots. Angles are degrees clockwise from 12 o'clock.
void uiArc(int cx, int cy, int r, int a0, int a1, int w, uint16_t col) {
  if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
  int rad = w / 2;
  if (rad < 1) rad = 1;
  int n = (int)(((a1 - a0) * 0.01745f * r) / rad) + 1;   // dots must overlap
  for (int i = 0; i <= n; i++) {
    float ang = ((float)a0 + (float)(a1 - a0) * (float)i / (float)n - 90.0f) * 0.01745329f;
    gfx->fillCircle(cx + (int)(cosf(ang) * r), cy + (int)(sinf(ang) * r), rad, col);
  }
}

// The gesture model. Its tables and handlers live down beside uiCurrentScreen(),
// but handleTouch() and onSwipe() sit ~1000 lines above them.
#define TILE_COUNT 6   // PLAYER . PARTY . [PET] . EXPLORE . GYM . DEX
#define TILE_PET 2
#define RIM_R0 186     // a drag starting outside this radius pages, not swipes
#define RIM_STEP 20    // degrees of arc per page
#define RIM_BAR_R 222  // where the arc scrollbar is drawn
// 2 to 5 o'clock. Angles are clockwise from 12, so this is the right flank --
// the top is the title's and the bottom is the tile dots'.
#define RIM_BAR_A0 55
#define RIM_BAR_A1 145

// A boss row's rectangle -- as wide as the panel allows AT ITS OWN HEIGHT, so
// the ladder bulges in the middle instead of throwing away 66 px there and
// hanging its top and bottom corners off the glass. The draw path and the tap
// path both ask this; they used to share the literals 70 and 396.
void gymRowRect(int i, int *x, int *y, int *w, int *h) {
  int ry = GYM_ROW_Y(i);
  int half = uiSafeHalfWidthFor(ry, ry + GYM_ROW_H) - GYM_ROW_INSET;
  if (x) *x = CX - half;
  if (y) *y = ry;
  if (w) *w = half * 2;
  if (h) *h = GYM_ROW_H;
}

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
// Five across the arc: spacing tightened 62 -> 54 so the outer pair stays far
// enough in to keep its old y. Lifting them instead would have run the row into
// the ENE/HYG bars, which end at y=361 -- the buttons are 52 tall, so any centre
// above 387 overlaps them.
// Four, not five. The ball had its own icon here until it became DEFENCE's
// tamer and moved into the training menu -- at which point tapping it just
// opened the same menu the dumbbell does, two icons for one destination.
//
// They sit on the panel's curve: y = 372 - dx^2/1100, the outer pair lifted so
// the row stays on glass. At 80 px and 88 px apart the outermost reaches radius
// 220 of 233 -- the row cannot grow again without losing an icon, because four
// UI_TAP_FINGER targets need 392 px of chord and reach radius 222.
#define BTN_COUNT 4
// Referred to by NAME, never by literal index. Removing the ball icon shifted
// every index by one and drawButtons() still had `i != 2` meaning LIGHT -- which
// silently made the BATH button the one that wakes the pet.
#define BTN_FOOD  0
#define BTN_LIGHT 1
#define BTN_BATH  2
#define BTN_TRAIN 3
Btn buttons[BTN_COUNT] = {
  {  92, 352, SPR_ICON_FOOD },   // comer
  { 186, 370, SPR_ICON_LIGHT },  // luz
  { 280, 370, SPR_ICON_CLEAN },  // bano
  { 374, 352, SPR_ICON_TRAIN },  // entrenar
};
// The GRAPHIC is 72 px and the HIT AREA is 94. They were the same 80 px, which
// looked heavy without actually reaching the finger floor -- the usual answer
// here is a target bigger than the thing it draws, which is what the battle
// grid and the egg pill already do. At a 94 px pitch the four hit discs are
// exactly tangent: no overlap to make a tap ambiguous, no gap to swallow one.
#define BTN_HALF 36
#define BTN_HIT 47

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// eleccion de inicial (primera partida): BOTAMON / AGUMON / BETAMON, 3 filas
// The first-boot starter list is the FRONT of each version's starter array in
// dex.h -- not a copy of it. That array is also the pool a version's first egg
// is drawn from (pet.cpp rollInRegion). Every version starts from its Baby I
// egg, and the choice screen only lists the ones that can be drawn. starter_test pins the first three of every version, so
// reordering that array cannot silently change the first screen anyone sees.
#define STARTER_SHOWN 3
// Only Baby Is that can actually be drawn are offered: Ver.3's Poyomon has no
// sprite yet, so the mixed pool skips it rather than offering a "?".
int16_t starterOf(uint8_t region, uint8_t i) {
  const RegionInfo &rg = REGIONS[region % REGION_COUNT];
  uint8_t k = 0;
  for (uint8_t j = 0; j < rg.starterCount; j++) {
    if (!speciesShowable(rg.starters[j])) continue;
    if (k++ == i) return rg.starters[j];
  }
  return rg.starters[0];
}
uint8_t starterCountShown(uint8_t region) {
  const RegionInfo &rg = REGIONS[region % REGION_COUNT];
  uint8_t n = 0;
  for (uint8_t j = 0; j < rg.starterCount; j++)
    if (speciesShowable(rg.starters[j])) n++;
  return n < STARTER_SHOWN ? n : STARTER_SHOWN;
}

// First boot runs version -> starter. This is NOT persisted: a reset between the
// two lands back on the version, which is the harmless direction to fail in --
// nothing has been chosen yet, and pet.setRegion() is idempotent.
static bool starterRegionDone = false;
#define STARTER_ROW_Y 110
#define STARTER_ROW_H 70
#define STARTER_ROW_GAP 8
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176

// The food popup. It had its rectangle written out twice -- once where it is
// drawn and once in the tap handler -- which is the shape that drifts the
// moment either moves, and it had to move: the home row grew up into it.
#define FEED_MENU_X 101
#define FEED_MENU_Y 236
#define FEED_MENU_W 264
#define FEED_MENU_H 64
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)

// ---------------------------------------------------------------------------
// BRIGHTNESS: the player's setting, and what the panel is actually showing.
//
// The setting is a CEILING on the automatic ladder, not a replacement for it.
// The dim stages and the sleep dimming are AMOLED protection rather than
// preferences -- a setting that overrode them would let somebody park a static
// screen at full brightness for a week -- so the level raises or lowers the
// attended brightness and the ladder still walks down from wherever that lands.
//
// gBrightLevel is persisted under "brt", exactly as the volume is under "vol",
// and BRIGHT_DEFAULT is chosen so that brightBase() returns the numbers this
// firmware has always used. An existing save has no "brt" key, reads the
// default, and behaves bit-identically -- which is the whole requirement: a
// missing setting must mean today's behaviour, never zero.
#define BRIGHT_LEVELS 10
#define BRIGHT_DEFAULT 7
uint8_t gBrightLevel = BRIGHT_DEFAULT;
// What the panel is set to RIGHT NOW. It used to be a function-local static
// inside updateBrightness(), which meant the one fact "the screen is dark" was
// unreachable from anywhere else -- and made the setting unable to take effect,
// since a level change that happened to compute the same target was cached away.
uint8_t gBrightNow = 255;

// The attended, undimmed brightness for a level. THE one place the setting turns
// into a number.
uint8_t brightBase(uint8_t level, bool usb) {
  if (level < 1) level = 1;
  if (level > BRIGHT_LEVELS) level = BRIGHT_LEVELS;
  const uint8_t v = usb ? 180 : 145;     // what it has always been at the default
  if (level >= BRIGHT_DEFAULT)
    return (uint8_t)(v + (uint16_t)(255 - v) * (level - BRIGHT_DEFAULT) /
                             (BRIGHT_LEVELS - BRIGHT_DEFAULT));
  // Below the default it scales down, with a floor: 0 belongs to the PWR button,
  // and a settings screen able to reach it could leave a player with a black
  // panel and no visible way back.
  const uint16_t lo = (uint16_t)v * level / BRIGHT_DEFAULT;
  return (uint8_t)(lo < 8 ? 8 : lo);
}

void brightLoad() {
  Preferences p;
  if (!p.begin("tamadigi", true)) return;
  uint8_t v = p.getUChar("brt", BRIGHT_DEFAULT);
  p.end();
  if (v < 1 || v > BRIGHT_LEVELS) v = BRIGHT_DEFAULT;   // junk reads as default
  gBrightLevel = v;
}

void brightSetLevel(uint8_t level) {
  if (level < 1) level = 1;
  if (level > BRIGHT_LEVELS) level = BRIGHT_LEVELS;
  gBrightLevel = level;
  gBrightNow = 255;      // force updateBrightness() to re-apply on the next pass
  Preferences p;
  if (!p.begin("tamadigi", false)) return;
  p.putUChar("brt", gBrightLevel);
  p.end();
}
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogo de decision: 0 ninguno, 1 evolucion, 2 despedida
uint32_t choiceUntil = 0;   // se cierra solo a este millis
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool holdFired = false;
// Rim-drag state. Decided at touch-down: a gesture that starts outside RIM_R0
// is a scroll for its whole life, and never also resolves as a swipe or a tap.
bool rimActive = false;
bool rimMoved = false;
float rimA0 = 0;
int rimSteps = 0;

// A factory reset has emptied NVS and we are waiting to restart into a new game.
// Nothing may write the in-RAM creature back over it in the meantime.
bool saveInhibited = false;

// Flush a pending save because power is about to go away: the PWR key on its way
// to the AXP2101's hardware cut, the battery's low warning, or a software
// restart. THE single answer for all three, so the one case where it must NOT
// happen cannot be forgotten by whoever adds the fourth -- see CLAUDE.md § "A
// rule enforced in one path but not in its twin", which is a list of what
// happens when three callers each keep their own copy of a guard.
//
// Deliberately ignores the dim/off gate that the periodic autosave in loop()
// obeys. That gate exists so a ~1 s flash write is never visible mid-animation;
// a visible stutter is a far smaller price than the last few minutes of a
// creature's life.
void flushBeforePowerLoss(const char *why) {
  if (saveInhibited) return;
  if (!pet.savePending()) return;
  Serial.printf("save: flushing (%s)\n", why);
  pet.flushSave();
}

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaDigi fw v%s\n", FW_VERSION);
  bootReport();   // why the last run ended, and what it was doing
  // BEFORE anything opens the game namespace or loads a creature into RAM. A
  // wipe asked for on the previous run finishes here, where there is no window
  // for the old creature to be written back over it. See save.h § RESET_NS.
  resetRecover();
  loadLang();  // idioma guardado (ES por defecto)
  brightLoad();  // brillo guardado (por defecto: el de siempre)
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  // The player's own level, not a literal 180: booting to full brightness and
  // then dropping to a dim setting a frame later is a flash in the eye every
  // time the device starts. usbPresent() is not readable yet -- the PMU is set
  // up further down -- so this takes the battery figure and the first
  // updateBrightness() corrects it if the cable is in.
  gBrightNow = brightBase(gBrightLevel, false);
  panel->setBrightness(gBrightNow);

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("CST9217 no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  party.begin();
  bag.begin();
  pet.begin();
  // AFTER pet.begin(), because it compares against the pet checkpoint's loaded
  // generation, and BEFORE syncClock() below, so offline progression is applied
  // to whichever creature the swap actually settled on.
  focusRecover();
  sdBegin();
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  if (e == 0) {
    // CLOCK_EPOCH_FLOOR, not a literal: the same number is what clockVerdict()
    // treats as the earliest real time, and two copies of it would let the seed
    // drift below the floor that judges it.
    rtcSetEpoch(CLOCK_EPOCH_FLOOR);  // RTC virgen: semilla (la hora absoluta da
    e = rtcEpoch();                  // igual, solo importan las diferencias)
    Serial.println("RTC sin hora: sembrado, sin progresion offline esta vez");
  }
  pet.syncClock(e);

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

  // After everything has loaded, so the figure reflects a real save rather than
  // an empty one. A full partition is erased WHOLE by the core on the next boot,
  // so this number trending down over a soak test is the only warning there is.
  nvsReport("boot");
  gNvsLow = nvsLowOnSpace();

  // Covers the SOFTWARE restarts -- ESP.restart() after an IMPORT, and the one
  // the serial console does. It does NOT cover the AXP2101's 4-second hardware
  // power-off, which tells the firmware nothing at all; that is what the
  // long-press flush in loop() is for.
  esp_register_shutdown_handler([]() { flushBeforePowerLoss("shutdown"); });

  lastInteract = millis();
}

// A sprite pack is streaming in over USB. The game stands aside -- no game frames,
// no taps, no per-file sprite reload or version rescan -- so the board spends its
// time on the card. Never over a fight or a minigame: their render paths pump the
// link and apply training, so those keep the old, slower behaviour.
bool transferMode() {
  return sdTransferAt && millis() - sdTransferAt < SD_TRANSFER_IDLE_MS &&
         !battleOpen && !lan.live() && !gameOpen && !sackOpen && !spdOpen && !memoOpen;
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  bool sdReload = sdDirty && !transferMode();
  if (pet.speciesId == monFor && monVariantFor == pet.variant && !sdReload) return;
  sdDirty = false;
  // A NEW CREATURE IS ON THE PANEL: let it speak. This one hook covers hatching,
  // digivolving and swapping the focused creature, because all three arrive here --
  // which is better than three call sites that could drift apart.
  //
  // monFor starts at -2, the "nothing loaded yet" sentinel, and that case is
  // deliberately silent: a cry on every boot is a device that shouts when you turn
  // it on. -1 is an egg, which has no voice either.
  if (monFor != -2 && pet.speciesId >= 1 && pet.speciesId != monFor)
    audioCry(pet.speciesId);
  monFor = pet.speciesId;
  monVariantFor = pet.variant;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.variant);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.variant);  // respaldo: B/N
  }
}

void loop() {
  uint32_t now = millis();
  pet.update(now);

  // The link is pumped here rather than from the LAN screen, because it has to
  // keep running through the battle too: linkNowPoll() drains what the radio
  // parked on the WiFi task, and tick() is what resends a lost packet and gives
  // up on a peer that has gone quiet.
  if (lan.live()) {
    linkNowPoll();
    lan.tick(now);
  }

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  handleSerial();
  ensureMon();

  // A sprite pack can arrive long after the card was mounted: the web installer
  // streams it over PUT into the FIRMWARE THAT IS ALREADY RUNNING. gRegionArt was
  // computed once in sdBegin(), so without this the version a player just spent ten
  // minutes downloading stayed greyed out reading NEEDS PACK until they rebooted --
  // indistinguishable, from the player's side, from the download having failed.
  // Quietly, because the host is still parsing this serial stream -- and once per
  // transfer rather than after every file, which cost 27 lookups in /mons each time.
  if (sdArtDirty && !transferMode()) {
    sdArtDirty = false;
    sdScanRegionArt(false);
  }
  // The same, for the gallery: thumbs.bin is read in setup(), so a copy that arrived
  // over PUT drew nothing new until a restart.
  if (sdThumbsDirty && !transferMode()) {
    sdThumbsDirty = false;
    thumbs.load();
  }

  // A farewell or release just finished: the creature is waiting for a slot.
  // With room it simply joins; with a full party the player is taken straight
  // to the party screen to choose who it replaces, or to let it go.
  if (pet.endedKind != CER_NONE && !partyPick) {
    // party first, then the box; only a full box makes it your choice
    if (party.add(pet.endedMon) || party.boxAdd(pet.endedMon)) {
      snprintf(partyBannerName, sizeof(partyBannerName), "%s",
               pet.endedMon.nick[0] ? pet.endedMon.nick : L(DEX_TBL[pet.endedMon.dex].name));
      partyBannerUntil = now + 3500;
      pet.clearEnded();
      sfxPlay(SFX_MEDAL);
    } else {
      partyPick = true;
      partyOpen = true;
      menuOpen = false;
    }
  }

  // El PMU late sus interrupciones: se leen TODAS de una vez (pwrPoll) y los
  // accesores de abajo consumen lo que vio. Ver rtcbat.cpp: dos sondeos
  // independientes se borrarian los bits el uno al otro.
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    pwrPoll();
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      pet.setScreenOff(screenOff);   // asleep only if it is also night
      if (!screenOff) lastInteract = now;
    }
    // The hold is on its way to the AXP2101's own power-off, and the firmware
    // gets no second warning. Flush NOW, ignoring the dim/off gate below: that
    // gate exists so a ~1 s flash write is never visible mid-animation, and a
    // visible stutter is a much smaller price than the last few minutes of a
    // creature's life. Same for the battery's low warning.
    if (pwrLongPressed()) flushBeforePowerLoss("pwr long press");
    if (batLowWarning()) flushBeforePowerLoss("battery low");
  }

  updateBrightness(now);

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u save=%s\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), pet.saveHealthy() ? "ok" : "BROKEN");
    // NVS headroom rides the same heartbeat as the heap, and for the same
    // reason: the value on its own says little, the trend over a soak says
    // everything. See nvsinfo.cpp for what running out costs.
    nvsReport("health");
    gNvsLow = nvsLowOnSpace();
  }

  // 85 ms en juego/saco: margen seguro para que el redibujado no pise el envio
  // DMA del frame anterior (a 40-65 ms solapaba y causaba flashes negros; con
  // sprites grandes el dibujo tarda mas, asi que se deja colchon)
  //
  // WITH THE PANEL DARK THE CADENCE STRETCHES; IT DOES NOT STOP. Skipping
  // render() outright is the obvious optimisation and it is wrong, because this
  // sketch advances real state inside its render paths: renderBattle() pumps the
  // LAN turn (btlLinkPoll), renderGame/renderSack/renderSpeed are what APPLY a
  // finished session's training and close it, renderGallery consumes
  // galleryDirty, and four modal timers -- the food menu, the release confirm,
  // the choice dialog and the party banner -- only expire in render(). Returning
  // early would freeze a linked fight, silently discard a training session, and
  // strand a dialog open until something else redrew.
  //
  // So the frame still happens, just far less often, and anything with motion or
  // a peer waiting on it keeps its full rate.
  uint32_t period = (gameOpen || sackOpen || spdOpen || memoOpen) ? 85 : 100;
  if (gBrightNow == 0 && !battleOpen && !gameOpen && !sackOpen && !spdOpen && !memoOpen)
    period = 500;
  // The one pause, and transferMode() already excludes the fights and minigames above.
  if (transferMode()) period = 1000;
  if (now - lastRender >= period) {
    lastRender = now;
    render();
  }
}

// brillo segun sueno + inactividad (proteccion del AMOLED)
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart()) {
    lastInteract = now;
  }
  // `now` is taken at the top of loop(), and a tap handled later in the same
  // pass sets lastInteract a few ms AFTER it. The plain difference then wrapped
  // to ~49 days, dimStage jumped to 2 and the panel dropped to brightness 8 --
  // near black -- right after a tap, which is every turn of a fight. Found on
  // TamaPoke through its diagnostics log (BRIGHT -> 8 then -> 180 straight
  // away); the bug came with the engine. A touch "in the future" is a touch now.
  uint32_t idle = (int32_t)(now - lastInteract) > 0 ? now - lastInteract : 0;
  dimStage = (idle > 300000) ? 2 : (idle > 90000) ? 1 : 0;
  const uint8_t base = brightBase(gBrightLevel, usbPresent());
  uint8_t target = pet.sleeping ? 25 : base;
  if (dimStage == 1) target = pet.sleeping ? 10 : 60;
  else if (dimStage == 2) target = 8;
  // The setting is a CEILING. Without this a low level would still be overridden
  // upwards by the dim ladder's own numbers -- at level 1 the base is ~20 and
  // dimStage 1's flat 60 would make an idle screen BRIGHTER than an attended one.
  if (target > base) target = base;
  if (screenOff) target = 0;
  if (target != gBrightNow) {
    gBrightNow = target;
    panel->setBrightness(target);
  }
}

// ---------- consola serie (provision de SD + depuracion) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    // level() is 1 + age/rate, so the age for level N is (N-1) rates -- this
    // used to set N and hand back N+1, which is a poor thing for a command
    // named LVL to do when it is what every balance check is anchored on.
    long want = line.substring(4).toInt();
    if (want < 1) want = 1;
    if (want > MAX_LEVEL) want = MAX_LEVEL;
    pet.ageMinutes = (uint32_t)(want - 1) * MINUTES_PER_LEVEL;
    pet.saveNow();
    Serial.printf("lvl=%u\n", pet.level());
  } else if (line == "MAX" && gEmuDebug) {
    // MAX: the fully-raised creature, in one command. LVL 100 + IV 31 x4 + TR at
    // the ceiling + no care mistakes, which is otherwise four commands in the
    // right order -- TR has to come after IV, because trMaxFor() is what caps it.
    //
    // EMULATOR ONLY, gated on gEmuDebug. Not because it is dangerous but because
    // it is not earned: this is a game about raising something slowly, and a
    // one-word cheat on a board would be a way to skip the whole point of it by
    // accident. The board still compiles this; see the flag's note.
    //
    // Deliberately BEFORE any startsWith("MA...") branch would go, and an exact
    // match, so it cannot shadow a future command.
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
    pet.trAtk = pet.trMaxAtk();
    pet.trDef = pet.trMaxDef();
    pet.trSpe = pet.trMaxSpe();
    pet.ageMinutes = (uint32_t)(MAX_LEVEL - 1) * MINUTES_PER_LEVEL;
    pet.careMistakes = 0;
    pet.relearnFromLevel();          // a level 100 creature knows level 100 moves
    pet.saveNow();
    Serial.printf("lvl=%u iv=31/31/31/31 tr=%u/%u/%u\n", pet.level(),
                  pet.trAtk, pet.trDef, pet.trSpe);
    Serial.println("DONE");
  } else if (line.startsWith("MISS ")) {
    // MISS <n>: sets the care mistakes -- "descuidos", the desc= on STATS.
    // Each one pushes every digivolution threshold up a level, so a creature that
    // was neglected early digivolves late; MISS 0 forgives that. Its sibling
    // commands are IV, TR and LVL.
    long m = line.substring(5).toInt();
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    pet.careMistakes = (uint8_t)m;
    pet.saveNow();
    Serial.printf("desc=%u\n", pet.careMistakes);
  } else if (line.startsWith("TR ")) {
    // TR <atk> <def> <spe>: sets the TRAINING (this game's EVs), for testing a
    // fully-raised creature without playing the minigames for an hour. Each is
    // clamped to trMaxFor(iv), the same IV-bound ceiling the games enforce, so
    // this cannot produce a creature the player could not have raised.
    int v[3] = { 0, 0, 0 };
    int n = sscanf(line.c_str() + 3, "%d %d %d", &v[0], &v[1], &v[2]);
    if (n >= 1) {
      int a = v[0], d = (n >= 2) ? v[1] : v[0], e = (n >= 3) ? v[2] : v[0];
      pet.trAtk = (uint8_t)(a < 0 ? 0 : (a > pet.trMaxAtk() ? pet.trMaxAtk() : a));
      pet.trDef = (uint8_t)(d < 0 ? 0 : (d > pet.trMaxDef() ? pet.trMaxDef() : d));
      pet.trSpe = (uint8_t)(e < 0 ? 0 : (e > pet.trMaxSpe() ? pet.trMaxSpe() : e));
      pet.saveNow();
    }
    Serial.printf("tr=%u/%u/%u topes=%u/%u/%u\n", pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
  } else if (line.startsWith("IV ")) {
    // IV <fue> <def> <vel> <vit>: fija los valores individuales (pruebas).
    // Con "IV 31 31 31 31" se ve el techo; con "IV 8 8 8 8" el suelo.
    int v[4] = { 16, 16, 16, 16 };
    int n = sscanf(line.c_str() + 3, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1) {
      for (int i = 0; i < 4; i++) v[i] = v[i] < 0 ? 0 : (v[i] > 31 ? 31 : v[i]);
      pet.ivAtk = v[0];
      pet.ivDef = (n >= 2) ? v[1] : v[0];
      pet.ivSpe = (n >= 3) ? v[2] : v[0];
      pet.ivHp = (n >= 4) ? v[3] : v[0];
      if (pet.trAtk > pet.trMaxAtk()) pet.trAtk = pet.trMaxAtk();
      if (pet.trDef > pet.trMaxDef()) pet.trDef = pet.trMaxDef();
      if (pet.trSpe > pet.trMaxSpe()) pet.trSpe = pet.trMaxSpe();
    }
    pet.saveNow();   // IV used to change RAM only and never write
    Serial.printf("iv=%u/%u/%u/%u topes=%u/%u/%u\n", pet.ivAtk, pet.ivDef,
                  pet.ivSpe, pet.ivHp, pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "BAG") {
    for (uint8_t i = 0; i < bag.distinctCount(); i++) {
      ItemKey k = bag.keyAt(i);
      Serial.printf("%u:%s x%u\n", k, itemEntry(k).name, bag.count(k));
    }
    Serial.println("DONE");
  } else if (line.startsWith("GIVE ")) {
    // GIVE <key> [n]: stock the bag, so catching can be exercised without
    // grinding wild wins first.
    int key = 0, n = 1;
    if (sscanf(line.c_str() + 5, "%d %d", &key, &n) >= 1 &&
        itemValid((ItemKey)key)) {
      uint8_t got = bag.add((ItemKey)key, (uint8_t)(n > 0 ? n : 1));
      Serial.printf("%s x%u (+%u)\n", ITEM_TBL[key].name,
                    bag.count((ItemKey)key), got);
    } else {
      Serial.println("bad item");
    }
    Serial.println("DONE");
  } else if (line == "WILD" || line == "WILD HARD") {
    bool hard = (line == "WILD HARD");
    if (startWildBattle(currentExploreRegion(), hard))
      Serial.printf("wild %s Lv.%u%s iv=%u/%u/%u/%u\n", DEX_TBL[wildDex].name,
                    wildLvl, wildVariant ? " *VARIANT*" : "",
                    wildIv[0], wildIv[1], wildIv[2], wildIv[3]);
    else
      Serial.println("no encounter (egg, ceremony, or no sprite pack)");
    Serial.println("DONE");
  } else if (line.startsWith("FOCUS ")) {
    // FOCUS <slot>: swap the creature on the main screen with a party slot,
    // which is the console half of the MAKE ACTIVE button.
    int s = -1;
    if (sscanf(line.c_str() + 6, "%d", &s) == 1 && s >= 0 && s < PARTY_SLOTS &&
        !party.slots[s].empty()) {
      focusSwap((uint8_t)s);
      Serial.printf("raising %s Lv.%u\n", DEX_TBL[pet.speciesId].name, pet.level());
    } else {
      Serial.println("empty or out of range");
    }
    Serial.println("DONE");
  } else if (line.startsWith("EGG ")) {
    // EGG <dex> [variant]: hatch a chosen Digimon right now. The Mega and
    // variant IV guarantees only apply at hatch time, so this is the only way to
    // exercise them on hardware (VARIANT below just toggles the flag afterwards).
    int dex = 0, sh = 0;
    int n = sscanf(line.c_str() + 4, "%d %d", &dex, &sh);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      pet.dbgHatchAs(dex, sh != 0);
      Serial.printf("%s%s iv=%u/%u/%u/%u\n", DEX_TBL[dex].name,
                    pet.variant ? " *VARIANT*" : "", pet.ivAtk, pet.ivDef,
                    pet.ivSpe, pet.ivHp);
    }
    Serial.println("DONE");
  } else if (line.startsWith("BATTLE")) {
    // BATTLE <dex> [level] -- the only way in until the tamer roster exists
    int dex = 0, lvl = 0;
    int n = sscanf(line.c_str() + 6, "%d %d", &dex, &lvl);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      startBattle(dex, lvl > 0 ? (uint8_t)lvl : pet.level());
      Serial.printf("battle vs %s Lv.%u\n", DEX_TBL[dex].name, btlFoe.level);
    } else {
      Serial.println("uso: BATTLE <dex> [nivel]");
    }
    Serial.println("DONE");
  } else if (line == "VARIANT") {  // alterna variant del actual (pruebas)
    pet.variant = !pet.variant;
    Serial.printf("variant=%d\n", pet.variant);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "EXPORT") {
    // Prints the whole save as a block of IMPORT commands. Pasting that block
    // back is the restore -- there is no separate format to get wrong, and no
    // single 2000-character line for a terminal to mangle.
    //
    // saveExport() reads the checkpoints only (see save.cpp) -- those are what
    // save() and persist() write on every change, so nothing needs forcing
    // fresh here first.
    static uint8_t buf[SAVE_TRANSFER_MAX];
    size_t n = saveExport(buf, sizeof(buf));
    if (!n) { Serial.println("EXPORT FAIL"); return; }
    Serial.printf("# TamaDigi save, %u bytes. Paste this whole block back.\n",
                  (unsigned)n);
    for (size_t i = 0; i < n; i += 48) {
      Serial.print("IMPORT ");
      for (size_t j = i; j < i + 48 && j < n; j++) Serial.printf("%02X", buf[j]);
      Serial.println();
    }
    Serial.println("IMPORT");        // the empty one commits
  } else if (line.startsWith("IMPORT")) {
    // IMPORT <hex>   append a chunk
    // IMPORT         commit what has been appended
    static uint8_t in[SAVE_TRANSFER_MAX];
    static size_t inN = 0;
    String hex = line.substring(6);
    hex.trim();
    if (hex.length()) {
      if (hex.length() & 1) { Serial.println("IMPORT ODD"); inN = 0; return; }
      for (size_t i = 0; i + 1 < (size_t)hex.length(); i += 2) {
        if (inN >= sizeof(in)) { Serial.println("IMPORT FULL"); inN = 0; return; }
        auto nyb = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return -1;
        };
        const char *hs = hex.c_str();
        int hi = nyb(hs[i]), lo = nyb(hs[i + 1]);
        if (hi < 0 || lo < 0) { Serial.println("IMPORT BAD"); inN = 0; return; }
        in[inN++] = (uint8_t)((hi << 4) | lo);
      }
      Serial.println("IMPORT MORE");
      return;                        // wait for the next chunk or commit
    }
    if (!inN) { Serial.println("IMPORT EMPTY"); return; }
    bool ok = saveImport(in, inN);
    Serial.println(ok ? "IMPORT OK" : "IMPORT REJECTED");
    inN = 0;
    if (ok) { Serial.println("DONE"); delay(100); ESP.restart(); }
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
    // Set BEFORE the restart, because the restart runs the shutdown handler, and
    // that handler's whole job is to flush a pending save. Without this it would
    // write the creature that was just deleted straight back into the empty NVS.
    saveInhibited = true;
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line.startsWith("PARTY")) {
    // PARTY          list the party
    // PARTY <dex>    bank a level-50 specimen (fills slots up for testing)
    // PARTY CLEAR    empty it
    String arg = line.substring(5);
    arg.trim();
    if (arg == "CLEAR") {
      for (int i = 0; i < PARTY_SLOTS; i++) party.releaseAt(i);
    } else if (arg.length()) {
      int d = arg.toInt();
      if (d >= 1 && d <= DEX_COUNT) {
        PartyMon m;
        m.dex = d;
        m.level = 50;
        m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
        m.trAtk = m.trDef = m.trSpe = 50;
        Serial.println(party.add(m) ? "added" : "party full");
      }
    }
    Serial.printf("party %u/%u:", party.count(), PARTY_SLOTS);
    for (int i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = party.slots[i];
      if (m.empty()) Serial.print(" -");
      else Serial.printf(" %s%s(nv%u)", DEX_TBL[m.dex].name, m.variant ? "*" : "", m.level);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "REG") {
    Serial.printf("digidex %u/%u:", pet.registeredCount(), DEX_COUNT);
    for (int i = 1; i <= DEX_COUNT; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    // PSRAM is where the sprites and the framebuffer live, so a memory problem
    // shows up here long before it shows up in the heap figure above.
    Serial.printf("psram=%u screen=%s btlspr=%d/%d\n", (unsigned)ESP.getFreePsram(),
                  SCREEN_NAME[uiCurrentScreen() % SCR_COUNT],
                  btlPmd[0].loaded ? 1 : 0, btlPmd[1].loaded ? 1 : 0);
    // The same two lines the periodic heartbeat prints. Typed on a board is when
    // you actually want them, and a console HEALTH that reported LESS than the
    // automatic one is § "A rule enforced in one path but not in its twin".
    Serial.printf("save=%s pending=%d\n", pet.saveHealthy() ? "ok" : "BROKEN",
                  pet.savePending() ? 1 : 0);
    nvsReport("health");
    gNvsLow = nvsLowOnSpace();
    Serial.println("DONE");
  } else if (line == "STATS") {
    // A stable per-board label, for the web installer's backup history so two
    // devices' saves can be told apart. The efuse MAC is fixed in silicon and
    // needs no storage. It is NOT a secret and NOTHING is authorised by it --
    // which is why the installer keeps its history in the browser rather than
    // on a server keyed by this.
    Serial.printf("board=%012llX\n", ESP.getEfuseMac());
    // Its own line: the installer matches board= as a whole line, and older pages must still.
    Serial.printf("fw=%s\n", FW_VERSION);
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u vit=%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.vitStat(), pet.berryKnown);
    Serial.printf("iv=%u/%u/%u/%u tr=%u/%u/%u topes=%u/%u/%u\n",
                  pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp,
                  pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.printf("variant=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.variant, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

// ---------- entrada tactil ----------

bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      lastInteract = millis();
      if (y < 72) leaveSack();       // tocar arriba = salir, conservando lo ganado
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    // A drag that STARTS on the rim is a scroll, not a swipe. Deciding it at
    // touch-down rather than on release is what keeps it unambiguous: the
    // finger never has to be told afterwards what it meant.
    rimActive = uiOnRim(x, y) && uiRimPages() > 1;
    rimA0 = uiRimAngle(x, y);
    rimSteps = 0;
    rimMoved = false;
    swallowGesture = (dimStage > 0) || screenOff || transferMode();  // waking or mid-transfer: no action
    if (screenOff) pet.setScreenOff(false);        // waking the screen wakes it
    screenOff = false;
    lastInteract = millis();
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    if (rimActive && !swallowGesture) {
      float d = uiRimAngle(x, y) - rimA0;
      while (d > 180.0f) d -= 360.0f;
      while (d < -180.0f) d += 360.0f;
      if (d > 6.0f || d < -6.0f) rimMoved = true;
      int want = (int)(d / RIM_STEP);
      while (rimSteps < want) { onRim(1); rimSteps++; }
      while (rimSteps > want) { onRim(-1); rimSteps--; }
    }
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    //
    // Gated on the MAIN screen, not on a hand-maintained list of screens to
    // exclude. That list had gallery/card/keyboard/clock on it and nothing
    // else, so the hold still fired on the party, box, boss, battle, player and
    // menu screens -- every one of which draws something inside inPetZone.
    // On the party screen it was genuinely dangerous: the grid overlaps the
    // zone, so holding a party slot opened "release the live pet?", and that
    // dialog's YES box sits on top of party slot 4. Hold a slot, tap where you
    // think a creature is, lose the creature you are actually raising.
    // One question with one answer, so a new screen cannot be forgotten.
    if (!holdFired && !swallowGesture && uiCurrentScreen() == SCR_MAIN && millis() - tStart > 3000 &&
        abs(tXl - tX0) < 30 && abs(tYl - tY0) < 30 && inPetZone(tX0, tY0) &&
        !pet.isEgg() && !confirmUntil && !pet.ceremony) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    lastInteract = millis();
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (rimMoved) { rimActive = false; wasPressed = pressed; return; }   // it was a scroll
    if (!holdFired && !swallowGesture) {
      if (abs(dx) > 80 && abs(dy) < 70 && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > 80 && abs(dx) < 70 && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < 40 && abs(dy) < 40) onTap(tX0, tY0);
    }
    rimActive = false;
  }
  wasPressed = pressed;
}

// deslizar vertical: DOWN is back, UP is deeper.
//
// One meaning per direction, on every screen. It used to be a per-screen
// grab-bag -- vertical closed some screens, changed version on others, and on
// the main screen down opened the player card while up opened the creature's.
// The player card is a tile now, so down is free to mean the one thing a round
// UI most needs and never had: a back gesture that works everywhere.

void openClock();  // prototipo

void onSwipeV(int dir) {
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight
  const bool back = dir > 0;

  // Modals and sheets: down backs out, up has nowhere deeper to go.
  if (pickOpen)     { if (back) pickOpen = false; return; }
  if (lanOpen)      { if (back) { lanLeave(); lanOpen = false; } return; }
  if (trainOpen)    { if (back) trainOpen = false; return; }
  if (bagOpen)      { if (back) bagOpen = false; return; }
  if (movePickOpen) { if (back) movePickOpen = false; return; }
  if (boxOpen)      { if (back) { boxOpen = false; boxSel = 0; } return; }
  // Either minigame exits on a swipe, keeping what was earned. A swipe cannot
  // be confused with a ball hit -- the gesture resolver separates them -- which
  // the header tap no longer could once the ball became hittable up there.
  if (gameOpen) { leaveGame(); return; }
  if (sackOpen) { leaveSack(); return; }
  if (spdOpen)  { leaveSpeed(); return; }
  if (memoOpen) { leaveMemo(); return; }
  if (kbOpen || pet.ceremony) return;
  if (clockOpen) {
    // The confirm is a level of depth: down dismisses it rather than leaving the
    // screen out from under it. A swipe must never be able to CONFIRM a wipe,
    // only decline one.
    if (resetConfirm) { if (back) resetConfirm = false; return; }
    if (back) clockOpen = false;
    return;
  }

  if (exploreOpen) { if (back) uiTileGo(TILE_PET); return; }

  // The two multi-version tiles are two levels deep: chooser, then ladder/grid.
  // DOWN walks back out of them one step at a time; UP keeps the version
  // shortcut, which is now a convenience rather than the only way in.
  if (gymOpen) {
    if (gymPick) { if (back) uiTileGo(TILE_PET); return; }
    if (back) { gymPick = true; rpickPage = 0; sfxPlay(SFX_TAP); return; }
    gymRegion = (uint8_t)((gymRegion + 1) % GYM_REGIONS);
    gymPage = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  if (galleryOpen) {
    if (galleryDetail) {
      if (back) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; }
      return;
    }
    if (galleryPick) { if (back) uiTileGo(TILE_PET); return; }
    if (back) {
      galleryPick = true; rpickPage = 0;
      galleryPmd.unload();
      galleryDirty = true;
      sfxPlay(SFX_TAP);
      return;
    }
    galleryRegion = (uint8_t)((galleryRegion + 1) % GAL_REGIONS);
    galleryPage = 0;
    galleryClampPage();
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (playerOpen) { if (back) uiTileGo(TILE_PET); return; }
  if (partyOpen) {
    if (partyDetail) { if (back) partyDetail = 0; return; }
    if (back) {
      if (partyPick) { partyPick = false; pet.clearEnded(); }
      uiTileGo(TILE_PET);
    }
    return;
  }
  if (cardOpen) { if (back) cardOpen = false; return; }

  // The pet tile itself. UP is its detail sheet; DOWN is already home.
  if (!back && !pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;
    cardPage = 0;
  }
}

// party screen: pick a slot (when a newcomer is waiting) or just leave
// A banked creature's sheet: its moves above all, since typing alone does not
// tell you whether that WHAMON still has ICE BEAM -- and in hard mode that is
// what decides the fight.
// The detail sheet, shared by the party and the box so the two cannot drift.
// `fromBox` picks which action the LEFT button offers; the right one is always
// RELEASE, which is irreversible and therefore always asks first.
void renderMonSheet(const PartyMon &m, bool fromBox) {
  const DexEntry &d = DEX_TBL[m.dex];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[36];
  snprintf(head, sizeof(head), "%s%s Lv.%u", m.variant ? "*" : "",
           m.nick[0] ? m.nick : L(d.name), (unsigned)m.level);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(head) * 6, 40);
  gfx->print(head);
  char ty[24];
  if (d.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(d.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(d.type1), typeName(d.type2));
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(ty) * 3, 64);
  gfx->print(ty);

  for (int i = 0; i < MOVE_SLOTS; i++)
    drawMoveRow(78 + i * 52, m.moves[i], false, m.dex);

  char st[64];   // the stat labels are localized: Chinese ones are 6 bytes each
  snprintf(st, sizeof(st), "%s %u  %s %u  %s %u  %s %u",
           T(S_STAT_ATK), party.atkOf(m), T(S_STAT_DEF), party.defOf(m),
           T(S_STAT_SPE), party.speOf(m), T(S_STAT_VIT), party.vitOf(m));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(st) * 3, 300);
  gfx->print(st);

  // Which creature you are RAISING is now a choice you can make at any time.
  // It is a true exchange -- the one on the main screen takes the slot this one
  // vacates -- so unlike the old frozen BRING BACK it needs no free slot and no
  // "only while an egg waits" rule. Both keep their full care state, so nothing
  // is reset by swapping and swapping back.
  //
  // A box creature still goes to the party first: one rule per screen, and the
  // swap is then offered from the party sheet like any other member.
  bool leftOk = fromBox || (pet.ceremony == CER_NONE && !pet.awaitingStarter());
  const char *leftLbl = fromBox ? T(S_BOX_TAKE) : T(S_FOCUS);
  const char *rightLbl = fromBox ? T(S_RELEASE_BTN) : T(S_BOX_PUT);
  gfx->fillRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10,
                     leftOk ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10, UI_INK);
  gfx->setTextColor(leftOk ? UI_BG_DAY : 0x8410);
  int leftSize = tcols(leftLbl) * 12 <= PDET_L_W - 8 ? 2 : 1;
  gfx->setTextSize(leftSize);
  gfx->setCursor(PDET_L_X + PDET_L_W / 2 - (int)tcols(leftLbl) * 3 * leftSize,
                 PDET_BTN_Y + PDET_BTN_H / 2 - 4 * leftSize);
  gfx->print(leftLbl);

  gfx->fillRoundRect(PDET_R_X, PDET_BTN_Y, PDET_R_W, PDET_BTN_H, 10, UI_TRACK);
  gfx->drawRoundRect(PDET_R_X, PDET_BTN_Y, PDET_R_W, PDET_BTN_H, 10,
                     fromBox ? UI_BAR_BAD : UI_INK);
  gfx->setTextColor(UI_INK);
  int rightSize = tcols(rightLbl) * 12 <= PDET_R_W - 8 ? 2 : 1;
  gfx->setTextSize(rightSize);
  gfx->setCursor(PDET_R_X + PDET_R_W / 2 - (int)tcols(rightLbl) * 3 * rightSize,
                 PDET_BTN_Y + PDET_BTN_H / 2 - 4 * rightSize);
  gfx->print(rightLbl);

  if (!leftOk) {
    const char *why = fromBox ? T(S_PARTY_FULL) : T(S_FOCUS_NOW);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(why) * 3, PDET_BTN_Y - 14);
    gfx->print(why);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 404);
  gfx->print(T(S_BACK));

  // Asked before it happens, because nothing gets this creature back: it is not
  // a farewell, it does not join anything, and there is no undo.
  if (releaseConfirm) {
    char q[40];
    snprintf(q, sizeof(q), T(S_RELEASE_FMT), m.nick[0] ? m.nick : L(d.name));
    drawConfirmPanel(q, T(S_RELEASE_GONE), nullptr, UI_BAR_BAD,
                     T(S_RELEASE_BTN), UI_BAR_BAD, UI_WHITE,
                     T(S_NO), UI_TRACK, UI_INK);
  }
  gfx->flush();
}

void renderPartyDetail() {
  // An empty slot means the creature was just let go. Fall through to the grid
  // rather than returning: a bare return draws nothing AND never flushes, which
  // leaves the previous frame frozen on the panel -- the exact failure
  // flush_test exists to catch, and invisible to any screenshot.
  if (party.slots[partyDetail - 1].empty()) {
    partyDetail = 0;
    releaseConfirm = false;
    renderParty();
    return;
  }
  renderMonSheet(party.slots[partyDetail - 1], false);
}

void renderBoxDetail() {
  if (party.box[boxDetail - 1].empty()) {
    boxDetail = 0;
    releaseConfirm = false;
    renderBox();
    return;
  }
  renderMonSheet(party.box[boxDetail - 1], true);
}

// The two buttons' hit areas, so a test can prove they do not overlap without
// copying the geometry -- a test that restates the numbers drifts from them.
// Every primary button's height, so a test can hold them all to UI_TAP_MIN
// instead of waiting for somebody to report the next one by hand.
void uiButtonHeights(int *out, int max, int *n) {
  // What a control is worth is what you can HIT, not what it draws: the home
  // icons are a 72 px graphic inside a 94 px disc.
  const int h[] = { PSLOT_HUB_R * 2, PSLOT_R * 2, PARTYCLOSE_H, LANBTN_H,
                    BTL_CELL_H + BTL_HIT_PAD * 2,
                    EXPLORE_BTN_H + EXPLORE_BTN_PAD * 2, BTN_HIT * 2,
                    // The memory game's pads. COMPACT controls, like everything
                    // else here -- a disc you aim at, so height is the whole
                    // story. Full-width list rows are a different shape and are
                    // held to a different rule; see uiRowHeights() below.
                    MEMO_R * 2 };
  int c = (int)(sizeof(h) / sizeof(h[0]));
  if (c > max) c = max;
  for (int i = 0; i < c; i++) out[i] = h[i];
  if (n) *n = c;
}

// FULL-WIDTH LIST ROWS, which are a different class from the discs and pills
// above and have to be judged differently.
//
// A row spans the whole panel, so at 44 px tall it is still a ~276 x 44 target --
// far easier to hit than a 44 px button, and nothing about it is ambiguous
// because there is nothing beside it to hit instead. They also CANNOT reach
// UI_TAP_FINGER where they live: five settings rows at 94 px would need 520 px on
// a 466 px panel, and four training rows would need 416 before the header. So
// they are held to the hard 44 px floor and not to the finger floor.
//
// Published so that floor is enforced rather than assumed -- the training menu's
// pitch was just retuned to fit a fourth row, which is exactly the change that
// could have quietly dropped it under.
void uiRowHeights(int *out, int max, int *n) {
  const int h[] = { MENU_ROW_H, TRAIN_ROW_H, SET_ROW_H, GOAL_ROW_H, GYM_ROW_H };
  int c = (int)(sizeof(h) / sizeof(h[0]));
  if (c > max) c = max;
  for (int i = 0; i < c; i++) out[i] = h[i];
  if (n) *n = c;
}

// The boss list's difficulty pill against its first boss row. Enlarging the
// pill to a real tap target once pushed it straight over that row -- the third
// overlap of this kind, after BOX/CLOSE and the battle grid against BACK.
// Which home icon is the only one live while the pet sleeps, and where it is.
// Exposed so a test can prove it is the LIGHT: removing an icon once shifted
// every index and quietly made the BATH button the wake-up button.
// Asleep, the LIGHT is the only live icon -- it is what wakes the pet. Both the
// draw path and the tap path ask THIS, so a greyed button can never still be
// tappable and the greying can never point at the wrong icon.
bool uiButtonDisabled(int i) { return pet.sleeping && i != BTN_LIGHT; }

int uiSleepButton(int *cx, int *cy) {
  if (cx) *cx = buttons[BTN_LIGHT].cx;
  if (cy) *cy = buttons[BTN_LIGHT].cy;
  return BTN_LIGHT;
}

void uiButtonAt(int i, int *cx, int *cy, int *half) {
  if (i < 0 || i >= BTN_COUNT) return;
  if (cx) *cx = buttons[i].cx;
  if (cy) *cy = buttons[i].cy;
  if (half) *half = BTN_HALF;
}

void gymHeaderRects(int *pillTop, int *pillBot, int *rowTop) {
  if (pillTop) *pillTop = GYMDIF_Y;
  if (pillBot) *pillBot = GYMDIF_Y + GYMDIF_H;
  if (rowTop) *rowTop = GYM_ROW_Y(0);
}

void partyButtonRects(int *boxTop, int *boxBot, int *closeTop, int *closeBot) {
  if (boxTop) *boxTop = PSLOT_CY - PSLOT_HUB_R;
  if (boxBot) *boxBot = PSLOT_CY + PSLOT_HUB_R;
  if (closeTop) *closeTop = PARTYCLOSE_Y;
  if (closeBot) *closeBot = PARTYCLOSE_Y + PARTYCLOSE_H;
}

// Which of the detail sheet's two buttons a tap landed on. One answer for the
// party and the box, off the same PDET_* constants the sheet is DRAWN with, so
// the graphic and the hit area cannot drift apart.
bool monSheetBtn(int16_t x, int16_t y, bool left) {
  if (y < PDET_BTN_Y || y > PDET_BTN_Y + PDET_BTN_H) return false;
  int x0 = left ? PDET_L_X : PDET_R_X;
  int w = left ? PDET_L_W : PDET_R_W;
  return x >= x0 && x <= x0 + w;
}

// YES / NO on the release confirm. Returns true when the tap was consumed. The
// caller swallows everything else while it is up: a modal that leaks a miss
// through to what is drawn underneath is precisely how an irreversible action
// gets triggered by a fumbled tap, which is the shape CLAUDE.md section 4 keeps
// warning about.
bool monSheetConfirmTap(int16_t x, int16_t y, bool fromBox) {
  int c1t, c1b, c2t, c2b;
  uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
  if (x < CONFIRM_BTN_X || x > CONFIRM_BTN_X + CONFIRM_BTN_W) return false;
  if (y >= c1t && y <= c1b) {            // YES -- and it does not come back
    if (fromBox) { party.boxReleaseAt(boxDetail - 1); boxDetail = 0; }
    else { party.releaseAt(partyDetail - 1); partyDetail = 0; }
    // A half-finished swap named a slot that may now be empty, so disarm both
    // sides rather than leaving one pointing at a creature that is gone.
    boxSwapFrom = 0;
    boxSel = 0;
    releaseConfirm = false;
    sfxPlay(SFX_BYE);
    return true;
  }
  if (y >= c2t && y <= c2b) {            // NO
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return true;
  }
  return false;
}

// The six party/box slots on a RING rather than a 2x3 grid; the geometry is
// declared with the other party layout constants above.
void partySlotPos(int i, int *cx, int *cy) {
  float a = (-120.0f + 60.0f * (float)i) * 0.01745329f;   // clockwise from top-left
  if (cx) *cx = CX + (int)(cosf(a) * PSLOT_RING);
  if (cy) *cy = PSLOT_CY + (int)(sinf(a) * PSLOT_RING);
}

// Which slot a tap landed in, or -1. The draw path and both tap paths ask this
// one function; the grid they replace had its rectangle written out in three
// places, which is how the party screen's controls drifted apart before.
int partySlotAt(int16_t x, int16_t y) {
  for (int i = 0; i < PARTY_SLOTS; i++) {
    int sx, sy;
    partySlotPos(i, &sx, &sy);
    int dx = x - sx, dy = y - sy;
    if (dx * dx + dy * dy <= PSLOT_R * PSLOT_R) return i;
  }
  return -1;
}

bool partyHubAt(int16_t x, int16_t y) {
  int dx = x - CX, dy = y - PSLOT_CY;
  return dx * dx + dy * dy <= PSLOT_HUB_R * PSLOT_HUB_R;
}

// Exposed so hit_test can hold every control to the finger floor without
// keeping its own copy of the number it is enforcing.
int uiTapFinger() { return UI_TAP_FINGER; }

// The home icons' TAP radius, which is larger than the graphic they draw.
int uiButtonHitR() { return BTN_HIT; }

void partyTap(int16_t x, int16_t y) {
  if (boxOpen) { boxTap(x, y); return; }
  // The SHEET IS CHECKED FIRST, and that is not cosmetic ordering. It covers the
  // whole screen, and its action row overlaps the BOX button's hit area below.
  // BOX button's hit area (x138..328, y312..372) below -- so with BOX tested
  // first, tapping RELEASE opened the box instead. Nothing behind a full-screen
  // sheet may answer a tap.
  if (partyDetail) {
    // The confirm is modal: while it is up nothing else on the sheet responds,
    // or a miss on YES would fall through to the move rows underneath it.
    if (releaseConfirm) { monSheetConfirmTap(x, y, false); return; }
    if (monSheetBtn(x, y, true)) {           // MAKE ACTIVE
      if (pet.ceremony != CER_NONE || pet.awaitingStarter()) { sfxPlay(SFX_DENY); return; }
      focusSwap((uint8_t)(partyDetail - 1));
      partyDetail = 0;
      boxSwapFrom = 0;
      partyOpen = false;
      sfxPlay(SFX_HATCH);
      return;
    }
    if (monSheetBtn(x, y, false)) {          // TO BOX
      partyDetail = 0;
      boxOpen = true;
      boxPage = 0;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a move to change it
      int ry = 78 + i * 52;
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      movePickParty = partyDetail;
      movePickSlot = i;
      movePickPage = 0;
      movePickOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    partyDetail = 0;
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  if (!partyPick && partyHubAt(x, y)) {
    boxOpen = true;                  // open the box, nothing picked yet
    boxPage = 0;
    boxSwapFrom = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  // exit button, and the top band, both always work
  if ((y >= PARTYCLOSE_Y && y <= PARTYCLOSE_Y + PARTYCLOSE_H &&
       x >= PARTYCLOSE_X && x <= PARTYCLOSE_X + PARTYCLOSE_W) || y < 34) {
    if (partyPick) {                 // declined the swap: the pet is let go
      partyPick = false;
      pet.clearEnded();
    }
    partyOpen = false;
    sfxPlay(SFX_TAP);
    return;
  }
  for (int i = 0; i < PARTY_SLOTS; i++) {
    if (partySlotAt(x, y) != i) continue;
    if (boxSel) {                    // a box creature is waiting for a slot
      party.swapPartyBox(i, boxSel - 1);
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (!partyPick) {
      if (boxSwapFrom == i + 1) {    // tapped again: take it to the box
        boxOpen = true;
        boxPage = 0;
        sfxPlay(SFX_TAP);
        return;
      }
      if (party.slots[i].empty()) { boxSwapFrom = i + 1; sfxPlay(SFX_TAP); return; }
      partyDetail = i + 1;
      boxSwapFrom = i + 1;           // armed, in case the box is opened next
      sfxPlay(SFX_TAP);
      return;
    }
    party.replaceAt(i, pet.endedMon);
    snprintf(partyBannerName, sizeof(partyBannerName), "%s",
             pet.endedMon.nick[0] ? pet.endedMon.nick : L(DEX_TBL[pet.endedMon.dex].name));
    partyBannerUntil = millis() + 3500;
    pet.clearEnded();
    partyPick = false;
    partyOpen = false;
    sfxPlay(SFX_MEDAL);
    return;
  }
}

// deslizar: dir +1 = hacia la derecha
//
// HORIZONTAL IS THE TILE AXIS, and nothing else. Every screen used to keep its
// own opinion in here -- some paged, some closed, and several paged and THEN
// closed off the end, which is one gesture meaning two things and is exactly
// how the same paging bug shipped four times. Paging moved to the rim
// (onRim), which is what frees this axis to mean one thing.
void onSwipe(int dir) {
  // First boot has no axis yet -- nothing but the version chooser exists -- so
  // horizontal still pages it there. The rim is not a gesture anyone has been
  // taught on the first screen they ever see.
  if (pet.awaitingStarter()) { rpickSwipe(dir); return; }
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight

  int t = uiTileIndex();
  if (t >= 0) {
    // A sheet drawn on top of a tile closes first; the axis is underneath it.
    if (galleryDetail) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; return; }
    if (partyDetail) { partyDetail = 0; return; }
    if (partyPick) { partyPick = false; pet.clearEnded(); }
    if (pet.ceremony || confirmUntil) return;
    int n = t - dir;   // the content follows the finger
    // IT BUMPS. This is the whole point of the axis: a horizontal swipe can no
    // longer close anything, so it cannot be confused with paging.
    if (n < 0 || n >= TILE_COUNT) { sfxPlay(SFX_DENY); return; }
    uiTileGo(n);
    return;
  }

  // Off the axis are modals -- pickers, sheets, minigames. None of them page
  // any more, so here horizontal simply backs out.
  if (pickOpen) { pickOpen = false; return; }
  if (trainOpen) { trainOpen = false; return; }
  if (bagOpen) { bagOpen = false; return; }
  if (movePickOpen) { movePickOpen = false; return; }
  if (boxOpen) { boxOpen = false; boxSel = 0; return; }
  if (gameOpen) { leaveGame(); return; }   // swipe out, keeping what you earned
  if (spdOpen) { leaveSpeed(); return; }
  if (memoOpen) { leaveMemo(); return; }
  if (kbOpen || clockOpen) return;
  if (cardOpen) { cardOpen = false; return; }
}

void onTap(int16_t x, int16_t y) {
  if (pet.awaitingStarter()) {  // primera partida: version y luego inicial
    if (!starterRegionDone) {
      int r = regionPickTap(x, y, RPICK_FOR_START);
      if (r >= 0) {
        pet.setRegion((uint8_t)r);   // the version picked here is where eggs come from too
        starterRegionDone = true;
        sfxPlay(SFX_TAP);
        // A version hatches ONE Baby I, so there is nothing left to choose:
        // its Digi-Egg appears straight away. The mixed pool (ALL) picks one
        // of the drawable Baby Is at random -- the surprise is the point.
        {
          const RegionInfo &rg = REGIONS[pet.region % REGION_COUNT];
          int16_t ok[8]; uint8_t n = 0;
          for (uint8_t i = 0; i < rg.starterCount && n < 8; i++)
            if (speciesShowable(rg.starters[i])) ok[n++] = rg.starters[i];
          if (n) pet.chooseStarter(ok[random(n)]);
        }
      }
      return;
    }
    for (int i = 0; i < starterCountShown(pet.region); i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= 70 && x <= 396 && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(starterOf(pet.region, (uint8_t)i));
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (battleOpen) {
    battleTap(x, y);
    return;
  }
  if (exploreOpen) {
    int8_t regionStep = exploreRegionHit(x, y);
    if (regionStep) {
      exploreStepRegion(regionStep);
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= EXPLORE_DIF_Y && y <= EXPLORE_DIF_Y + EXPLORE_DIF_H) {
      exploreHard = !exploreHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= EXPLORE_BTN_X - EXPLORE_BTN_PAD &&
      x <= EXPLORE_BTN_X + EXPLORE_BTN_W + EXPLORE_BTN_PAD &&
      y >= EXPLORE_BTN_Y - EXPLORE_BTN_PAD &&
      y <= EXPLORE_BTN_Y + EXPLORE_BTN_H + EXPLORE_BTN_PAD) {
      if (!startWildBattle(currentExploreRegion(), exploreHard)) { sfxPlay(SFX_DENY); return; }
      exploreOpen = false;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (bagOpen) {
    bagTap(x, y);
    return;
  }
  if (playerOpen) {
    if (playerPage == 0 && y >= 32 && y < 68) {   // the name: rename yourself
      openKeyboardFor(KB_TRAINER);
      sfxPlay(SFX_TAP);
      return;
    }
    // the avatar is the only other live target; everything else backs out
    if (playerPage == 0 && x > CX - 40 && x < CX + 40 && y > 70 && y < 146) {
      pet.avatar = (uint8_t)((pet.avatar + 1) % AVATAR_COUNT);
      pet.saveNow();
      sfxPlay(SFX_TAP);
      return;
    }
    playerOpen = false;
    return;
  }
  if (pickOpen) {
    pickTap(x, y);
    return;
  }
  if (lanOpen) {
    lanTap(x, y);
    return;
  }
  if (gymOpen && gymPick) {
    int r = regionPickTap(x, y, RPICK_FOR_GYMS);
    if (r >= 0) {
      gymRegion = (uint8_t)r;
      gymPage = 0;
      gymPick = false;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= LANBTN_Y && y <= LANBTN_Y + LANBTN_H &&
        x >= LANBTN_X && x <= LANBTN_X + LANBTN_W) {   // LAN battle
      gymOpen = false; gymPick = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) { gymOpen = false; gymPick = false; }
    return;
  }
  if (gymOpen) {
    if (y >= GYMDIF_Y && y <= GYMDIF_Y + GYMDIF_H) {   // the difficulty pill
      gymHard = !gymHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= GYMBTN_X && x <= GYMBTN_X + GYMBTN_W &&
        y >= GYMBTN_Y && y <= GYMBTN_Y + GYMBTN_H) {
      gymOpen = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < GYM_ROWS; i++) {
      uint8_t idx = gymPage * GYM_ROWS + i;
      if (idx >= TRAINER_COUNT) break;
      int rx, ry, rw, rh;
      gymRowRect(i, &rx, &ry, &rw, &rh);
      if (x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
      if (!gymUnlocked(idx, gymHard)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      gymOpen = false;
      pickTrainer = idx;
      pickHard = gymHard;
      pickRegion = gymRegion;     // BEFORE squadCap: it reads pickRegion now
      pickPage = 0;
      pickDefault(squadCap(idx, gymHard));
      pickOpen = true;
      return;
    }
    gymOpen = false;
    return;
  }
  if (pet.hasLearnOffer()) {
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int ry = LEARN_ROW_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      pet.acceptLearn(i);
      return;
    }
    if (x >= 70 && x <= 396 && y >= LEARN_SKIP_Y && y <= LEARN_SKIP_Y + 44) {
      sfxPlay(SFX_TAP);
      pet.declineLearn();
    }
    return;   // modal: nothing else on screen responds until it is answered
  }
  if (trainOpen) {
    bool inPanel = (x >= TRAIN_X && x <= TRAIN_X + TRAIN_W &&
                    y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H);
    if (!inPanel) { trainOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < TRAIN_ROWS; i++) {
      int ry = TRAIN_ROW_Y(i);
      if (x < TRAIN_X + 18 || x > TRAIN_X + TRAIN_W - 18) continue;
      if (y < ry || y > ry + TRAIN_ROW_H) continue;
      sfxPlay(SFX_TAP);
      trainOpen = false;
      if (i == TRAIN_ROW_ATK) startSack();
      else if (i == TRAIN_ROW_SPE) startSpeedGame();
      else if (i == TRAIN_ROW_DEF) startGame();   // the ball game trains DEF
      else startMemoGame();                       // and this one trains neither
      return;
    }
    return;
  }
  // The menu is modal and has three independent ways out: the CLOSE row, a tap
  // anywhere on the dimmed area outside the panel, and any swipe (see onSwipe).
  // Deliberately no timeout: a menu that vanishes while you read it is worse
  // than one that lingers.
  if (menuOpen) {
    bool inPanel = (x >= MENU_X && x <= MENU_X + MENU_W &&
                    y >= MENU_Y && y <= MENU_Y + MENU_H);
    if (!inPanel) { menuOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < MENU_ROWS; i++) {
      int ry = MENU_ROW_Y(i);
      if (x < MENU_X + 18 || x > MENU_X + MENU_W - 18) continue;
      if (y < ry || y > ry + MENU_ROW_H) continue;
      sfxPlay(SFX_TAP);
      menuOpen = false;
      if (i == 0) { cardOpen = true; cardPage = 1; }   // straight to the stats page
      else if (i == 1) { bagOpen = true; bagPage = 0; }
      else if (i == 2) { galleryOpen = true; galleryPick = true; galleryPage = 0; rpickPage = 0; galleryDetail = 0; galleryDirty = true; }
      else if (i == 3) { openClock(); }
      else if (i == 4) {
        if (!pet.canRetireNow()) { sfxPlay(SFX_DENY); return; }
        choiceKind = 3; choiceUntil = millis() + 12000;
      }
      return;                                     // i == 5 is CLOSE: just shut
    }
    return;
  }
  if (movePickOpen) {
    uint8_t all[64];
    uint8_t n = learnableList(all, sizeof(all));
    for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
      uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
      if (idx >= n) break;
      int ry = MOVE_PICK_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      // Swapping for a move already in another slot would silently duplicate
      // it, so trade the two slots instead of overwriting.
      uint8_t *tgt = pickTargetMoves();
      for (int s = 0; s < MOVE_SLOTS; s++)
        if (tgt[s] == all[idx] && s != movePickSlot) tgt[s] = tgt[movePickSlot];
      tgt[movePickSlot] = all[idx];
      if (movePickParty) party.save(); else pet.saveNow();
      movePickOpen = false;
      return;
    }
    movePickOpen = false;   // tap anywhere else = back to the moves page
    return;
  }
  if (partyOpen) {
    partyTap(x, y);
    return;
  }
  if (galleryOpen) {
    if (galleryPick) {
      int r = regionPickTap(x, y, RPICK_FOR_DEX);
      if (r >= 0) {
        galleryRegion = (uint8_t)r;
        galleryPage = 0;
        galleryClampPage();
        galleryDetail = 0;
        galleryDirty = true;
        galleryPick = false;
        sfxPlay(SFX_TAP);
      } else if (y > 380) {
        galleryOpen = false;
      }
      return;
    }
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (cardPage == 0 && y < 84) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 2) {
      for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a slot to change it
        int ry = MOVE_ROW_Y(i);
        if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
        sfxPlay(SFX_TAP);
        movePickParty = 0;      // the live pet
        movePickSlot = i;
        movePickPage = 0;
        movePickOpen = true;
        return;
      }
      cardOpen = false;            // anywhere else on the page still exits
    } else {
      cardOpen = false;
    }
    return;
  }
  if (spdOpen) {
    spdTap(x, y);
    return;
  }
  if (memoOpen) {
    memoTap(x, y);
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    int c1t, c1b, c2t, c2b;
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    const bool inX = (x >= CONFIRM_BTN_X && x <= CONFIRM_BTN_X + CONFIRM_BTN_W);
    bool b1 = inX && y >= c1t && y <= c1b;   // accion
    bool b2 = inX && y >= c2t && y <= c2b;   // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.variant); }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 3) {          // retirada a peticion
      if (b1) pet.startRetire();
      // b2 is simply "no": nothing to decline, the row is always there
    } else if (choiceKind == 2) {          // despedida
      if (b1) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (millis() < confirmUntil && x >= 118 && x <= 218 && y >= 252 && y <= 304) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (millis() < feedMenuUntil && y >= FEED_MENU_Y && y <= FEED_MENU_Y + FEED_MENU_H &&
        x >= FEED_MENU_X && x <= FEED_MENU_X + FEED_MENU_W) {
      int item = (x - FEED_MENU_X) / (FEED_MENU_W / 4);
      if (item > 3) item = 3;
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    // the version pill first, or choosing a version would also crack the egg --
    // and a near miss is swallowed rather than counted, since three taps hatch
    if (eggRegionTap(x, y)) return;
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // botones de final (mismo recuadro): escapada directa; despedida abre dialogo.
  //
  // The runaway does NOT ask, deliberately. A pet you have to authorise to
  // leave is not really at stake, and neglect having teeth is the whole premise.
  // What was actually wrong is that a night's sleep could reach this state at
  // all -- fixed where it belongs, in the drain, by having the creature put
  // itself to bed (Pet::tick, autoSleep).
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
    if (pet.wantFarewellButton()) { choiceKind = 2; choiceUntil = millis() + 12000; return; }
  }
  for (int i = 0; i < BTN_COUNT; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      if (uiButtonDisabled(i)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (i == BTN_FOOD) feedMenuUntil = millis() + 6000;
      else if (i == BTN_LIGHT) pet.toggleLight();
      else if (i == BTN_BATH) startBath();
      else trainOpen = true;
      return;
    }
  }
  // tapping the name/status band opens the menu. This band was inert before,
  // and it sits clear of inPetZone (which starts at y 95).
  if (y >= 28 && y < 94) {
    menuOpen = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    pet.caress();
    if (!pet.sleeping) sfxPlay(SFX_HEART);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)((now / 50 + k * 250) % 560) - 40;
    int cy = 70 + k * 34;
    gfx->fillCircle(cx, cy, 16, col);
    gfx->fillCircle(cx + 18, cy + 3, 13, col);
    gfx->fillCircle(cx - 15, cy + 4, 12, col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }  // amanecer
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }  // dia
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }  // atardecer

  // cielo en bandas
  for (int y = 0; y < HORIZON; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, HORIZON));

  // sol o luna
  if (night) {
    gfx->fillCircle(360, 78, 24, C565(0xe8, 0xee, 0xf5));
    gfx->fillCircle(370, 72, 22, lerp565(top, bot, 78, HORIZON));  // creciente
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  } else if (h < 18) {
    gfx->fillCircle(360, 84, 26, h < 8 ? C565(0xff, 0xd9, 0x8a) : C565(0xff, 0xe7, 0x9f));
    drawClouds(now, C565(0xff, 0xff, 0xff));
  } else {
    gfx->fillCircle(233, HORIZON - 6, 34, C565(0xff, 0xf1, 0xc8));  // sol poniente
  }

  // mar de la playa: una franja de agua sobre la arena
  uint16_t soil = BIOME_SOIL[biome < 6 ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  if (biome == 1) {
    uint16_t sea = night ? C565(0x1c, 0x34, 0x52) : C565(0x4f, 0x96, 0xc4);
    gfx->fillRect(0, HORIZON - 26, 466, 26, sea);
    for (int i = 0; i < 3; i++) {
      int wy = HORIZON - 22 + i * 7;
      uint16_t fc = night ? C565(0x3a, 0x58, 0x78) : C565(0xbf, 0xe6, 0xf5);
      gfx->fillRect(60 + ((now / 60 + i * 30) % 60), wy, 26, 2, fc);
      gfx->fillRect(300 - ((now / 60 + i * 20) % 60), wy, 26, 2, fc);
    }
  }

  // suelo
  gfx->fillRect(0, HORIZON, 466, 466 - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-60, HORIZON - 14, 586, 60, 30, hill);

  // detalles del bioma
  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  if (biome == 2) {  // bosque: coniferas en silueta
    for (int tx : { 60, 150, 360, 416 }) {
      gfx->fillTriangle(tx, HORIZON - 46, tx - 16, HORIZON, tx + 16, HORIZON, dk);
      gfx->fillTriangle(tx, HORIZON - 60, tx - 12, HORIZON - 28, tx + 12, HORIZON - 28, dk);
    }
  } else if (biome == 3) {  // volcan: rocas y brasas
    gfx->fillTriangle(70, HORIZON, 40, HORIZON + 30, 100, HORIZON + 30, dk);
    gfx->fillTriangle(400, HORIZON + 4, 372, HORIZON + 30, 430, HORIZON + 30, dk);
    if (!night)
      for (int e = 0; e < 4; e++)
        gfx->fillRect(120 + e * 70, HORIZON + 8 + (e % 2) * 6, 4, 4, C565(0xff, 0x9b, 0x3a));
  } else if (biome == 4) {  // montana: cumbres al fondo
    gfx->fillTriangle(140, HORIZON - 50, 60, HORIZON, 220, HORIZON, dk);
    gfx->fillTriangle(330, HORIZON - 38, 250, HORIZON, 410, HORIZON, dk);
  } else if (biome == 5 && !night) {  // nieve: copos cayendo
    for (int f = 0; f < 10; f++) {
      int fx = (f * 53 + now / 40) % 466;
      int fy = (f * 90 + now / 18) % HORIZON;
      gfx->fillRect(fx, fy, 3, 3, UI_WHITE);
    }
  } else if (biome == 0) {  // pradera: matas de hierba
    for (int gx : { 80, 175, 300, 395 })
      for (int b = -1; b <= 1; b++)
        gfx->fillRect(gx + b * 5, HORIZON + 6, 2, 8 + (b == 0 ? 4 : 0), dk);
  }
}

// primera partida: elige inicial entre BOTAMON / AGUMON / BETAMON
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(t) * 6, 68);
  gfx->print(t);
  for (int i = 0; i < starterCountShown(pet.region); i++) {
    int16_t d = starterOf(pet.region, i);
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(178, ry + 24);
    gfx->print(L(de.name));
  }
  gfx->flush();
}

// ---------- crash breadcrumbs ----------
//
// "It has a mini crash" is not a bug report you can act on, because the board
// never says WHY it restarted. These three live in RTC memory, which survives
// a panic, a watchdog and a software reset -- everything except pulling the
// power -- so the next boot can say what the firmware was doing when it died.
//
// Written every frame. That is one word to RTC RAM per render, which costs
// nothing next to a full-screen redraw, and it means the crumb always names
// the screen that was actually on the panel rather than the last one opened.
#define CRUMB_MAGIC 0x7AABE10C
RTC_NOINIT_ATTR uint32_t gCrumbMagic;
RTC_NOINIT_ATTR uint32_t gCrumbScreen;
RTC_NOINIT_ATTR uint32_t gCrumbHeap;


// Which screen is on the panel RIGHT NOW, in the same order render() tests.
uint8_t uiCurrentScreen() {
  if (transferMode()) return SCR_TRANSFER;
  if (pet.awaitingStarter()) return starterRegionDone ? SCR_STARTER : SCR_REGION;
  if (galleryOpen) return galleryPick ? SCR_DEXPICK : SCR_GALLERY;
  if (movePickOpen) return SCR_MOVEPICK;
  if (partyOpen) return boxOpen ? SCR_BOX : SCR_PARTY;
  if (bagOpen) return SCR_BAGSCR;
  if (kbOpen) return SCR_KEYBOARD;
  if (cardOpen) return SCR_CARD;
  if (playerOpen) return SCR_PLAYER;
  if (clockOpen) return SCR_CLOCK;
  if (btlWinUntil) return SCR_WIN;
  if (battleOpen) return SCR_BATTLE;
  if (pickOpen) return SCR_PICK;
  if (lanOpen) return SCR_LAN;
  if (exploreOpen) return SCR_EXPLORE;
  if (gymOpen) return gymPick ? SCR_GYMPICK : SCR_GYM;
  if (pet.hasLearnOffer()) return SCR_LEARN;
  if (gameOpen || sackOpen || spdOpen || memoOpen) return SCR_GAME;
  if (trainOpen) return SCR_TRAIN;
  if (menuOpen) return SCR_MENU;
  return SCR_MAIN;
}

// ---------------------------------------------------------------------------
// The tile axis and the rim.
//
// Two rules replace the pile of per-screen opinions that used to live in
// onSwipe(), and between them they close the bug this project shipped FOUR
// times -- a paged screen that CLOSED instead of paging:
//
//   HORIZONTAL is the tile axis. Six peer screens with the pet near the middle,
//   and it BUMPS at the ends. It can no longer close anything, so the gesture
//   that means "next page" and the gesture that means "exit" are different
//   gestures, which they never were before.
//
//   THE RIM is paging. A drag that starts outside RIM_R0 pages whatever is in
//   front of you, and CLAMPS at both ends. This is what frees the horizontal
//   axis -- A and C only work as a pair, and this is the joint.
//
// Vertical stays depth: up opens, down goes back.

// A tile's screen, and the chooser it opens on where it has one. Both belong to
// the same tile: the version choosers are not a level of depth, they ARE the boss
// and Digidex tiles until a version is picked. One table, so uiTileIndex() and
// uiTileGo() cannot end up with different ideas of what is on the axis.
//
// PLAYER . PARTY . [PET] . EXPLORE . GYM . DEX -- yours on the left, the world
// on the right. Exploration is a primary loop, not a boss action, so it gets the
// first world-facing stop and the main screen can name it directly.
struct TileDef { uint8_t scr, alt; };
static const TileDef TILE[TILE_COUNT] = {
  { SCR_PLAYER, SCR_PLAYER }, { SCR_PARTY, SCR_PARTY }, { SCR_MAIN, SCR_MAIN },
  { SCR_EXPLORE, SCR_EXPLORE }, { SCR_GYM, SCR_GYMPICK },
  { SCR_GALLERY, SCR_DEXPICK },
};

int uiTileIndex() {
  uint8_t s = uiCurrentScreen();
  for (int i = 0; i < TILE_COUNT; i++)
    if (TILE[i].scr == s || TILE[i].alt == s) return i;
  return -1;   // not on the axis: a modal, a minigame, a fight
}

// Jump to tile i. Every stop on the axis is a top-level screen, so this closes
// the others rather than stacking on them.
void uiTileGo(int i) {
  if (i < 0) i = 0;
  if (i >= TILE_COUNT) i = TILE_COUNT - 1;
  exploreOpen = gymOpen = partyOpen = galleryOpen = playerOpen = false;
  boxOpen = false;
  boxDetail = partyDetail = 0;
  galleryDetail = 0;
  switch (TILE[i].scr) {
    // The two multi-version screens land on their CHOOSER, never on whichever
    // version was last set. Opening straight into it is how Ver.2 and Ver.3 came
    // to be built, reachable, and completely invisible.
    case SCR_GYM: gymOpen = true; gymPick = true; gymPage = 0; rpickPage = 0; break;
    case SCR_EXPLORE: exploreOpen = true; break;
    case SCR_PARTY: partyOpen = true; break;
    case SCR_GALLERY:
      galleryOpen = true; galleryPick = true; galleryPage = 0; rpickPage = 0;
      galleryDirty = true;
      break;
    case SCR_PLAYER: playerOpen = true; playerPage = 0; break;
    default: break;   // the pet tile is everything else being shut
  }
  sfxPlay(SFX_TAP);
}

// ---------- the rim ----------

// The paged screens as ONE table: which variable holds the page, and how many
// there are. Three separate switches over the same list -- one to draw the
// scrollbar, one to page, one for the test -- is precisely the shape CLAUDE.md
// warns about, so draw, input and test all come through here.
static uint8_t *uiRimTarget(uint8_t *pages) {
  uint8_t n = 1;
  uint8_t *p = nullptr;
  switch (uiCurrentScreen()) {
    case SCR_GYM:     p = &gymPage;     n = (TRAINER_COUNT + GYM_ROWS - 1) / GYM_ROWS; break;
    // galleryPages(), not GAL_PAGES: with a filter on, the grid is shorter than
    // the version, and paging past the filtered end draws an empty screen.
    case SCR_GALLERY: p = &galleryPage; n = galleryPages(); break;
    case SCR_PLAYER:  p = &playerPage;  n = PLAYER_PAGES; break;
    case SCR_BOX:     p = &boxPage;     n = BOX_SLOTS / BOX_PER_PAGE; break;
    case SCR_BAGSCR:  p = &bagPage;     n = bagPages(); break;
    case SCR_CARD:    p = &cardPage;    n = CARD_PAGES; break;
    // Settings became two pages when brightness and the reset arrived; the rim
    // is how every other paged screen here works, and uiDrawRimBar() reads the
    // same table, so the second page advertises itself with no new control.
    case SCR_CLOCK:   p = &clockPage;   n = CLOCK_PAGES; break;
    case SCR_MOVEPICK: {
      uint8_t all[64];
      uint8_t k = learnableList(all, sizeof(all));
      p = &movePickPage;
      n = k ? (uint8_t)((k + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE) : 1;
      break;
    }
    case SCR_PICK: {
      uint8_t k = pickCandidates();
      p = &pickPage;
      n = k ? (uint8_t)((k + PICK_PER_PAGE - 1) / PICK_PER_PAGE) : 1;
      break;
    }
    default: break;
  }
  if (pages) *pages = n ? n : 1;
  return p;
}

uint8_t uiRimPages() {
  uint8_t n = 1;
  if (rpickModeNow() != 0xFF) return rpickPageCount(rpickModeNow());
  uiRimTarget(&n);
  return n;
}

uint8_t uiRimPage() {
  if (rpickModeNow() != 0xFF) return rpickPage;
  uint8_t *p = uiRimTarget(nullptr);
  return p ? *p : 0;
}

// dir > 0 is clockwise, which is forwards.
void onRim(int dir) {
  if (rpickSwipe(-dir)) return;   // the chooser pages itself, and WRAPS
  uint8_t n = 1;
  uint8_t *p = uiRimTarget(&n);
  if (!p || n < 2) return;
  int v = (int)(*p) + dir;
  if (v < 0) v = 0;
  if (v >= n) v = n - 1;          // CLAMPS: the rim can never close a screen
  if (v == (int)(*p)) return;
  *p = (uint8_t)v;
  if (uiCurrentScreen() == SCR_GALLERY) galleryDirty = true;
  sfxPlay(SFX_TAP);
}

bool uiOnRim(int16_t x, int16_t y) {
  int dx = x - CX, dy = y - CY;
  return dx * dx + dy * dy >= RIM_R0 * RIM_R0;
}

float uiRimAngle(int16_t x, int16_t y) {
  return atan2f((float)(y - CY), (float)(x - CX)) * 57.29578f;
}

// ---------- the chrome that makes both of the above visible ----------
#define TILE_DOT_Y 440

static inline uint16_t uiChromeInk() {
  return uiCurrentScreen() == SCR_MAIN ? inkColor() : UI_INK;
}

// The tile axis, drawn on every screen that is on it. Five bound gestures that
// nothing on the panel mentions is precisely how Ver.2 and Ver.3 came to be
// built, reachable and completely invisible; an axis nobody can see is that
// same mistake one level up.
void uiDrawTileDots() {
  int t = uiTileIndex();
  if (t < 0) return;
  uint16_t ink = uiChromeInk();
  // Not under an egg: the egg's version hint sits on the same row.
  if (t == TILE_PET && !pet.isEgg()) {
    char left[28], right[24];
    snprintf(left, sizeof(left), "< %s", T(S_PARTY));
    snprintf(right, sizeof(right), "%s >", T(S_EXPLORE));
    gfx->setTextColor(ink);
    gfx->setTextSize(2);
    gfx->setCursor(CX - 8 - (int)tcols(left) * 12, 408);
    gfx->print(left);
    gfx->setCursor(CX + 8, 408);
    gfx->print(right);
  }
  for (int i = 0; i < TILE_COUNT; i++) {
    int x = CX - (TILE_COUNT - 1) * 13 + i * 26;
    if (i == t) gfx->fillRoundRect(x - 9, TILE_DOT_Y - 3, 18, 7, 3, ink);
    else gfx->fillCircle(x, TILE_DOT_Y, 3, UI_TRACK);
  }
}

// The arc scrollbar, at 2-4 o'clock, replacing the dot rows the paged screens
// used to carry. A straight bar is at its thinnest exactly where a list is
// longest, and dots show position without showing extent. Both this and the
// rim drag read uiRimTarget(), so they cannot disagree about where you are.
void uiDrawRimBar() {
  uint8_t n = uiRimPages();
  if (n < 2) return;
  uint8_t p = uiRimPage();
  if (p >= n) p = n - 1;
  int span = RIM_BAR_A1 - RIM_BAR_A0;
  int thumb = span / n;
  if (thumb < 9) thumb = 9;
  int a0 = RIM_BAR_A0 + (span - thumb) * (int)p / (int)(n - 1);
  uiArc(CX, CY, RIM_BAR_R, RIM_BAR_A0, RIM_BAR_A1, 7, UI_TRACK);
  uiArc(CX, CY, RIM_BAR_R, a0, a0 + thumb, 7, uiChromeInk());
}

// Called by every renderer that is on the axis or pages, immediately before it
// flushes. One call, so a screen either has the chrome or visibly does not.
void uiChrome() {
  uiDrawTileDots();
  uiDrawRimBar();
}

uint8_t currentExploreRegion() {
  if (exploreRegion < REGION_COUNT && regionAvailable(exploreRegion)) return exploreRegion;
  uint8_t base = pet.region % REGION_COUNT;
  exploreRegion = regionAvailable(base) ? base : nextAvailableRegion(base);
  return exploreRegion;
}

int8_t exploreRegionHit(int16_t x, int16_t y) {
  if (y < EXPLORE_REGION_Y || y > EXPLORE_REGION_Y + EXPLORE_REGION_H ||
      x < EXPLORE_REGION_X || x > EXPLORE_REGION_X + EXPLORE_REGION_W) return 0;
  if (x <= EXPLORE_REGION_X + EXPLORE_REGION_ARROW_W) return -1;
  if (x >= EXPLORE_REGION_X + EXPLORE_REGION_W - EXPLORE_REGION_ARROW_W) return 1;
  return 0;
}

void exploreStepRegion(int8_t step) {
  int region = currentExploreRegion();
  for (uint8_t i = 0; i < REGION_COUNT; i++) {
    region += step < 0 ? -1 : 1;
    if (region < 0) region = REGION_COUNT - 1;
    else if (region >= REGION_COUNT) region = 0;
    if (regionAvailable((uint8_t)region)) {
      exploreRegion = (uint8_t)region;
      return;
    }
  }
}

void renderExplore() {
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  gfx->fillRoundRect(78, 28, 310, 54, 14, UI_BG_DAY);
  gfx->drawRoundRect(78, 28, 310, 54, 14, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)tcols(T(S_EXPLORE)) * 9, 44);
  gfx->print(T(S_EXPLORE));

  drawMap(SPR_ICON_PLAY, 16, CX - 40, 88, 5, false);
  const char *region = L(REGIONS[currentExploreRegion()].name);
  gfx->fillRoundRect(EXPLORE_REGION_X, EXPLORE_REGION_Y, EXPLORE_REGION_W,
                     EXPLORE_REGION_H, 16, UI_BG_DAY);
  gfx->drawRoundRect(EXPLORE_REGION_X, EXPLORE_REGION_Y, EXPLORE_REGION_W,
                     EXPLORE_REGION_H, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(EXPLORE_REGION_X + 38, EXPLORE_REGION_Y + 29);
  gfx->print("<");
  gfx->setCursor(EXPLORE_REGION_X + EXPLORE_REGION_W - 56, EXPLORE_REGION_Y + 29);
  gfx->print(">");
  gfx->setTextSize(tcols(region) <= 10 ? 3 : 2);
  int regionCharHalf = tcols(region) <= 10 ? 9 : 6;
  gfx->setCursor(CX - (int)tcols(region) * regionCharHalf,
                 EXPLORE_REGION_Y + (tcols(region) <= 10 ? 29 : 35));
  gfx->print(region);

  const char *dif = T(exploreHard ? S_HARD : S_EASY);
  int dw = (int)tcols(dif) * 12 + 48;
  if (dw < 140) dw = 140;
  gfx->fillRoundRect(CX - dw / 2, EXPLORE_DIF_Y, dw, EXPLORE_DIF_H, 12,
                     exploreHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(exploreHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(dif) * 6, EXPLORE_DIF_Y + 14);
  gfx->print(dif);

  bool live = !pet.isEgg() && pet.ceremony == CER_NONE;
  gfx->fillRoundRect(EXPLORE_BTN_X, EXPLORE_BTN_Y, EXPLORE_BTN_W, EXPLORE_BTN_H,
                     16, live ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(EXPLORE_BTN_X, EXPLORE_BTN_Y, EXPLORE_BTN_W, EXPLORE_BTN_H,
                     16, live ? UI_INK : UI_INK_SOFT);
  gfx->setTextColor(live ? UI_WHITE : UI_INK_SOFT);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)tcols(T(S_EXPLORE)) * 9, EXPLORE_BTN_Y + 24);
  gfx->print(T(S_EXPLORE));
  uiChrome();
  gfx->flush();
}

static void crumbDrop() {
  gCrumbMagic = CRUMB_MAGIC;
  gCrumbScreen = uiCurrentScreen();
  gCrumbHeap = ESP.getFreeHeap();
}

static const char *resetReasonName(int r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "software (our own restart)";
    case ESP_RST_PANIC:    return "PANIC -- a crash";
    case ESP_RST_INT_WDT:  return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG -- something blocked too long";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT -- the supply sagged";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}

// Printed once at boot. On a clean start it is one line; after a crash it says
// which screen was up and how much heap was left, which is the whole point.
void bootReport() {
  int r = (int)esp_reset_reason();
  bool bad = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
              r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT);
  Serial.printf("boot: reset=%s heap=%u psram=%u\n", resetReasonName(r),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (bad && gCrumbMagic == CRUMB_MAGIC) {
    Serial.printf("CRASH: it died on the '%s' screen with heap=%u\n",
                  gCrumbScreen < SCR_COUNT ? SCREEN_NAME[gCrumbScreen] : "?",
                  (unsigned)gCrumbHeap);
  } else if (bad) {
    Serial.println("CRASH: no breadcrumb (RTC memory was lost too)");
  }
  gCrumbMagic = 0;   // one report per crash, not on every later boot
}

// Dark on purpose: it can sit on an AMOLED for the length of a whole install.
void renderTransfer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(UI_INK_NIGHT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_XFER)) * 6, 160);
  gfx->print(T(S_XFER));
  char n[32];
  snprintf(n, sizeof(n), T(S_XFER_FMT), (unsigned)sdTransferFiles);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)tcols(n) * 9, 212);
  gfx->print(n);
  gfx->setTextColor(UI_SCROLL_NIGHT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_XFER_HINT)) * 6, 290);
  gfx->print(T(S_XFER_HINT));
  gfx->flush();
}

void render() {
  crumbDrop();   // so a crash can name the screen it happened on
  if (transferMode()) {
    renderTransfer();
    return;
  }
  if (pet.awaitingStarter()) {  // primera partida: version y luego inicial
    if (!starterRegionDone) renderRegionPick(RPICK_FOR_START);
    else renderStarterSelect();
    return;
  }
  if (galleryOpen) {
    if (galleryPick) { renderRegionPick(RPICK_FOR_DEX); return; }
    renderGallery();
    return;
  }
  if (movePickOpen) {
    renderMovePick();
    return;
  }
  if (partyOpen) {
    if (boxOpen) {
      if (boxDetail) renderBoxDetail();
      else renderBox();
    } else if (partyDetail) renderPartyDetail();
    else renderParty();
    return;
  }
  if (gameOpen) {
    renderGame();
    return;
  }
  if (bagOpen) {
    renderBag();
    return;
  }
  if (sackOpen) {
    renderSack();
    return;
  }
  if (spdOpen) {
    renderSpeed();
    return;
  }
  if (memoOpen) {
    renderMemo();
    return;
  }
  if (trainOpen) {
    renderTrain();
    return;
  }
  if (kbOpen) {
    renderKeyboard();
    return;
  }
  if (clockOpen) {
    renderClock();
    return;
  }
  if (battleOpen) {
    btlLinkPoll();
    renderBattle();
    return;
  }
  if (pickOpen) {
    renderPick();
    return;
  }
  if (lanOpen) {
    renderLan();
    return;
  }
  if (exploreOpen) {
    renderExplore();
    return;
  }
  if (gymOpen) {
    if (gymPick) renderRegionPick(RPICK_FOR_GYMS);
    else renderGyms();
    return;
  }
  if (playerOpen) {
    renderPlayer();
    return;
  }
  if (pet.hasLearnOffer()) {
    renderLearn();
    return;
  }
  if (cardOpen) {
    renderCard();
    return;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(L(d.name), d.accent, msg);
    drawCeremony();
    gfx->flush();
    return;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      gfx->setTextSize(2);
      gfx->setCursor(CX - tcols(rar) * 6, 316);
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_DIGIDEX_FMT), pet.registeredCount(), DEX_COUNT);
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    gfx->setTextColor(inkColor());
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(reg) * 6, 344);
    gfx->print(reg);

    // Which generation this egg comes from. It lives HERE rather than in the
    // settings screen because this is the only moment it does anything: the
    // Digimon is decided when the egg appears, so choosing the version is
    // something you do to the egg in front of you.
    drawEggRegion();
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : L(d.name);
    snprintf(name, sizeof(name), T(S_NAME_FMT), pet.variant ? "*" : "", base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    drawStreakBadge();
    drawPet();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // CTA dorado: despedida
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    gfx->setTextSize(3);
    gfx->setCursor(320, 130);
    gfx->print("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (millis() > feedMenuUntil) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(FEED_MENU_X, FEED_MENU_Y, FEED_MENU_W, FEED_MENU_H, 14, UI_WHITE);
      gfx->drawRoundRect(FEED_MENU_X, FEED_MENU_Y, FEED_MENU_W, FEED_MENU_H, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, FEED_MENU_X + 9, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, FEED_MENU_X + 75, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, FEED_MENU_X + 141, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_CANDY, 16, FEED_MENU_X + 207, FEED_MENU_Y + 8, 3, false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (millis() > confirmUntil) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(94, 168, 278, 152, 16, UI_WHITE);
      gfx->drawRoundRect(94, 168, 278, 152, 16, UI_INK);
      char q[28];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), L(DEX_TBL[pet.speciesId].name));
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - tcols(q) * 6, 196);
      gfx->print(q);
      gfx->fillRoundRect(118, 252, 100, 52, 12, UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(118 + (100 - (int)tcols(T(S_YES)) * 12) / 2, 270);
      gfx->print(T(S_YES));
      gfx->fillRoundRect(248, 252, 100, 52, 12, UI_BAR_BAD);
      gfx->setCursor(248 + (100 - (int)tcols(T(S_NO)) * 12) / 2, 270);
      gfx->print(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (millis() > choiceUntil) choiceKind = 0;
    else drawChoiceDialog();
  }

  // "<name> joined the party!" after a farewell or release
  if (partyBannerUntil) {
    if (millis() > partyBannerUntil) {
      partyBannerUntil = 0;
    } else {
      char b[40];
      snprintf(b, sizeof(b), T(S_PARTY_JOINED), partyBannerName);
      gfx->fillRoundRect(53, 176, 360, 74, 16, UI_BAR_OK);
      gfx->drawRoundRect(53, 176, 360, 74, 16, UI_INK);
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)tcols(b) * 6, 206);
      gfx->print(b);
    }
  }

  uiChrome();
  if (menuOpen) drawMenu();

  gfx->flush();
}

// ---------- minijuego: toques con la scan chip ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameOpen = true;
  gameOverUntil = 0;
  gameUntil = millis() + GAME_MS;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = 233;
  respawnBall();
}

void respawnBall() {
  ballX = 150 + random(166);
  ballY = 96;
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

// Leaving a minigame early banks what was actually earned rather than voiding
// it. Quitting used to forfeit everything, which mattered little when the ball
// game trained a stat you could grind back -- but it is now purely about
// happiness, and a pet that just played should be happier for it. The
// gameOver/over guards stop a swipe during the results screen paying twice.
void leaveGame() {
  if (!gameOverUntil) pet.playResult(gameScore);
  gameOpen = false;
}
void leaveSack() {
  if (!sackOverUntil) pet.trainStrength(sackHits);
  sackOpen = false;
}
void leaveSpeed() {
  if (!spdOverUntil) pet.trainSpeed(spdHits);
  spdOpen = false;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  // The ball is checked BEFORE the quit strip. The ball bounces inside a circle
  // of radius 205 about the centre, so it reaches y=28 -- well inside the y<72
  // header. Reaching up to hit a high ball used to abandon the game instead,
  // silently forfeiting the score, the speed training and the record.
  float dx = ballX - x, dy = ballY - y;
  bool onBall = (dx * dx + dy * dy < 74 * 74);
  if (!onBall && y < 72) {  // tocar la cabecera = salir, conservando lo ganado
    leaveGame();
    return;
  }
  if (onBall) {  // toque a la bola!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
    float lift = 6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f);
    ballVY = -lift;
    ballVX += dx * 0.12f;
    if (ballVX > 6.5f) ballVX = 6.5f;
    if (ballVX < -6.5f) ballVX = -6.5f;
    hitX = ballX;
    hitY = ballY;
    hitTime = millis();
  }
}

void stepGame() {
  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  ballVY += grav;
  ballX += ballVX;
  ballY += ballVY;
  // rebote en la pared circular
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 205) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 0.85f;
      ballVY = (ballVY - 2 * dot * ny) * 0.85f;
    }
    ballX = CX + nx * 205;
    ballY = CY + ny * 205;
  }
  // the clock, or three misses, whichever lands first
  if (gameUntil && millis() >= gameUntil && !gameOverUntil) {
    gameNewHi = (gameScore > pet.gameHi);
    pet.playResult(gameScore);
    sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
    gameOverUntil = millis() + 4000;
    return;
  }
  if (ballY > 384) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // actualiza el record y da felicidad
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  if (chase > 7) chase = 7;
  if (chase < -7) chase = -7;
  gamePetX += chase;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

void sackTap() {
  if (millis() >= sackUntil) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
}

void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (now > sackOverUntil) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - tcols(b) * 12, 150);
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(CX - tcols(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - tcols(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - tcols(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (now >= sackUntil) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(6);
  gfx->setCursor(CX - tcols(buf) * 18, 268);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_HIT_FAST)) * 6, 322);
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if (millis() > gameOverUntil) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - tcols(buf) * 12, 160);
    gfx->print(buf);
    gfx->setTextSize(2);
    if (gameNewHi && gameScore > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - tcols(T(S_NEW_RECORD)) * 6, 214);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - tcols(rec) * 6, 214);
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(CX - tcols(msg) * 6, 250);
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - tcols(buf) * 12, 30);
  gfx->print(buf);
  char rec[12];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(rec) * 6, 76);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }
  // The clock, drawn like the bag's and the reaction test's so all three games
  // read the same way. Thin and near the rim: the middle belongs to the ball.
  if (!gameOverUntil) {
    uint32_t now2 = millis();
    uint32_t left = (gameUntil > now2) ? gameUntil - now2 : 0;
    int bw = 200, fw = (int)((uint32_t)bw * left / GAME_MS);
    gfx->fillRoundRect(CX - bw / 2, 124, bw, 10, 4, UI_TRACK);
    if (fw > 2)
      gfx->fillRoundRect(CX - bw / 2, 124, fw, 10, 4,
                         left < 5000 ? UI_BAR_WARN : UI_BAR_OK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + 4) ? PMD_WALKR : (ballX < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, 394, millis(), true, false, 3);
  } else if (mon.loaded) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = (int)gamePetX - w / 2, py = 394 - h;
    for (int r = 0; r < mon.h; r++)
      for (int c = 0; c < mon.w; c++) {
        uint8_t idx = fr[r * mon.w + c];
        if (idx == 0xFF) continue;
        gfx->fillRect(px + c * s, py + r * s, s, s, mon.pal[idx]);
      }
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // la scan chip
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 24, (int)ballY - 24, 3, false);

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

// una fila de la ficha: etiqueta, barra, valor y (si iv != IV_NONE) el valor
// individual que fija el techo de ese stat
// (sin argumento por defecto: el generador de prototipos de Arduino los
// descarta y las llamadas que lo omitan no compilarian)
#define IV_NONE 0xFF
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar,
                  uint16_t color, uint8_t iv) {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(70, y);
  gfx->print(label);
  // The bar used to start at 112, which leaves 42px for a label drawn at size 2
  // -- three characters. BOND (EN), LIEN (FR) and LACO (PT) are four, so the
  // label ran under the bar. 132 fits five, with the bar narrowed to keep the
  // number clear of it.
  int bw = 130;
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(132, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(132, y + 2, fw, 11, 3, color);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(272, y);
  gfx->print(num);
  if (iv != IV_NONE) {
    char b[10];
    snprintf(b, sizeof(b), T(S_IV_FMT), iv);
    // un IV perfecto se resalta: es el golpe de suerte que el jugador busca
    gfx->setTextColor(iv >= 31 ? UI_BAR_WARN : UI_TRACK);
    gfx->setCursor(344, y);
    gfx->print(b);
  }
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
  clockPage = 0;         // always opens on the clock, never on wherever it was
  resetConfirm = false;  // and never with a wipe half-asked
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(4);
  gfx->setCursor(x + 17, y + 15);
  gfx->print(l);
}

// ---------------------------------------------------------------------------
// SETTINGS IS TWO PAGES NOW, and it had to become two.
//
// It was one, and full: the clock took the top half and a single 30 px strip at
// y 296 carried the sound switch, the volume mixer and the language pill across
// 34..432 with nothing between them. There was no room for brightness, let alone
// an in-game reset, and the strip's controls were already only 30 px tall --
// under the finger floor CLAUDE.md section 4 keeps warning about.
//
// It pages through the RIM, like every other paged screen, so no new gesture is
// invented and the arc scrollbar advertises the second page by itself. Splitting
// on the OK button is what makes the split honest: only the TIME needs applying,
// and everything on page 1 takes effect the moment it is touched.
//
// CLOCK_PAGES and clockPage live up with the other screen state, because
// uiRimTarget() reaches them ~900 lines above here.

// Page 1's rows. As wide as the glass allows AT THEIR OWN HEIGHT, the same rule
// gymRowRect() follows -- one width for every row wastes the middle and hangs the
// bottom row's corners off the bezel.
// Its metrics live up with the other layout constants, because uiButtonHeights()
// holds SET_ROW_H to the finger floor ~1700 lines above here.
// The -/+ pair on a stepper row sits at its two ends, so the label and the bar
// own the middle.
#define SET_STEP_W 54

// One answer for where a settings row is, asked by the draw path, the tap path
// and hit_test. Three copies of this arithmetic is how a control ends up
// somewhere its graphic is not.
void setRowRect(int i, int *x, int *y, int *w, int *h) {
  int ry = SET_ROW_Y(i);
  int half = uiSafeHalfWidthFor(ry, ry + SET_ROW_H) - SET_ROW_INSET;
  if (x) *x = CX - half;
  if (y) *y = ry;
  if (w) *w = half * 2;
  if (h) *h = SET_ROW_H;
}
int uiSettingsRowCenterY(int i) { return SET_ROW_Y(i) + SET_ROW_H / 2; }
int uiSettingsRows() { return SET_ROWS; }

// The training menu's geometry and the memory game's pads, published for hit_test
// so it holds them to the finger floor and the round bezel without keeping its own
// copy of either -- a test that restates a layout proves the transcription and
// then drifts from it.
int uiTrainRows() { return TRAIN_ROWS; }
int uiTrainRowH() { return TRAIN_ROW_H; }
int uiTrainRowTop(int i) { return TRAIN_ROW_Y(i); }
int uiTrainPanelTop() { return TRAIN_Y; }
int uiTrainPanelBot() { return TRAIN_Y + TRAIN_H; }
int uiTrainPanelHalfW() { return TRAIN_W / 2; }
int uiMemoPads() { return MEMO_PADS; }
int uiMemoPadR() { return MEMO_R; }

static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "中文" };

// Page 0: the clock. Unchanged, minus the controls that moved to page 1.
static void renderClockTime() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_SET_TIME)) * 9, 44);
  gfx->print(T(S_SET_TIME));

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextSize(7);
  gfx->setCursor(CX - 105, 108);
  gfx->print(t);

  drawClockBtn(104, 190, "-");  // hora -
  drawClockBtn(170, 190, "+");  // hora +
  drawClockBtn(252, 190, "-");  // min -
  drawClockBtn(318, 190, "+");  // min +
  gfx->setTextSize(2);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setCursor(120, 256);
  gfx->print(T(S_HOUR));
  gfx->setCursor(276, 256);
  gfx->print(T(S_MIN));

  gfx->fillRoundRect(133, 340, 200, 48, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  gfx->setTextSize(3);
  gfx->setCursor(CX - 18, 352);
  gfx->print("OK");

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_CLOCK_CANCEL)) * 6, 410);
  gfx->print(T(S_CLOCK_CANCEL));
}

// A stepper row: label, a level bar, and -/+ at the two ends. Volume and
// brightness are the same control over different state, so they are drawn by the
// same function -- two copies would drift the moment one of them was retuned.
// `fmt` carries its own %u, so the volume row keeps using the string it always
// had ("VOL %u") rather than needing a bare label added beside it in all six
// languages -- and STRINGS is positional, so every avoidable entry is one fewer
// chance to shift a row.
static void drawStepperRow(int i, const char *fmt, uint8_t v, uint8_t vmax,
                           uint16_t barCol) {
  int rx, ry, rw, rh;
  setRowRect(i, &rx, &ry, &rw, &rh);
  gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_BG_DAY);
  gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
  for (int b = 0; b < 2; b++) {
    const bool plus = (b != 0);
    const int bx = plus ? rx + rw - SET_STEP_W : rx;
    const bool live = plus ? (v < vmax) : (v > 0);
    gfx->fillRoundRect(bx, ry, SET_STEP_W, rh, 10, live ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(bx, ry, SET_STEP_W, rh, 10, UI_INK);
    gfx->setTextColor(live ? UI_INK : 0x8410);
    gfx->setTextSize(3);
    gfx->setCursor(bx + SET_STEP_W / 2 - 8, ry + rh / 2 - 11);
    gfx->print(plus ? "+" : "-");
  }
  const int mid = rx + SET_STEP_W, midW = rw - SET_STEP_W * 2;
  char l[24];
  snprintf(l, sizeof(l), fmt, (unsigned)v);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(mid + (midW - (int)tcols(l) * 12) / 2, ry + 6);
  gfx->print(l);
  const int barW = midW - 24;
  gfx->fillRoundRect(mid + 12, ry + rh - 14, barW, 8, 3, UI_TRACK);
  if (v) gfx->fillRoundRect(mid + 12, ry + rh - 14, barW * v / vmax, 8, 3, barCol);
}

// Page 1: everything that takes effect the moment it is touched.
static void renderClockOptions() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_SETTINGS)) * 9, 44);
  gfx->print(T(S_SETTINGS));

  int rx, ry, rw, rh;
  // sound: a full-width switch, so the master control is unmissable
  {
    setRowRect(SET_SOUND, &rx, &ry, &rw, &rh);
    const bool snd = audioEnabled();
    const char *sl = snd ? T(S_SND_ON) : T(S_SND_OFF);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, snd ? UI_BAR_OK : UI_WHITE);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
    gfx->setTextColor(snd ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(sl) * 6, ry + rh / 2 - 8);
    gfx->print(sl);
  }
  drawStepperRow(SET_VOL, T(S_VOL_FMT), audioVolume(), 10, UI_BAR_OK);
  drawStepperRow(SET_BRIGHT, T(S_BRIGHT_FMT), gBrightLevel, BRIGHT_LEVELS,
                 UI_BAR_WARN);
  // language: a pill that cycles
  {
    setRowRect(SET_LANG, &rx, &ry, &rw, &rh);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_WHITE);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
    char lp[24];
    snprintf(lp, sizeof(lp), "%s  %s >", T(S_LANG_LABEL), LANG_CODES[gLang]);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(lp) * 6, ry + rh / 2 - 8);
    gfx->print(lp);
  }
  // reset: drawn as the destructive thing it is, and it only ever OPENS the
  // confirm -- see clockTap()
  {
    setRowRect(SET_RESET, &rx, &ry, &rw, &rh);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_TRACK);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_BAR_BAD);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(T(S_RESET)) * 6, ry + rh / 2 - 8);
    gfx->print(T(S_RESET));
  }
}

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (clockPage == 0) renderClockTime();
  else renderClockOptions();

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaDigi v%s", FW_VERSION);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(ver) * 3, 436);
  gfx->print(ver);
  // the arc scrollbar and the tile dots, so the second page advertises itself
  uiChrome();
  // Drawn LAST and over everything, because it is modal: while it is up nothing
  // else on the screen responds (clockTap checks it first).
  if (resetConfirm)
    drawConfirmPanel(T(S_RESET_Q), T(S_RESET_COST), nullptr, UI_BAR_BAD,
                     T(S_RESET), UI_BAR_BAD, UI_WHITE, T(S_NO), UI_BAR_OK, UI_WHITE);
  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  // MODAL FIRST. The confirm covers the rows underneath it, and its NO button
  // sits over the reset row -- so with the rows tested first, declining the wipe
  // would re-open the very dialog it was declining.
  if (resetConfirm) {
    int c1t, c1b, c2t, c2b;
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    if (x >= CONFIRM_BTN_X && x <= CONFIRM_BTN_X + CONFIRM_BTN_W) {
      if (y >= c1t && y <= c1b) {            // yes, wipe it
        resetConfirm = false;
        // Record the intent, stop anything writing the old creature back, and
        // restart. The wipe itself happens at the top of the next boot, before
        // any of this is loaded again -- see save.h section RESET_NS.
        resetArm();
        saveInhibited = true;
        Serial.println("reset: armed by the player; restarting");
        delay(100);
        ESP.restart();
        return;
      }
      if (y >= c2t && y <= c2b) {            // no
        resetConfirm = false;
        sfxPlay(SFX_TAP);
        return;
      }
    }
    return;      // anything else while it is up does nothing at all
  }

  if (clockPage != 0) {
    for (int i = 0; i < SET_ROWS; i++) {
      int rx, ry, rw, rh;
      setRowRect(i, &rx, &ry, &rw, &rh);
      if (x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
      if (i == SET_SOUND) {
        audioSetEnabled(!audioEnabled());
        if (audioEnabled()) sfxPlay(SFX_TAP);    // confirma al encender
        return;
      }
      if (i == SET_VOL || i == SET_BRIGHT) {
        const bool minus = (x < rx + SET_STEP_W);
        const bool plus = (x > rx + rw - SET_STEP_W);
        if (!minus && !plus) return;             // the middle is a readout
        if (i == SET_VOL) {
          uint8_t v = audioVolume();
          if (plus && v < 10) audioSetVolume(v + 1);
          else if (minus && v > 0) audioSetVolume(v - 1);
          sfxPlay(SFX_TAP);                      // so the new level is audible
        } else {
          // 1, not 0: see brightBase(). A settings screen that could reach a
          // black panel would leave no visible way back from it.
          if (plus) brightSetLevel(gBrightLevel + 1);
          else if (gBrightLevel > 1) brightSetLevel(gBrightLevel - 1);
          sfxPlay(SFX_TAP);
        }
        return;
      }
      if (i == SET_LANG) {
        setLang((Lang)((gLang + 1) % LANG_COUNT));
        sfxPlay(SFX_TAP);
        return;
      }
      if (i == SET_RESET) {                      // ASKS. Always.
        resetConfirm = true;
        sfxPlay(SFX_DENY);
        return;
      }
    }
    return;
  }

  if (y >= 190 && y <= 248) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (y >= 340 && y <= 388 && x >= 133 && x <= 333) { applyClock(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x + 22, y + 2);
  gfx->print(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[20];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(l1) * 9, 176);
  gfx->print(l1);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(l2) * 6, 212);
  gfx->print(l2);
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  gfx->setTextSize(2);
  gfx->setCursor(x + (100 - (int)tcols(medalLabel(i)) * 12) / 2, y + 5);
  gfx->print(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : L(d.name);
  char head[26];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.variant ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  int hlen = tcols(head);
  int hts = (hlen <= 11) ? 3 : 2;
  gfx->setTextSize(hts);
  gfx->setCursor(CX - hlen * (hts == 3 ? 9 : 6), hts == 3 ? 34 : 40);
  gfx->print(head);
  // the stage, under the name (and the real Digimon under a nickname)
  {
    char sub[40];
    const char *stage = T((StrId)(S_STAGE_0 + (d.stage % STAGE_COUNT)));
    if (pet.nick[0]) snprintf(sub, sizeof(sub), "%s - %s", L(d.name), stage);
    else snprintf(sub, sizeof(sub), "%s", stage);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(sub) * 6, 64);
    gfx->print(sub);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 212, millis(), true, false, 3);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[30];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(sx + 24, sy + 2);
  gfx->print(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e), IV_NONE);

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[40];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(info) * 6, 296);
  gfx->print(info);

  // Its temperament, beside the food it likes -- the other thing about this
  // individual that is true from the moment it hatches and never changes. Derived
  // from the IVs, so it costs the save nothing; see Pet::personality().
  gfx->setTextColor(DEX_TBL[pet.speciesId].accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(personalityName()) * 6, 320);
  gfx->print(personalityName());

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - tcols(T(S_RENAME_HINT)) * 3, 346);
  gfx->print(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_BATTLE)) * 9, 44);
  gfx->print(T(S_BATTLE));

  // typing, in the accent colour of the Digimon (English in every language,
  // same as the Digimon names themselves)
  const DexEntry &de = DEX_TBL[pet.speciesId];
  // element and attribute: the two things a matchup is decided by
  char ty[32];
  snprintf(ty, sizeof(ty), "%s / %s", typeName(de.type1), attrName(de.attr));
  gfx->setTextColor(de.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(ty) * 6, 76);
  gfx->print(ty);

  // 360 de tope de barra: a nivel 73 (fin de ciclo) el stat mas alto de toda
  // la dex es la vitalidad de CHANSEY (355). El 260 anterior ya se desbordaba.
  drawCardStat(104, T(S_STAT_ATK), pet.atkStat(), 360, UI_BAR_BAD, pet.ivAtk);
  drawCardStat(144, T(S_STAT_DEF), pet.defStat(), 360, 0x4C98, pet.ivDef);
  drawCardStat(184, T(S_STAT_SPE), pet.speStat(), 360, UI_BAR_WARN, pet.ivSpe);
  drawCardStat(224, T(S_STAT_VIT), pet.vitStat(), 360, UI_BAR_OK, pet.ivHp);
  drawCardStat(264, T(S_STAT_WGT), pet.weight, 100, 0xB3C8, IV_NONE);

}

// Draws one move as a row: name, its type in the type's own colour, and either
// power or a STATUS marker. Shared by the moves page and the picker so a move
// looks the same wherever you meet it.
// A filled chip in the type's own colour, label in whichever of black/white
// reads on it. Returns its width so a caller can lay out beside it.
int drawTypeChip(int x, int y, uint8_t type) {
  const char *nm = typeName(type);
  int w = (int)tcols(nm) * 6 + 10;
  gfx->fillRoundRect(x, y, w, 15, 4, typeColor(type));
  gfx->setTextSize(1);
  gfx->setTextColor(typeColorIsLight(type) ? UI_INK : UI_WHITE);
  gfx->setCursor(x + 5, y + 4);
  gfx->print(nm);
  return w;
}

void drawMoveRow(int y, uint8_t mv, bool highlight, int16_t dex) {
  gfx->fillRoundRect(70, y, 326, 50, 12, highlight ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawRoundRect(70, y, 326, 50, 12, UI_INK);
  if (!mv) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(T(S_MOVE_EMPTY)) * 6, y + 17);
    gfx->print(T(S_MOVE_EMPTY));
    return;
  }
  const MoveEntry &m = MOVE_TBL[mv];
  const char *name = moveName(mv);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(82, y + 8);
  gfx->print(name);
  // There is no per-type palette (DexEntry.accent is per Digimon), and inventing
  // one by hand would duplicate what gen_dex.py generates. Colouring same-type
  // moves in the Digimon accent is more useful anyway: STAB is a 1.5x damage
  // bonus, so this marks the moves that actually hit hardest for this creature.
  // The chip carries the TYPE; STAB moved onto the power figure, where it
  // belongs -- STAB is a damage bonus, so saying it next to the damage reads
  // straight, and it leaves the type free to be its own colour.
  bool stab = hasStab(dex, m.type) && m.cat != MC_STATUS;
  int cw = drawTypeChip(82, y + 29, m.type);
  if (stab) {
    gfx->setTextColor(DEX_TBL[dex].accent);
    gfx->setTextSize(1);
    gfx->setCursor(82 + cw + 6, y + 33);
    gfx->print("STAB");
  }
  char pw[16];
  if (m.cat == MC_STATUS) snprintf(pw, sizeof(pw), "%s", T(S_MOVE_STATUS));
  else snprintf(pw, sizeof(pw), T(S_MOVE_PWR), m.power);
  gfx->setTextColor(stab ? DEX_TBL[dex].accent : UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(384 - (int)tcols(pw) * 6, y + 33);
  gfx->print(pw);
}

// card page 4: the four known moves. Tapping a slot opens the picker.
void renderCardMoves() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_MOVES)) * 9, 44);
  gfx->print(T(S_MOVES));
  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(MOVE_ROW_Y(i), pet.moves[i], false, pet.speciesId);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(T(S_MOVE_TAP)) * 3, 340);
  gfx->print(T(S_MOVE_TAP));
}

// Every move the Digimon can learn by this level, so a slot can be swapped for
// anything legal -- not just the handful a level-up would have offered.
uint8_t learnableFor(int16_t dex, uint8_t lvl, uint8_t *out, uint8_t max) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  uint8_t n = learnCount(dex), w = 0;
  for (uint8_t i = 0; i < n && w < max; i++) {
    // moveUnlockLevel(), NOT learnLevel(): a TM is stored as level 0 and would
    // otherwise clear this check at level 1. That is how a level 22 AGUMON
    // came to be offered FIRE BLAST -- the same class of bug as the level 1
    // BETAMON with SURF, in the one path that fix did not reach.
    if (moveUnlockLevel(dex, i) > lvl) continue;
    uint8_t mv = learnMove(dex, i);
    if (!mv || mv >= MOVE_COUNT) continue;
    bool dup = false;
    for (uint8_t j = 0; j < w; j++)
      if (out[j] == mv) { dup = true; break; }
    if (!dup) out[w++] = mv;
  }
  return w;
}

// The picker targets either the live pet or a banked member. A banked one keeps
// its frozen level, so it can only relearn what it could have known back then.
uint8_t learnableList(uint8_t *out, uint8_t max) {
  if (movePickParty) {
    const PartyMon &m = party.slots[movePickParty - 1];
    return learnableFor(m.dex, (uint8_t)m.level, out, max);
  }
  return pet.isEgg() ? 0 : learnableFor(pet.speciesId, pet.level(), out, max);
}

uint8_t *pickTargetMoves() {
  return movePickParty ? party.slots[movePickParty - 1].moves : pet.moves;
}
int16_t pickTargetDex() {
  return movePickParty ? party.slots[movePickParty - 1].dex : pet.speciesId;
}

void renderMovePick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_MOVE_PICK)) * 6, 40);
  gfx->print(T(S_MOVE_PICK));

  uint8_t all[64];
  uint8_t n = learnableList(all, sizeof(all));
  uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
  if (movePickPage >= pages) movePickPage = 0;
  for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
    uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
    if (idx >= n) break;
    // the move already in this slot is highlighted, so replacing like for like
    // is obvious rather than a guess
    drawMoveRow(MOVE_PICK_Y(i), all[idx], all[idx] == pickTargetMoves()[movePickSlot], pickTargetDex());
  }
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    if (i == movePickPage) gfx->fillCircle(CX - (pages - 1) * 13 + i * 26, 380, 5, UI_INK);
    else gfx->drawCircle(CX - (pages - 1) * 13 + i * 26, 380, 4, UI_INK);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 402);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// ---------- battle ----------

// Draws a battle backdrop scaled 2x. Emitted as runs of identical indices
// rather than a write per pixel: this is flat pixel art with long horizontal
// runs, and 240x112 at 2x would otherwise be 26,880 fillRect calls a frame.
// The round bezel crops the overhang physically, so nothing is clipped here.
static void drawBack(const BackScene &b, int y0) {
  const int SC = 2;
  int x0 = CX - (b.w * SC) / 2;
  for (int r = 0; r < b.h; r++) {
    const uint8_t *row = b.idx + (uint32_t)r * b.w;
    int c = 0;
    while (c < b.w) {
      uint8_t v = row[c];
      int run = 1;
      while (c + run < b.w && row[c + run] == v) run++;
      uint16_t col = b.pal[v];
      gfx->fillRect(x0 + c * SC, y0 + r * SC, run * SC, SC, col);
      c += run;
    }
  }
}

// Which scene: the FOE's biome, since a battle happens where it lives, and the
// same day/night split the main screen already uses.
static void drawBattleBack() {
  int16_t dex = btlFoe.dex;
  if (dex < 1 || dex > DEX_COUNT) { gfx->fillCircle(CX, CY, 231, UI_BG_DAY); return; }
  uint8_t bi = DEX_TBL[dex].biome;
  if (bi >= BACK_BIOMES) bi = 0;
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  drawBack(BACKS[bi][night ? 1 : 0], 30);
}

// Streams a side's sprite if it is not already the one loaded. Called whenever
// a creature steps in, never per frame.
static void btlSyncSprite(uint8_t who, const Combatant &c) {
  int16_t key = c.dex * (c.variant ? -1 : 1);
  if (btlPmdDex[who] == key && btlPmd[who].loaded) return;
  btlPmd[who].unload();
  btlPmdDex[who] = 0;
  if (c.dex < 1 || c.dex > DEX_COUNT) return;
  if (btlPmd[who].load(c.dex, c.variant)) btlPmdDex[who] = key;   // NOT (uint8_t): Ver.3 runs past 255
}

static void btlFreeSprites() {
  for (int i = 0; i < 2; i++) { btlPmd[i].unload(); btlPmdDex[i] = 0; }
}

static void btlSay(const char *fmt, ...) {
  if (btlMsgCount >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(btlMsg[btlMsgCount], sizeof(btlMsg[0]), fmt, ap);
  va_end(ap);
  btlMsgCount++;
}

// Turns a TurnLog into narration. Everything here was already decided by the
// engine -- nothing is recomputed, so the text can never disagree with the maths.
// Picks the cue for an action from the TurnLog, so the sound can never
// disagree with what actually happened.
static void btlSfxFor(const TurnLog &lg) {
  if (lg.targetFainted) { sfxPlay(SFX_FAINT); return; }
  if (lg.inflicted) { sfxPlay(SFX_STATUS); return; }
  if (lg.damage && lg.effPct > 100) { sfxPlay(SFX_SUPER); return; }
  if (lg.damage) {
    sfxPlay(lg.move && MOVE_TBL[lg.move].cat == MC_SPEC ? SFX_BEAM : SFX_HIT);
    return;
  }
  if (lg.move && MOVE_TBL[lg.move].cat == MC_STATUS && !lg.missed) sfxPlay(SFX_STATUS);
}

static void btlNarrate(const Combatant &actor, const Combatant &target, const TurnLog &lg) {
  if (lg.skipped) return;
  btlSfxFor(lg);
  if (lg.hurtSelf) { btlSay(T(S_BTL_HURTSELF)); return; }
  if (lg.charged) { btlSay(T(S_BTL_USED), L(actor.name), moveName(lg.move)); return; }
  if (lg.move) btlSay(T(S_BTL_USED), L(actor.name), moveName(lg.move));
  if (lg.missed) { btlSay(T(S_BTL_MISS), L(actor.name)); return; }
  if (lg.immune) { btlSay(T(S_BTL_IMMUNE)); return; }
  if (lg.crit) btlSay(T(S_BTL_CRIT));
  if (lg.damage && lg.effPct > 100) btlSay(T(S_BTL_SUPER));
  else if (lg.damage && lg.effPct < 100) btlSay(T(S_BTL_WEAK));
  if (lg.inflicted) {
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    if (lg.inflicted < 7) btlSay(T(S_BTL_STATUS), L(target.name), T(AIL_STR[lg.inflicted]));
  }
  if (lg.targetFainted) btlSay(T(S_BTL_FAINT), L(target.name));
}

// Builds one opponent through Pet, so it gets the same stat formula and the
// same learnset-driven moveset the player's creatures do.
static void foeFromSpecies(Combatant &c, int16_t dex, uint8_t lvl, uint8_t iv) {
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = iv;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(c, foe);
}

// Your side: the live pet first, then the banked party.
//
// Both ladders cap your LEVEL to the boss's best, so a boss is always fought
// on its own terms and grinding is never the answer -- the type chart, the
// movesets and the choices are. Hard additionally caps your team SIZE to the
// boss's, so the first boss is two-on-two. The caps are applied while BUILDING the
// combatants, so nothing is ever written back to the stored creature, exactly
// like ailments.
static void buildSquad(uint8_t maxLvl, uint8_t maxCount, uint16_t mask) {
  btlSquadN = 0;
  btlSquadAt = 0;
  btlPetIn = false;
  if (maxCount > TRAINER_TEAM_MAX) maxCount = TRAINER_TEAM_MAX;
  if (!pet.isEgg() && btlSquadN < maxCount && (mask & 1)) {
    combatantFromPet(btlSquad[btlSquadN++], pet, maxLvl);
    btlPetIn = true;      // the training reward goes to whoever fought for it
  }
  for (int i = 0; i < PARTY_SLOTS && btlSquadN < maxCount; i++) {
    if (party.slots[i].empty() || !(mask & (1 << (i + 1)))) continue;
    PartyMon m = party.slots[i];
    if (maxLvl && m.level > maxLvl) m.level = maxLvl;
    combatantFromParty(btlSquad[btlSquadN++], m);
  }
  if (btlSquadN) btlYou = btlSquad[0];
}

// How many you may bring: the boss's own count in hard mode, six otherwise.
// PICK_TRAINERS, not TRAINERS -- this answers for the fight being set up, so it
// must read the version the picker was opened for.
uint8_t squadCap(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT) return TRAINER_TEAM_MAX;   // PICK_LAN: uncapped six
  return hard ? PICK_TRAINERS[idx].count : TRAINER_TEAM_MAX;
}

// A fight against another device. The squads are already exchanged; the host
// owns resolution and the guest renders what it is sent.
void startLinkBattle() {
  if (!lan.mineN || !lan.theirsN) return;
  // Rebuilt from lan.mine, NOT from squadMask. What we fight with has to be
  // exactly what the peer was told we have -- rebuilding from the party would
  // silently diverge if anything changed between offering and starting.
  btlSquadN = 0;
  btlSquadAt = 0;
  for (uint8_t i = 0; i < lan.mineN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlSquad[btlSquadN++], lan.mine[i]);
  if (!btlSquadN) return;
  btlYou = btlSquad[0];
  btlLink = true;
  btlLinkHost = lan.isHost;
  btlWild = false;
  btlTrainer = -1;
  btlHard = false;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  for (uint8_t i = 0; i < lan.theirsN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlFoeSquad[btlFoeSquadN++], lan.theirs[i]);
  btlFoe = btlFoeSquad[0];
  btlMyAct = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  battleOpen = true;
}

void startTrainerBattle(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT || pet.isEgg() || pet.ceremony != CER_NONE) return;
  uint8_t region = gymRegion % GYM_REGIONS;
  const Trainer &tr = TRAINER_SETS[region].list[idx];
  uint8_t top = 0;
  for (int k = 0; k < tr.count; k++)
    if (tr.team[k].level > top) top = tr.team[k].level;
  // BOTH ladders cap your level to the boss's best. Without it a L73 team
  // walks every tamer at 100% and the type chart never matters. Hard adds the
  // size cap on top, plus a smarter AI and better opposing IVs.
  buildSquad(top, hard ? tr.count : TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return;
  btlTrainer = (int8_t)idx;
  btlRegion = region;
  btlHard = hard;
  btlWild = false;
  btlFoeAt = 0;
  foeFromSpecies(btlFoe, tr.team[0].dex, tr.team[0].level, hard ? HARD_IV : EASY_IV);
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

void startBattle(int16_t dex, uint8_t lvl) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return;
  if (dex < 1 || dex > DEX_COUNT) return;
  buildSquad(0, TRAINER_TEAM_MAX, 0xFFFF);
  if (!btlSquadN) return;
  // The opponent is built through Pet so it gets the same stat formula and the
  // same learnset-driven moveset the player's creature does -- no special-cased
  // "enemy" maths that could quietly diverge.
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = 20;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlWild = false;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

// The guest's whole turn: copy in what the host resolved and play the same
// animations the host is playing. It runs no battle logic at all -- that is the
// entire point of one side being authoritative (see link.h).
static void btlApplyResult() {
  if (lan.resultN < sizeof(LinkResult)) { lan.resultNew = false; return; }
  LinkResult r;
  memcpy(&r, lan.result, sizeof(r));
  lan.resultNew = false;

  // The wire says "host"/"guest"; here we are always the guest, so their fields
  // are the foe's and ours are ours.
  uint32_t now = millis();
  if (r.guestIdx < btlSquadN && r.guestIdx != btlSquadAt) {
    btlSquad[btlSquadAt] = btlYou;
    btlSquadAt = r.guestIdx;
    btlYou = btlSquad[btlSquadAt];
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
  }
  if (r.hostIdx != btlFoeAt && r.hostIdx < lan.theirsN) {
    btlFoeAt = r.hostIdx;
    linkMonTo(btlFoe, lan.theirs[btlFoeAt]);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
  }
  btlYou.hp = r.guestHp > btlYou.maxHp ? btlYou.maxHp : r.guestHp;
  btlFoe.hp = r.hostHp > btlFoe.maxHp ? btlFoe.maxHp : r.hostHp;
  btlYou.ailment = r.guestAil;
  btlFoe.ailment = r.hostAil;

  btlMsgCount = 0;
  if (r.hostMove) btlSay(T(S_BTL_USED), L(btlFoe.name), moveName(r.hostMove));
  if (r.guestMove) btlSay(T(S_BTL_USED), L(btlYou.name), moveName(r.guestMove));
  if (r.guestDmg) { btlHitUntil[0] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (r.hostDmg) { btlHitUntil[1] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (btlYou.fainted()) {
    btlFaintUntil[0] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), L(btlYou.name));
  }
  if (btlFoe.fainted()) {
    btlFaintUntil[1] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), L(btlFoe.name));
  }
}

// Radio packets land on another task, so the guest picks them up here, once a
// frame, rather than rendering from inside an interrupt.
static void btlLinkPoll() {
  if (!btlLink) return;

  // A peer that stopped answering. Ending the fight is the only honest thing to
  // do -- there is no result coming, and pretending otherwise is the hang this
  // whole layer exists to remove.
  if (!lan.live() && !btlOver) {
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlMsgCount = 0;
    btlSay("%s", T(S_LAN_GONE));
    return;
  }

  if (btlLinkHost) {
    // Our own action was latched when it was tapped; theirs arrives whenever
    // the radio manages it. Whichever is second sets the turn going.
    if (btlMyAct && lan.hasPeerAct() && !btlOver && !btlMsgCount &&
        btlSwapWho < 0) {
      uint8_t act = btlMyAct;
      btlMyAct = 0;
      if (LINK_ACT_IS_SWITCH(act)) btlSwitchTo(LINK_ACT_SLOT(act));
      else btlResolve(btlYou.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS]);
    }
    return;
  }

  if (lan.resultNew) btlApplyResult();
  if (lan.state == LINK_DONE && !btlOver) {
    btlOver = true;
    btlWon = lan.youWon;
    if (btlWon && btlPetIn) pet.addWin();
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE));
  }
}

// Packs the outcome for the guest. Only the host ever calls this.
static void btlShipResult(uint8_t yourMove, uint8_t theirMove,
                          uint16_t hp0You, uint16_t hp0Foe) {
  LinkResult r = {};
  r.hostHp = btlYou.hp;   r.guestHp = btlFoe.hp;
  r.hostAil = btlYou.ailment; r.guestAil = btlFoe.ailment;
  r.hostMove = yourMove;  r.guestMove = theirMove;
  r.hostDmg = (hp0You > btlYou.hp) ? hp0You - btlYou.hp : 0;
  r.guestDmg = (hp0Foe > btlFoe.hp) ? hp0Foe - btlFoe.hp : 0;
  r.hostIdx = btlSquadAt; r.guestIdx = btlFoeAt;
  if (btlYou.fainted() || btlFoe.fainted()) r.flags |= 0x04;
  lan.sendResult((const uint8_t *)&r, (uint8_t)sizeof(r));
}

// ---------- wild battle actions ----------

// What beating a wild creature is worth. Each draw EXCLUDES every earlier one,
// so a settlement can never list the same item twice -- which would read as a
// bug rather than as luck, the same reasoning that stops rewardTraining()
// picking a stat with no headroom.
static void wildGrantDrops() {
  wildDropN = 0;
  uint8_t n = wildDropCount(btlHard, (uint8_t)random(100));
  for (uint8_t i = 0; i < n && i < 4; i++) {
    uint32_t total = bag.weightTotal(wildDrops, wildDropN);
    if (!total) break;
    ItemKey k = bag.weightedDrop((uint32_t)random(total), wildDrops, wildDropN);
    if (!k) break;
    bag.add(k);
    wildDrops[wildDropN++] = k;
    btlSay(T(S_FOUND_ITEM), L(itemEntry(k).name));
  }
}

// Where a caught creature goes. It reuses the SAME slot-finding path a farewell
// uses -- party, then box, then ask -- rather than growing a second copy of it.
// CLAUDE.md's most repeated bug is a rule enforced in one path but not its
// twin, and "which slot does a creature take" is exactly that shape.
static void wildStoreCaught() {
  PartyMon m = wildToPartyMon();
  // NOT registered in the Digidex here. Registration means "you raised this",
  // and switchTo() does it when the creature actually becomes the one you are
  // raising -- so catching one and leaving it in the box does not fill the dex.
  if (party.add(m) || party.boxAdd(m)) return;
  // Both full: hand it to the chooser the endings already own. endedKind is
  // only ever tested against CER_NONE by that flow, so this is the intended
  // handover and not a borrowed meaning. Through setEnded() so it is written to
  // the pet checkpoint as well -- a creature you just caught and have not yet
  // found a slot for must not evaporate on a power cut either.
  pet.setEnded(m, CER_RELEASE);
}

// Throwing a ball. Spends the turn either way: a free capture attempt every
// round would make weakening the target pointless.
static void btlThrowBall(ItemKey k) {
  if (!bag.consume(k)) { sfxPlay(SFX_DENY); return; }
  btlMenu = 0;
  btlSay(T(S_THREW), L(itemEntry(k).name));
  uint8_t chance = wildCaptureChance(wildTierOfDex(btlFoe.dex), btlFoe.hp,
                                     btlFoe.maxHp, btlFoe.ailment != AIL_NONE,
                                     itemEntry(k).param);
  if (random(100) < chance) {
    wildCaught = true;
    btlOver = true;
    btlWon = true;
    if (btlPetIn) pet.addWin();
    wildStoreCaught();
    audioMusic(MUS_VICTORY);
    sfxPlay(SFX_VICTORY);
    btlSay(T(S_CAUGHT), L(btlFoe.name));
    wildGrantDrops();
    return;
  }
  sfxPlay(SFX_DENY);
  btlSay("%s", T(S_BROKE_FREE));
  btlResolve(0);   // move 0 = no attack, so only the foe acts
}

// A potion or a full heal, on whichever creature is out. It works on the
// Combatant, so like every ailment it is battle-only and is never written back.
static void btlUseItem(ItemKey k) {
  const ItemEntry &e = itemEntry(k);
  bool useful = (e.category == IC_HEAL) ? btlYou.hp < btlYou.maxHp
              : (e.category == IC_CURE) ? btlYou.ailment != AIL_NONE
              : false;
  if (!useful) { sfxPlay(SFX_DENY); btlSay("%s", T(S_ITEM_NOUSE)); btlMenu = 0; return; }
  if (!bag.consume(k)) { sfxPlay(SFX_DENY); return; }
  if (e.category == IC_HEAL) {
    uint32_t hp = (uint32_t)btlYou.hp + (uint16_t)e.param;
    btlYou.hp = hp > btlYou.maxHp ? btlYou.maxHp : (uint16_t)hp;
  } else {
    btlYou.ailment = AIL_NONE;
    btlYou.ailTurns = 0;
    btlYou.confuseTurns = 0;
  }
  btlMenu = 0;
  sfxPlay(SFX_TAP);
  btlSay(T(S_ITEM_USED), L(e.name));
  btlResolve(0);   // using an item costs the turn, as it does in the real games
}

// One exchange: both sides act in speed order, then burn/poison chip.
static void btlResolve(uint8_t yourMove) {
  TurnLog lg;
  // Against another device the opponent's move comes off the wire, never from
  // the AI -- and the host is the only side that runs this at all.
  uint8_t foeMove;
  bool foeSwitched = false;
  uint32_t now = millis();
  if (btlLink) {
    // Off the wire, never from the AI. A switch is carried in the same message
    // as a move, and like our own switch it costs the turn: they change, we act.
    uint8_t act = lan.pendingAct;
    if (LINK_ACT_IS_SWITCH(act)) {
      uint8_t to = LINK_ACT_SLOT(act);
      if (to < btlFoeSquadN && to != btlFoeAt && !btlFoeSquad[to].fainted()) {
        btlFoeSquad[btlFoeAt] = btlFoe;     // remember how battered it was
        btlFoeAt = to;
        btlFoe = btlFoeSquad[to];
        btlHpShown[1] = btlFoe.hp;
        btlSyncSprite(1, btlFoe);
        btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
        btlEnterUntil[1] = now + BTL_ENTER_MS;
        btlSay(T(S_BTL_SENDS), lan.peerName, L(btlFoe.name));
        foeSwitched = true;
      }
      foeMove = 0;
    } else {
      foeMove = btlFoe.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS];
    }
    lan.pendingAct = 0;
  } else {
    foeMove = aiChooseMove(btlFoe, btlYou, btlHard);
    // A wild creature can leave. It spends its ACTION doing so rather than
    // fleeing and attacking in the same turn, so the fight simply ends -- with
    // no drops, which is what makes weakening it worth doing before it goes.
    if (btlWild && random(100) < wildFoeEscapeChance(btlFoe.hp, btlFoe.maxHp)) {
      btlOver = true;
      btlWon = false;
      audioMusic(MUS_NONE);
      btlSay(T(S_WILD_FLED), L(btlFoe.name));
      return;
    }
  }
  (void)foeSwitched;

  bool youFirst = battleMovesFirst(btlYou, yourMove, btlFoe, foeMove);
  Combatant *a = youFirst ? &btlYou : &btlFoe;
  Combatant *b = youFirst ? &btlFoe : &btlYou;
  uint8_t ma = youFirst ? yourMove : foeMove;
  uint8_t mb = youFirst ? foeMove : yourMove;

  uint16_t hp0You = btlYou.hp, hp0Foe = btlFoe.hp;
  battleAct(*a, *b, ma, lg);
  btlNarrate(*a, *b, lg);
  if (lg.damage && !lg.hurtSelf) btlLungeUntil[a == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS;
  if (!b->fainted()) {
    battleAct(*b, *a, mb, lg);
    btlNarrate(*b, *a, lg);
    if (lg.damage && !lg.hurtSelf)
      btlLungeUntil[b == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS + BTL_LUNGE_MS;
  }
  // whoever actually lost health flinches, whichever side dealt it
  if (btlYou.hp < hp0You) btlHitUntil[0] = now + BTL_HIT_MS;
  if (btlFoe.hp < hp0Foe) btlHitUntil[1] = now + BTL_HIT_MS;
  if (btlLink && btlLinkHost) btlShipResult(yourMove, foeMove, hp0You, hp0Foe);
  if (!btlYou.fainted() && !btlFoe.fainted()) {
    battleEndTurn(btlYou, lg);
    if (lg.damage) btlNarrate(btlYou, btlYou, lg);
    battleEndTurn(btlFoe, lg);
    if (lg.damage) btlNarrate(btlFoe, btlFoe, lg);
  }
  // Someone went down. The replacement is NOT swapped in here -- that made the
  // change instant and read as a jump cut. Flag it, let the sprite drop out of
  // frame, and swap when the player dismisses the message.
  if (btlFoe.fainted() && btlLink && btlFoeAt + 1 < btlFoeSquadN) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlFoe.fainted() && btlTrainer >= 0 && btlFoeAt + 1 < BTL_TRAINERS[btlTrainer].count) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlYou.fainted() && btlSquadAt + 1 < btlSquadN) {
    btlFaintUntil[0] = millis() + BTL_FAINT_MS;
    btlSwapWho = 0;
    return;
  }
  if (btlFoe.fainted() || btlYou.fainted()) {
    btlOver = true;
    btlWon = btlFoe.fainted();
    // Every won battle counts towards the live Digimon's next digivolution
    // (Champion -> Ultimate needs them), whoever finished the fight.
    if (btlWon && btlPetIn) pet.addWin();
    btlNewBadge = false;
    btlTrainGain = 0;
    if (btlWon && btlTrainer >= 0 && !pet.hasBadge(btlRegion, btlTrainer, btlHard)) {
      pet.winBadge(btlRegion, btlTrainer, btlHard);
      btlNewBadge = true;
    }
    // A badge and nothing else made the ladder a one-way checklist. A win now
    // trains the creature that fought for it -- so a boss you can already
    // beat is worth returning to. It goes to the LIVE pet only: banked members
    // are frozen at the level and training they were banked with, and battling
    // already costs the live pet energy, which is what rate-limits the grind
    // without needing a cooldown.
    if (btlWon && btlTrainer >= 0 && btlPetIn) {
      // Later bosses are worth more, and hard mode is worth roughly double.
      uint8_t amt = (btlHard ? 6 + random(5) : 3 + random(3)) + btlTrainer / 3;
      btlTrainGain = pet.rewardTraining(amt, btlTrainWhich);
    }
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    // Tell the peer before anything else: if we stop here without sending, the
    // other device sits on a battle that will never take another turn.
    if (btlLink && btlLinkHost) lan.sendEnd(btlWon);
    if (btlLink) { btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE)); return; }
    // A wild win reports its drops as narration rather than on the win screen:
    // that screen is the badge ceremony, and it has no badge to show.
    if (btlWon && btlWild) { btlSay("%s", T(S_BTL_WIN)); wildGrantDrops(); return; }
    if (btlWon && btlTrainer >= 0) { btlWinUntil = millis() + 60000; return; }
    btlSay("%s", T(S_BTL_LOSE));
  }
}

static void btlHpBar(int x, int y, int w, const Combatant &c, uint16_t shown) {
  int fw = c.maxHp ? (w - 4) * shown / c.maxHp : 0;
  uint16_t col = (shown * 2 > c.maxHp) ? UI_BAR_OK
                 : (shown * 4 > c.maxHp) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(x, y, w, 14, 4, UI_TRACK);
  if (fw > 0) gfx->fillRoundRect(x + 2, y + 2, fw, 10, 3, col);
  gfx->drawRoundRect(x, y, w, 14, 4, UI_INK);
}

static void btlSide(int tx, int ty, int sx, int sy, const Combatant &c, uint8_t who) {
  // the scenes are busy, so the name and bar sit on their own plate rather
  // than fighting the artwork for contrast
  int ph = (who == 0) ? 54 : 40;
  gfx->fillRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_BG_DAY);
  gfx->drawRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_INK);
  char l[28];
  snprintf(l, sizeof(l), "%s Lv.%u", L(c.name), c.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(tx, ty);
  gfx->print(l);
  gfx->setTextColor(UI_BAR_WARN);
  gfx->setCursor(tx, ty + 14);
  gfx->print("HP");
  btlHpBar(tx + 18, ty + 12, 122, c, btlHpShown[who]);
  if (who == 0) {                 // your own numbers, as the games do
    char hp[16];
    snprintf(hp, sizeof(hp), "%u/%u", btlHpShown[who], c.maxHp);
    gfx->setTextColor(UI_INK);
    gfx->setCursor(tx + 140 - (int)tcols(hp) * 6, ty + 28);
    gfx->print(hp);
  }
  if (c.ailment != AIL_NONE) {   // a status is the thing you most need to see
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setCursor(tx + 18, ty + 28);
    gfx->print(T(AIL_STR[c.ailment]));
  }
  // a platform under each creature, so they stand in the scene rather than
  // floating over it
  uint32_t now = millis();
  int ox = 0, oy = 0;
  bool flash = false;
  if (now < btlFaintUntil[who]) {          // sinks out of frame as it faints
    uint32_t left = btlFaintUntil[who] - now;
    oy += (int)((BTL_FAINT_MS - left) * 70 / BTL_FAINT_MS);
  } else if (c.fainted() && btlSwapWho == (int8_t)who) {
    return;                                // gone, waiting to be replaced
  }
  if (now < btlEnterUntil[who]) {          // and the next one rises into place
    uint32_t left = btlEnterUntil[who] - now;
    oy += (int)(left * 70 / BTL_ENTER_MS);
  }
  if (now < btlLungeUntil[who]) {          // lean in, then back out
    uint32_t left = btlLungeUntil[who] - now;
    int amt = (int)(left > BTL_LUNGE_MS / 2 ? BTL_LUNGE_MS - left : left) * 22 / (BTL_LUNGE_MS / 2);
    ox = who == 0 ? amt : -amt;            // you lunge right, the foe lunges left
    oy = who == 0 ? -amt / 2 : amt / 2;
  }
  if (now < btlHitUntil[who]) {
    uint32_t left = btlHitUntil[who] - now;
    ox += ((left / 50) % 2) ? 5 : -5;      // jitter
  }

  // Real PMD playback when the sprite streamed: attack while lunging, hurt
  // while flinching, idle otherwise. `has()` guards every one, because not
  // every Digimon ships every action -- falling through to idle, and to the
  // flat thumbnail if the sprite is missing entirely (no SD).
  if (btlPmd[who].loaded) {
    uint8_t act = PMD_IDLE;
    bool loop = true;
    uint32_t t = now;
    if (now < btlHitUntil[who] && btlPmd[who].has(PMD_HURT)) {
      act = PMD_HURT; loop = false; t = now - (btlHitUntil[who] - BTL_HIT_MS);
    } else if (now < btlLungeUntil[who] && btlPmd[who].has(PMD_ATTACK)) {
      act = PMD_ATTACK; loop = false; t = now - (btlLungeUntil[who] - BTL_LUNGE_MS);
    }
    // x3, not x4: a 16x16 dot sprite fills its whole canvas, where the old
    // art left half of it empty, so x4 pushed the foe off the top of the glass.
    if (who == 0) drawPmdActFlip(btlPmd[who], act, sx + 24 + ox, sy + 78 + oy, t, loop, false, 3);
    else drawPmdActM(btlPmd[who], act, sx + 24 + ox, sy + 78 + oy, t, loop, false, 3);
    return;
  }
  const uint8_t *th = thumbs.get(c.dex);
  if (!th) return;
  if (now < btlHitUntil[who]) flash = ((btlHitUntil[who] - now) / 60) % 2 == 0;
  drawThumb(th, sx + ox, sy + oy, 3, flash);
}

// Bars drain rather than snap: a hit that removes half your health should be
// visible as it happens, not as a value that was already different.
static void btlEaseBars() {
  const uint16_t real[2] = { btlYou.hp, btlFoe.hp };
  for (int i = 0; i < 2; i++) {
    int diff = (int)real[i] - (int)btlHpShown[i];
    if (!diff) continue;
    int step = diff / 5;
    if (!step) step = diff > 0 ? 1 : -1;
    btlHpShown[i] = (uint16_t)((int)btlHpShown[i] + step);
  }
}

// The moment the ladder builds toward. It used to be one more line in the same
// message box as "It's super effective!", with the badge awarded silently.
void renderWin() {
  const Trainer &t = BTL_TRAINERS[btlTrainer];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  gfx->setTextColor(UI_BAR_WARN);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_BTL_WIN)) * 9, 54);
  gfx->print(T(S_BTL_WIN));

  char l[40];
  snprintf(l, sizeof(l), T(S_BTL_BEAT), L(t.name));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(l) * 6, 96);
  gfx->print(l);

  // the badge, large, with the hard-mode halo if that is how it was won
  if (btlTrainer < TRAINER_GYMS) {
    int by = 190;
    if (btlHard) {
      for (int r = 62; r >= 56; r--) gfx->drawCircle(CX, by, r, r % 2 ? 0xFEA0 : 0xFF60);
    }
    const BadgeArt *a = badgeArtFor(btlRegion, btlTrainer);
    if (a) {
      for (int r = 0; r < BADGE_PX; r++)
        for (int c = 0; c < BADGE_PX; c++) {
          uint8_t v = a->idx[r * BADGE_PX + c];
          if (v == 0xFF) continue;
          // 3x, so it reads as a prize rather than a list entry
          gfx->fillRect(CX - BADGE_PX * 3 / 2 + c * 3, by - BADGE_PX * 3 / 2 + r * 3,
                        3, 3, a->pal[v]);
        }
    } else {
      // This version has no badge art. Say so with a plain medal in the boss's
      // type colour rather than borrowing another version's badge.
      const Trainer &tr = TRAINER_SETS[btlRegion % GYM_REGIONS].list[btlTrainer];
      gfx->fillCircle(CX, by, 26, typeColor(tr.type));
      gfx->drawCircle(CX, by, 26, UI_INK);
      char n[4];
      snprintf(n, sizeof(n), "%u", (unsigned)(btlTrainer + 1));
      gfx->setTextSize(3);
      gfx->setTextColor(typeColorIsLight(tr.type) ? UI_INK : UI_WHITE);
      gfx->setCursor(CX - (int)tcols(n) * 9, by - 11);
      gfx->print(n);
    }
    if (btlNewBadge) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - tcols(T(S_BTL_NEWBADGE)) * 6, 286);
      gfx->print(T(S_BTL_NEWBADGE));
    }
  }
  snprintf(l, sizeof(l), T(S_BADGES_FMT), pet.badgeCountIn(btlRegion, btlHard));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(l) * 6, 316);
  gfx->print(l);

  // what the win was worth beyond the badge
  if (btlTrainGain) {
    static const StrId NAMES[3] = { S_TR_ATK, S_TR_DEF, S_TR_SPE };
    snprintf(l, sizeof(l), T(S_WIN_TRAIN_FMT),
             T(NAMES[btlTrainWhich % 3]), btlTrainGain);
    gfx->setTextColor(UI_BAR_OK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(l) * 6, 344);
    gfx->print(l);
  } else if (btlPetIn && btlTrainer >= 0) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(T(S_WIN_MAXED)) * 3, 348);
    gfx->print(T(S_WIN_MAXED));
  }

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 380);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void renderBattle() {
  if (btlWinUntil) { renderWin(); return; }
  btlEaseBars();
  gfx->fillScreen(RGB565_BLACK);
  drawBattleBack();
  // the lower band stays flat so the move grid and the HP text keep their
  // contrast against it
  gfx->fillRect(0, 254, 466, 212, UI_BG_DAY);

  // x=82 not 58: at y=60 the round bezel starts around x=77, and a longer
  // name like SEADRAMON was losing its first characters off the edge
  btlSide(82, 82, 300, 40, btlFoe, 1);    // foe reads top-left, sprite top-right
  btlSide(250, 190, 76, 168, btlYou, 0);  // you read bottom-right, sprite bottom-left

  // Waiting on the other device. Without this the screen is identical to the
  // one where it is your turn, so a tap that has been sent and a tap that was
  // never registered look exactly the same.
  bool lanWait = btlLink && !btlOver &&
                 (btlLinkHost ? (btlMyAct && !lan.hasPeerAct())
                              : (lan.state == LINK_WAITING));
  if (lanWait && !btlMsgCount) {
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_INK);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    const char *w = T(S_LAN_WAITFOE);
    gfx->setCursor(CX - (int)tcols(w) * 6, BTL_GRID_Y + 48);
    gfx->print(w);
  } else if (btlMsgCount) {            // narration takes over the menu area
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    char lines[3][64] = {{0}};     // 24 columns of CJK is 36 bytes of UTF-8
    const char *p = btlMsg[0];
    uint8_t lineCount = 0;
    while (*p && lineCount < 3) {
      while (*p == ' ') p++;
      int take = wrapTake(p, 24);
      memcpy(lines[lineCount], p, take);
      lines[lineCount][take] = 0;
      p += take;
      lineCount++;
    }
    int firstY = BTL_GRID_Y + 18 + (3 - lineCount) * 10;
    for (uint8_t i = 0; i < lineCount; i++) {
      gfx->setCursor(CX - (int)tcols(lines[i]) * 6, firstY + i * 24);
      gfx->print(lines[i]);
    }
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(1);
    gfx->setCursor(CX - tcols(T(S_TAP_MORE)) * 3, BTL_GRID_Y + BTL_GRID_H - 14);
    gfx->print(T(S_TAP_MORE));
  } else if (btlMenu == 0) {
    // Four options on the 2x2 grid: FIGHT and BAG on top, DIGIMON and RUN
    // below. FIGHT used to be a full-width row, which left nowhere for the bag;
    // all four now reuse the move grid's cells and so inherit its padded hit
    // areas -- the same padding that fixed DIGIMON being hard to press.
    const char *opt[4] = { T(S_FIGHT), T(S_BAG), T(S_BTL_SWITCH), T(S_BTL_RUN) };
    for (int i = 0; i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      // The bag is dead in a link fight: an item the peer was never told about
      // would desync the two devices, and the host is authoritative.
      bool live = !(i == 1 && btlLink);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_INK : 0x8410);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(2);
      gfx->setCursor(x + (BTL_CELL_W - (int)tcols(opt[i]) * 12) / 2, y + 19);
      gfx->print(opt[i]);
    }
  } else if (btlMenu == 3) {
    drawBtlBack();
    // The battle bag: balls and medicine, never vitamins -- training is
    // permanent and belongs to the care sim.
    uint8_t shown = 0;
    uint8_t n = bag.distinctCount();
    for (uint8_t idx = 0; idx < n && shown < 4; idx++) {
      ItemKey k = bag.keyAt(idx);
      if (!itemUsableInBattle(k)) continue;
      if (shown < btlBagPage * 4) { shown++; continue; }
      int cell = shown - btlBagPage * 4;
      if (cell >= 4) break;
      int x = BTL_CELL_X(cell), y = BTL_CELL_Y(cell);
      bool live = !itemIsBall(k) || btlWild;   // a ball needs a wild target
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_INK : 0x8410);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(tcols(L(itemEntry(k).name)) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 8);
      gfx->print(L(itemEntry(k).name));
      gfx->setTextSize(1);
      char cnt[8];
      snprintf(cnt, sizeof(cnt), "x%u", bag.count(k));
      gfx->setCursor(x + 10, y + 28);
      gfx->print(cnt);
      shown++;
    }
    if (!shown) {
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(1);
      gfx->setCursor(CX - (int)tcols(T(S_BAG_EMPTY)) * 3, BTL_GRID_Y + 40);
      gfx->print(T(S_BAG_EMPTY));
    }
  } else if (btlMenu == 2) {
    drawBtlBack();
    // who to bring on instead; the current one and anything fainted is inert
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      bool usable = (i != btlSquadAt) && !m.fainted();
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(tcols(L(m.name)) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 8);
      gfx->print(L(m.name));
      gfx->setTextSize(1);
      char hp[20];
      snprintf(hp, sizeof(hp), "%u/%u", m.hp, m.maxHp);
      gfx->setCursor(x + 10, y + 28);
      gfx->print(hp);
    }
  } else {
    drawBtlBack();
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      uint8_t mv = btlYou.moves[i];
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, mv ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_INK);
      if (!mv) continue;
      const char *name = moveName(mv);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(tcols(name) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 7);
      gfx->print(name);
      // Same chip as the move list: in a fight the type IS the decision, and
      // grey 6px text was the least visible thing on the busiest screen.
      int cw = drawTypeChip(x + 10, y + 34, MOVE_TBL[mv].type);
      if (hasStab(btlYou.dex, MOVE_TBL[mv].type) &&
          MOVE_TBL[mv].cat != MC_STATUS) {
        gfx->setTextSize(1);
        gfx->setTextColor(DEX_TBL[btlYou.dex].accent);
        gfx->setCursor(x + 10 + cw + 4, y + 38);
        gfx->print("+");
      }
    }
  }
  gfx->flush();
}

// Brings on the flagged replacement and starts its entrance.
static void btlDoSwap() {
  uint32_t now = millis();
  if (btlSwapWho == 1 && btlLink) {
    btlFoeSquad[btlFoeAt] = btlFoe;
    // the next one still standing, not simply the next index
    uint8_t nxt = btlFoeAt;
    while (++nxt < btlFoeSquadN && btlFoeSquad[nxt].fainted()) {}
    if (nxt >= btlFoeSquadN) { btlSwapWho = -1; return; }
    btlFoeAt = nxt;
    btlFoe = btlFoeSquad[btlFoeAt];
    btlHpShown[1] = btlFoe.hp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), lan.peerName, L(btlFoe.name));
  } else if (btlSwapWho == 1) {
    const Trainer &t = BTL_TRAINERS[btlTrainer];
    btlFoeAt++;
    foeFromSpecies(btlFoe, t.team[btlFoeAt].dex, t.team[btlFoeAt].level,
                   btlHard ? HARD_IV : EASY_IV);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), L(t.name), L(btlFoe.name));
  } else if (btlSwapWho == 0) {
    btlSquad[btlSquadAt] = btlYou;     // remember how battered it was
    btlSquadAt++;
    btlYou = btlSquad[btlSquadAt];
    btlHpShown[0] = btlYou.hp;
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_GO), L(btlYou.name));
  }
  btlSwapWho = -1;
}

// Switching spends your turn: the opponent still acts. That is what stops it
// being a free look at the matchup every round.
static void btlSwitchTo(uint8_t i) {
  if (i >= btlSquadN || i == btlSquadAt) return;
  btlSquad[btlSquadAt] = btlYou;
  btlSquadAt = i;
  btlYou = btlSquad[i];
  btlHpShown[0] = btlYou.hp;
  btlSyncSprite(0, btlYou);
  btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
  btlEnterUntil[0] = millis() + BTL_ENTER_MS;
  btlMenu = 0;
  btlSay(T(S_BTL_GO), L(btlYou.name));
  audioCry(btlYou.dex);      // it announces itself as it steps in
  btlResolve(0);          // move 0 = no attack, so only the foe acts
}

int btlCellIndexAt(int16_t x, int16_t y) {
  for (int i = 0; i < 4; i++)
    if (btlCellHit(i, x, y)) return i;
  return -1;
}

// The way out of the move and switch screens. Without it the only exits were
// choosing something or leaving the fight entirely.
static void drawBtlBack() {
  gfx->fillRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_TRACK);
  gfx->drawRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_BACK)) * 6, BTL_BACK_Y + 14);
  gfx->print(T(S_BACK));
}

static bool btlBackTap(int16_t x, int16_t y) {
  if (x < BTL_BACK_X || x > BTL_BACK_X + BTL_BACK_W ||
      y < BTL_BACK_Y || y > BTL_BACK_Y + BTL_BACK_H) return false;
  btlMenu = 0;
  sfxPlay(SFX_TAP);
  return true;
}

// Running. A boss keeps their badge and the fight simply ends; against
// another device the peer is told, so it does not sit waiting for a move that
// will never come.
//
// A WILD fight is the exception: getting away is a roll, and failing it costs
// the turn. That is what stops a ball being risk-free -- you cannot throw one,
// see it fail and simply leave.
static void btlRun() {
  if (btlWild && !btlOver) {
    btlMenu = 0;
    if (random(100) >= wildEscapeChance(btlYou.level, btlFoe.level)) {
      sfxPlay(SFX_DENY);
      btlSay("%s", T(S_NO_ESCAPE));
      btlResolve(0);   // move 0 = no attack, so only the foe acts
      return;
    }
    // Narrated and left OVER rather than closed here, so the message is
    // actually seen: battleTap() dismisses a finished fight, and closing on the
    // same beat would flash the line for one frame.
    sfxPlay(SFX_TAP);
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlSay("%s", T(S_GOT_AWAY));
    return;
  }
  sfxPlay(SFX_DENY);
  btlFreeSprites();
  audioMusic(MUS_NONE);
  if (btlLink) { lanLeave(); btlLink = false; lanOpen = true; }
  battleOpen = false;
  btlWild = false;
  btlMenu = 0;
}

void battleTap(int16_t x, int16_t y) {
  if (btlWinUntil) {          // dismiss the win screen and leave the fight
    btlWinUntil = 0;
    btlFreeSprites();
    audioMusic(MUS_NONE);
    battleOpen = false;
    btlWild = false;
    if (btlLink) { btlLink = false; lanOpen = true; }
    return;
  }
  if (btlMsgCount) {          // one readable event per tap; never skip queued text
    for (uint8_t i = 1; i < btlMsgCount; i++)
      memcpy(btlMsg[i - 1], btlMsg[i], sizeof(btlMsg[0]));
    btlMsgCount--;
    if (btlMsgCount) return;
    if (btlOver) {
      btlFreeSprites();
      // Its twin above (the btlWinUntil dismiss) already stops the music; a
      // wild win/loss and a losing tamer fight end here instead and were the
      // one path that left MUS_BATTLE/MUS_VICTORY looping under the next screen.
      audioMusic(MUS_NONE);
      battleOpen = false;
      btlWild = false;
      // Back to the LAN screen rather than all the way out: that is where a
      // rematch is offered, and re-pairing for every fight would be tedious.
      if (btlLink) { btlLink = false; lanOpen = true; }
      return;
    }
    if (btlSwapWho >= 0) btlDoSwap();   // the replacement arrives on this beat
    return;
  }
  if (btlMenu == 0) {
    if (btlCellHit(0, x, y)) { sfxPlay(SFX_TAP); btlMenu = 1; return; }   // FIGHT
    if (btlCellHit(1, x, y)) {
      if (btlLink) { sfxPlay(SFX_DENY); return; }   // see renderBattle
      sfxPlay(SFX_TAP); btlMenu = 3; btlBagPage = 0; return;
    }
    if (btlCellHit(2, x, y)) { sfxPlay(SFX_TAP); btlMenu = 2; return; }
    if (btlCellHit(3, x, y)) { btlRun(); return; }
    return;
  }
  if (btlMenu == 3) {
    if (btlBackTap(x, y)) return;
    uint8_t shown = 0;
    uint8_t n = bag.distinctCount();
    for (uint8_t idx = 0; idx < n; idx++) {
      ItemKey k = bag.keyAt(idx);
      if (!itemUsableInBattle(k)) continue;
      if (shown < btlBagPage * 4) { shown++; continue; }
      int cell = shown - btlBagPage * 4;
      if (cell >= 4) break;
      shown++;
      if (!btlCellHit(cell, x, y)) continue;
      // ONE predicate decides whether a ball may be thrown, and the draw above
      // asks the same one -- a greyed cell that still worked is the exact fault
      // uiButtonDisabled() exists to prevent on the home row.
      if (itemIsBall(k)) {
        if (!btlWild) { sfxPlay(SFX_DENY); return; }
        btlThrowBall(k);
      } else {
        btlUseItem(k);
      }
      return;
    }
    btlMenu = 0;      // anywhere else backs out
    return;
  }
  if (btlMenu == 2) {
    if (btlBackTap(x, y)) return;
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      if (!btlCellHit(i, x, y)) continue;
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      if (i == btlSquadAt || m.fainted()) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (btlLink && !btlLinkHost) {
        // The guest asks; it never switches on its own. A switch rides the same
        // message as a move, so the host spends the turn on it exactly as it
        // would for us.
        lan.sendAct(LINK_ACT_SWITCH_TO(i));
        btlMenu = 0;
        return;
      }
      if (btlLink) {            // host: latched like a move, see btlLinkPoll
        btlMyAct = LINK_ACT_SWITCH_TO(i);
        btlMenu = 0;
        if (!lan.hasPeerAct()) return;
        btlMyAct = 0;
      }
      btlSwitchTo(i);
      return;
    }
    btlMenu = 0;      // anywhere else backs out
    return;
  }
  if (btlBackTap(x, y)) return;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!btlYou.moves[i]) continue;
    if (!btlCellHit(i, x, y)) continue;
    sfxPlay(SFX_TAP);
    btlMenu = 0;
    if (btlLink && !btlLinkHost) {
      lan.sendAct(LINK_ACT_MOVE(i));   // the guest asks; the host decides
      return;
    }
    if (btlLink) {
      // Latched, not discarded. The host used to throw the tap away when the
      // rival had not chosen yet, so you had to keep jabbing at the move until
      // the timing happened to line up.
      btlMyAct = LINK_ACT_MOVE(i);
      if (!lan.hasPeerAct()) return;   // resolved by btlLinkPoll when it lands
      btlMyAct = 0;
    }
    btlResolve(btlYou.moves[i]);
    return;
  }
  btlMenu = 0;        // a tap off the grid goes back to FIGHT/DIGIMON
}

// ---------- player card (swipe down) ----------
// Everything here is player-wide and outlives the creature: badges, the daily
// streak, the Digidex and the party. No player sprite yet -- the SD carries
// PMD creature sprites only, so a tamer portrait needs new art.
// Page 1: who you are -- avatar, badges, totals. Tap the avatar to cycle it;
// the four sprites are hand-drawn (see Digimon.h) because SpriteCollab has no
// tamer art and ripped sprites would be unlicensed.
static void renderPlayerBadges() {
  // the player's name if they have set one, the generic title if not; either
  // way tapping it opens the keyboard
  const char *tn = pet.trainerName[0] ? pet.trainerName : T(S_TRAINER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)tcols(tn) * 9, 40);
  gfx->print(tn);

  // Pages 1 and 2 are the other versions' ladders: name them, and drop the
  // avatar so the badges have the room. Only page 0 is "you".
  if (playerBadgeRegion != 0) {
    const char *rn = TRAINER_SETS[playerBadgeRegion].region;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(rn) * 6, 120);
    gfx->print(rn);
  } else if (gShowAllAvatars) {          // emulator only: every avatar at once
    for (uint8_t i = 0; i < AVATAR_COUNT; i++)
      drawAvatar(i, 60 + (i % 4) * 88, 60 + (i / 4) * 60, 3);
  } else {
    drawAvatar(pet.avatar, CX - AVATAR_PX * 2, 72, 4);
    // the sprite alone is not always obvious at 16x16, so it is named
    const char *an = L(AVATARS[pet.avatar % AVATAR_COUNT].name);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(an) * 3, 143);
    gfx->print(an);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(CX - (int)tcols(T(S_AVATAR_HINT)) * 3, 154);
    gfx->print(T(S_AVATAR_HINT));
  }

  // The real badges, 2x4. Unearned ones draw as a faint outline so the shape
  // of what is missing is still visible.
  for (int i = 0; i < TRAINER_GYMS; i++) {
    int bx = 140 + (i % 4) * 62, by = 188 + (i / 4) * 62;
    bool got = pet.hasBadge(playerBadgeRegion, i, false);
    bool hard = pet.hasBadge(playerBadgeRegion, i, true);
    if (hard) {
      // Beaten on hard: a golden halo. Concentric rings, not a filled disc --
      // a disc sat behind the art and read as a gold coin rather than a glow.
      gfx->drawCircle(bx, by, 25, 0xFDE0);
      gfx->drawCircle(bx, by, 24, 0xFEA0);
      gfx->drawCircle(bx, by, 23, 0xFF60);
      gfx->drawCircle(bx, by, 22, 0xFEA0);
      gfx->drawCircle(bx, by, 21, 0xFDE0);
    }
    if (!got) {
      gfx->drawCircle(bx, by, 20, UI_TRACK);
      continue;
    }
    const BadgeArt *a = badgeArtFor(playerBadgeRegion, i);
    if (!a) {                 // no art for this version: a filled disc, won is won
      gfx->fillCircle(bx, by, 14, typeColor(TRAINER_SETS[playerBadgeRegion % GYM_REGIONS].list[i].type));
      gfx->drawCircle(bx, by, 14, UI_INK);
      continue;
    }
    for (int r = 0; r < BADGE_PX; r++)
      for (int c = 0; c < BADGE_PX; c++) {
        uint8_t v = a->idx[r * BADGE_PX + c];
        if (v == 0xFF) continue;
        gfx->fillRect(bx - BADGE_PX / 2 + c, by - BADGE_PX / 2 + r, 1, 1, a->pal[v]);
      }
  }
  char l[32];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  snprintf(l, sizeof(l), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setCursor(CX - (int)tcols(l) * 6, 286);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_DIGIDEX_FMT), pet.registeredCount(), DEX_COUNT);
  gfx->setCursor(CX - (int)tcols(l) * 6, 312);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_PARTY_FMT), party.count());
  gfx->setCursor(CX - (int)tcols(l) * 6, 338);
  gfx->print(l);
}

// Page 2: the medals. They used to sit on the creature's card; they belong with
// the player, since totalMedals accumulates across every pet you raise.
static void renderPlayerMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(head) * 9, 44);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 46 + (i % 2) * 190, y = 96 + (i / 2) * 58;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 180, 48, 10, g ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(x, y, 180, 48, 10, UI_INK);
    gfx->setTextColor(g ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(x + (180 - (int)tcols(medalLabel(i)) * 12) / 2, y + 6);
    gfx->print(medalLabel(i));
    gfx->setTextSize(1);
    gfx->setCursor(x + (180 - (int)tcols(medalDesc(i)) * 6) / 2, y + 30);
    gfx->print(medalDesc(i));
  }
  char tot[28];
  snprintf(tot, sizeof(tot), T(S_MEDALS_TOTAL_FMT), pet.totalMedals);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(tot) * 3, 344);
  gfx->print(tot);
}

// The trait's name. Indexed by the PERS_* value, in the same order as the enum,
// so there is no switch that could disagree with it.
const char *personalityName() {
  static const StrId NAMES[Pet::PERSONALITY_COUNT] = {
    S_PERS_BOLD, S_PERS_STURDY, S_PERS_BRISK, S_PERS_HARDY, S_PERS_EAGER, S_PERS_CALM,
  };
  return T(NAMES[pet.personality() % Pet::PERSONALITY_COUNT]);
}

// ---------------------------------------------------------------------------
// TODAY'S CARE CHECKLIST.
//
// NON-DESTRUCTIVE, and that is the whole design rather than a caveat. Every line
// is COMPUTED from state the save already holds, so:
//   * nothing new is written, no field is added, no migration exists to get wrong;
//   * the list resets itself with the day, because lastCareDay and the bars
//     already do;
//   * and there is deliberately NO REWARD. A reward would have to be marked as
//     claimed, which means a per-day persisted flag, which is the migration this
//     avoids -- and the fork this idea came from pays for its version by
//     CONSUMING A LIVING CREATURE, which is not a trade worth making. The value
//     here is answering "what does it need right now", and a checklist does that
//     without being a currency.
//
// Every check reads PERSISTED state rather than a "did you do X today" counter.
// bondToday would have been the obvious source for a training goal and is RAM
// only, so it reads false after a reboot -- a checklist that forgets what you did
// because the device restarted is worse than one line shorter.
struct GoalDef {
  StrId label;
  bool (*met)();
};
static bool goalCared()  { return pet.caredToday(); }
static bool goalFed()    { return pet.fullness >= 70; }
static bool goalClean()  { return pet.poops == 0 && pet.hygiene >= 70; }
static bool goalHappy()  { return pet.joy >= 70; }
static bool goalRested() { return pet.energy >= 70; }
static const GoalDef GOALS[] = {
  { S_GOAL_CARED,  goalCared  },
  { S_GOAL_FED,    goalFed    },
  { S_GOAL_CLEAN,  goalClean  },
  { S_GOAL_HAPPY,  goalHappy  },
  { S_GOAL_RESTED, goalRested },
};
#define GOAL_COUNT ((int)(sizeof(GOALS) / sizeof(GOALS[0])))
// Its metrics live up with the other layout constants; uiRowHeights() holds
// GOAL_ROW_H to the hard floor far above here.

// Published so a test can drive the firmware's own answers rather than
// re-implementing five predicates and proving only the transcription.
int uiGoalCount() { return GOAL_COUNT; }
bool uiGoalMet(int i) { return i >= 0 && i < GOAL_COUNT && GOALS[i].met(); }
int uiGoalsMet() {
  int n = 0;
  for (int i = 0; i < GOAL_COUNT; i++) if (GOALS[i].met()) n++;
  return n;
}

static void renderPlayerGoals() {
  const int done = uiGoalsMet();
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_GOALS)) * 9, 44);
  gfx->print(T(S_GOALS));
  char sub[24];
  snprintf(sub, sizeof(sub), T(S_GOALS_FMT), done, GOAL_COUNT);
  gfx->setTextColor(done == GOAL_COUNT ? UI_BAR_OK : UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(sub) * 6, 76);
  gfx->print(sub);

  for (int i = 0; i < GOAL_COUNT; i++) {
    const int ry = GOAL_ROW_Y(i);
    // As wide as the glass allows AT THIS HEIGHT, the same rule every other row
    // list here follows -- one width for all five would hang the last one's
    // corners off the bezel.
    const int half = uiSafeHalfWidthFor(ry, ry + GOAL_ROW_H) - 18;
    const int rx = CX - half;
    const bool met = GOALS[i].met();
    gfx->fillRoundRect(rx, ry, half * 2, GOAL_ROW_H, 10, met ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(rx, ry, half * 2, GOAL_ROW_H, 10, UI_INK);
    // A tick or an empty box, so the state does not rest on colour alone -- the
    // filled green and the grey track are close in luminance and this screen has
    // to read for somebody who cannot tell them apart.
    const int bx = rx + 14, by = ry + GOAL_ROW_H / 2;
    if (met) {
      gfx->fillCircle(bx + 8, by, 9, UI_BG_DAY);
      gfx->drawLine(bx + 4, by, bx + 7, by + 4, UI_INK);
      gfx->drawLine(bx + 5, by, bx + 8, by + 4, UI_INK);
      gfx->drawLine(bx + 7, by + 4, bx + 13, by - 5, UI_INK);
      gfx->drawLine(bx + 8, by + 4, bx + 14, by - 5, UI_INK);
    } else {
      gfx->drawCircle(bx + 8, by, 9, UI_INK_SOFT);
    }
    const char *l = T(GOALS[i].label);
    gfx->setTextColor(met ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(bx + 30, by - 8);
    gfx->print(l);
  }
}

void renderPlayer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (playerPage < GYM_REGIONS) renderPlayerBadges();
  else if (playerPage == GYM_REGIONS) renderPlayerMedals();
  else renderPlayerGoals();

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

// ---------- reaction test (trains SPEED) ----------
// The third training verb. The bag is a masher and the ball is a juggler, so
// this one is a reaction: a target appears somewhere on the panel and you tap
// it before it expires. The window shrinks as you go, which is what makes it
// read as speed rather than as endurance.
#define SPD_MS 15000UL       // session length
#define SPD_LIFE0 1100       // first target's window, ms
#define SPD_LIFE_MIN 380
#define SPD_R 46             // target radius

void spdSpawn() {
  // Keep the whole target inside the bezel: pick an angle and a radius that
  // leave SPD_R of margin, rather than a square that clips at the corners.
  int ang = random(360);
  int rad = random(150);
  float a = ang * 3.14159f / 180.0f;
  spdX = CX + (int)(cosf(a) * rad);
  spdY = CY + (int)(sinf(a) * rad);
  spdBorn = millis();
}

void startSpeedGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  spdOpen = true;
  spdUntil = millis() + SPD_MS;
  spdOverUntil = 0;
  spdHits = 0;
  spdMisses = 0;
  spdGain = 0;
  spdNewHi = false;
  spdSpawn();
}

static uint16_t spdLife() {
  int life = SPD_LIFE0 - spdHits * 32;
  return life < SPD_LIFE_MIN ? SPD_LIFE_MIN : life;
}

void spdTap(int16_t x, int16_t y) {
  if (spdOverUntil) return;
  int dx = x - spdX, dy = y - spdY;
  if (dx * dx + dy * dy <= (SPD_R + 14) * (SPD_R + 14)) {
    spdHits++;
    sfxPlay(SFX_TAP);
    spdSpawn();
  }
}

void renderSpeed() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (spdOverUntil) {
    if (now > spdOverUntil) { spdOpen = false; return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_SCORE_FMT), spdHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - tcols(b) * 12, 150);
    gfx->print(b);
    char g[20];
    snprintf(g, sizeof(g), T(S_SPD_GAIN_FMT), spdGain);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(3);
    gfx->setCursor(CX - tcols(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (spdNewHi && spdHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - tcols(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[20];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.spdHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - tcols(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // session over?
  if (now >= spdUntil) {
    spdNewHi = (spdHits > pet.spdHi);
    spdGain = pet.trainSpeed(spdHits);
    sfxPlay(spdNewHi ? SFX_MEDAL : SFX_PLAY);
    spdOverUntil = now + 3500;
    gfx->flush();
    return;
  }
  // target expired?
  if (now - spdBorn > spdLife()) {
    spdMisses++;
    spdSpawn();
  }

  // the target, shrinking as its window runs out so the urgency is visible
  uint32_t age = now - spdBorn;
  int life = spdLife();
  int r = SPD_R - (int)((uint32_t)SPD_R * age / (life ? life : 1) / 2);
  if (r < 8) r = 8;
  gfx->fillCircle(spdX, spdY, r, UI_BAR_BAD);
  gfx->fillCircle(spdX, spdY, r * 2 / 3, UI_WHITE);
  gfx->fillCircle(spdX, spdY, r / 3, UI_BAR_BAD);

  char b[12];
  snprintf(b, sizeof(b), "%u", spdHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - tcols(b) * 12, 30);
  gfx->print(b);
  // seconds left
  uint32_t left = (spdUntil > now) ? (spdUntil - now + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%us", (unsigned)left);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(b) * 6, 76);
  gfx->print(b);
  gfx->flush();
}

// ---------- memory game (joy and bond, no stat) ----------
//
// The fourth thing to do with a creature, and deliberately NOT a fourth stat
// tamer: the bag trains ATK, the reaction test SPE and the ball DEF, so there
// is no stat left that a new game would not simply duplicate. This one pays in
// joy and bond, which makes it the game you play when the creature is too worn
// out for the other three -- it costs almost no energy and burns no weight.
//
// Four pads flash a sequence and you repeat it. One more step each round, so the
// score IS the longest sequence remembered, and the difficulty curve is the
// player's memory rather than a timer that gets meaner.
// Its metrics live up with the other layout constants, for the same reason the
// settings rows do: uiButtonHeights() and uiMemoPadR() sit far above this.

uint32_t memoOverUntil = 0;
uint8_t memoSeq[MEMO_MAX];
uint8_t memoLen = 0;         // how long the sequence currently is
uint8_t memoAt = 0;          // how much of it the player has repeated
uint8_t memoShowAt = 0;      // which step is being shown, while showing
bool memoShowing = false;
uint32_t memoStepUntil = 0;  // when the current shown step (or gap) ends
bool memoGap = false;
uint32_t memoDeadline = 0;   // when the player's patience runs out
int8_t memoLit = -1;         // which pad is lit right now, -1 for none
uint8_t memoBest = 0;        // longest sequence completed this session
bool memoWrong = false;      // the last answer was wrong, for one flash of red
bool memoNewHi = false;
uint8_t memoGain = 0;

// The pads sit on a ring, like the party slots, so the round panel is used rather
// than fought. ONE answer for where a pad is: the draw path and the tap path both
// ask, which is the rule every other control here follows.
void memoPadPos(int i, int *cx, int *cy) {
  const float a = (-90.0f + 90.0f * (float)i) * 0.01745329f;
  if (cx) *cx = CX + (int)(cosf(a) * MEMO_RING);
  if (cy) *cy = CY + (int)(sinf(a) * MEMO_RING);
}
int memoPadAt(int16_t x, int16_t y) {
  for (int i = 0; i < MEMO_PADS; i++) {
    int px, py;
    memoPadPos(i, &px, &py);
    const int dx = x - px, dy = y - py;
    if (dx * dx + dy * dy <= MEMO_R * MEMO_R) return i;
  }
  return -1;
}
static uint16_t memoPadColour(int i) {
  static const uint16_t C[MEMO_PADS] = {
    C565(0xe0, 0x5a, 0x5a), C565(0x4c, 0xa8, 0xe0),
    C565(0x5c, 0xc0, 0x6a), C565(0xe8, 0xc0, 0x4a),
  };
  return C[i % MEMO_PADS];
}

// Extends the sequence and starts showing it from the beginning. Replaying the
// WHOLE sequence each round is the point -- that is what makes it memory rather
// than reaction.
static void memoNextRound() {
  if (memoLen < MEMO_MAX) memoSeq[memoLen++] = (uint8_t)random(MEMO_PADS);
  memoAt = 0;
  memoShowAt = 0;
  memoShowing = true;
  memoGap = true;                       // a beat of dark before the first pad
  memoLit = -1;
  memoStepUntil = millis() + MEMO_GAP_MS * 2;
}

void startMemoGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  memoOpen = true;
  memoOverUntil = 0;
  memoLen = 0;
  memoBest = 0;
  memoWrong = false;
  memoNewHi = false;
  memoGain = 0;
  memoNextRound();
}

// Ends the session and banks what was earned. Its own function because three
// paths reach it -- a wrong answer, running out of patience, and walking away --
// and the reward must be applied exactly once however it ended.
static void memoFinish() {
  if (memoOverUntil) return;            // already ended
  memoNewHi = (memoBest > pet.memoHi);
  memoGain = pet.playMemo(memoBest);
  sfxPlay(memoNewHi && memoBest > 0 ? SFX_MEDAL : SFX_LEVEL);
  memoOverUntil = millis() + 3500;
}

// Leaving early BANKS what was earned rather than voiding it, exactly as the other
// three do. Quitting used to be the only way to lose a session's work, and it was
// removed from all of them for the same reason.
void leaveMemo() {
  memoFinish();
  memoOpen = false;
}

void memoTap(int16_t x, int16_t y) {
  if (memoOverUntil || memoShowing) return;   // not your turn yet
  const int pad = memoPadAt(x, y);
  if (pad < 0) return;
  memoLit = pad;                              // light what was pressed
  memoStepUntil = millis() + 160;
  if (memoSeq[memoAt] != (uint8_t)pad) {
    memoWrong = true;
    sfxPlay(SFX_DENY);
    memoFinish();
    return;
  }
  sfxPlay(SFX_TAP);
  memoAt++;
  memoDeadline = millis() + MEMO_INPUT_MS;
  if (memoAt >= memoLen) {                    // the whole sequence, repeated
    if (memoLen > memoBest) memoBest = memoLen;
    if (memoLen >= MEMO_MAX) { memoFinish(); return; }
    sfxPlay(SFX_PLAY);
    memoNextRound();
  }
}

void renderMemo() {
  const uint32_t now = millis();
  drawGameScene();
  const bool night = sceneHour() < 6 || sceneHour() >= 20;
  const uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (memoOverUntil) {
    if (now > memoOverUntil) { memoOpen = false; gfx->flush(); return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_MEMO_FMT), memoBest);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - tcols(b) * 12, 150);
    gfx->print(b);
    char g[22];
    snprintf(g, sizeof(g), T(S_PLUS_JOY_FMT), memoGain);
    gfx->setTextColor(C565(0xd4, 0x52, 0x7e));
    gfx->setTextSize(3);
    gfx->setCursor(CX - tcols(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (memoNewHi && memoBest > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - tcols(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[22];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.memoHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - tcols(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // --- advance the sequence being shown, or the player's patience
  if (memoShowing) {
    if (now >= memoStepUntil) {
      if (memoGap) {                    // the gap just ended: light the next pad
        if (memoShowAt >= memoLen) {
          memoShowing = false;          // the whole sequence has been shown
          memoLit = -1;
          memoDeadline = now + MEMO_INPUT_MS;
        } else {
          memoLit = memoSeq[memoShowAt];
          memoGap = false;
          memoStepUntil = now + MEMO_FLASH_MS;
        }
      } else {                          // the flash just ended
        memoLit = -1;
        memoShowAt++;
        memoGap = true;
        memoStepUntil = now + MEMO_GAP_MS;
      }
    }
  } else {
    if (memoLit >= 0 && now >= memoStepUntil) memoLit = -1;
    // Running out of patience ends the session rather than counting as a wrong
    // answer, so putting the device down mid-round still banks what was earned.
    if (memoDeadline && now >= memoDeadline) { memoFinish(); gfx->flush(); return; }
  }

  for (int i = 0; i < MEMO_PADS; i++) {
    int px, py;
    memoPadPos(i, &px, &py);
    const bool lit = (memoLit == i);
    const uint16_t base = memoPadColour(i);
    gfx->fillCircle(px, py, MEMO_R, lit ? UI_WHITE : base);
    if (lit) gfx->fillCircle(px, py, MEMO_R - 10, base);
    gfx->drawCircle(px, py, MEMO_R, ink);
    gfx->drawCircle(px, py, MEMO_R - 1, ink);
  }

  // How far in you are, in the middle where the pads leave a hole
  char c[10];
  snprintf(c, sizeof(c), "%u", memoShowing ? memoLen : memoAt);
  gfx->setTextColor(memoWrong ? UI_BAR_BAD : ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - tcols(c) * 12, CY - 16);
  gfx->print(c);

  // WATCH or YOUR TURN. Without it the two halves of a round look identical and
  // there is no way to know whether a tap is expected.
  const char *hint = memoShowing ? T(S_MEMO_WATCH) : T(S_MEMO_GO);
  gfx->setTextColor(ink);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(hint) * 6, 42);
  gfx->print(hint);
  char rec[22];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.memoHi);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(rec) * 3, 74);
  gfx->print(rec);
  gfx->flush();
}

// ---------- team select ----------
// Which creatures come to this fight. It exists because hard mode caps your
// team to the boss's size, so the difference between a sweep and a wipe is
// bringing the right type -- and the squad used to be simply whoever sat first
// in the party.

// candidate n: 0 = the live pet, 1..PARTY_SLOTS = banked members
bool pickExists(uint8_t n) {
  if (n == 0) return !pet.isEgg();
  return n <= PARTY_SLOTS && !party.slots[n - 1].empty();
}
uint8_t pickChosen() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n) && (squadMask & (1 << n))) c++;
  return c;
}
uint8_t pickCandidates() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n)) c++;
  return c;
}
// Trims the selection to the first `cap` candidates. The default used to be
// "everything", which with a live pet plus six banked is seven against a cap of
// six -- so the screen opened already invalid.
void pickDefault(uint8_t cap) {
  squadMask = 0;
  uint8_t taken = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS && taken < cap; n++)
    if (pickExists(n)) { squadMask |= (1 << n); taken++; }
}

static void drawPickCell(uint8_t n, int x, int y, uint8_t capLvl) {
  bool on = (squadMask & (1 << n)) != 0;
  int16_t dex; uint16_t lvl; const char *nm; bool variant;
  if (n == 0) {
    dex = pet.speciesId; lvl = pet.level(); variant = pet.variant;
    nm = pet.nick[0] ? pet.nick : L(DEX_TBL[dex].name);
  } else {
    const PartyMon &m = party.slots[n - 1];
    dex = m.dex; lvl = m.level; variant = m.variant;
    nm = m.nick[0] ? m.nick : L(DEX_TBL[dex].name);
  }
  if (capLvl && lvl > capLvl) lvl = capLvl;   // show the level it will FIGHT at
  gfx->fillRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_BG_DAY : UI_TRACK);
  gfx->drawRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_INK : 0x8410);
  const uint8_t *th = thumbs.get(dex);
  if (th) drawThumb(th, x - 12, y - 6, 2, !on);
  gfx->setTextColor(on ? UI_INK : 0x8410);
  gfx->setTextSize(1);
  gfx->setCursor(x + 54, y + 14);
  gfx->print(nm);
  char l[16];
  snprintf(l, sizeof(l), "Lv.%u%s", (unsigned)lvl, variant ? " *" : "");
  gfx->setCursor(x + 54, y + 30);
  gfx->print(l);
  // its typing is the whole reason you are on this screen
  const DexEntry &d = DEX_TBL[dex];
  gfx->setTextColor(on ? d.accent : 0x8410);
  gfx->setCursor(x + 54, y + 48);
  gfx->print(typeName(d.type1));
  if (d.type2 != T_NONE) {
    gfx->setCursor(x + 54, y + 60);
    gfx->print(typeName(d.type2));
  }
  if (on) {
    gfx->fillCircle(x + PICK_CELL_W - 16, y + 16, 9, UI_BAR_OK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(x + PICK_CELL_W - 19, y + 13);
    gfx->print("*");
  }
}

void renderPick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t cap = squadCap(pickTrainer, pickHard);
  uint8_t top = 0;          // the level cap shown on each cell; 0 = uncapped
  char head[40];
  if (pickTrainer == PICK_LAN) {
    snprintf(head, sizeof(head), "%s: %s", T(S_LAN),
             lanWantHost ? T(S_LAN_HOST) : T(S_LAN_JOIN));
  } else {
    const Trainer &t = PICK_TRAINERS[pickTrainer];
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    snprintf(head, sizeof(head), "%s  Lv.%u x%u", L(t.name), top, t.count);
  }
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(head) * 6, 44);
  gfx->print(head);
  char sub[28];
  snprintf(sub, sizeof(sub), T(S_PICK_FMT), pickChosen(), cap);
  gfx->setTextSize(1);
  gfx->setTextColor(pickChosen() > cap ? UI_BAR_BAD : UI_TRACK);
  gfx->setCursor(CX - (int)tcols(sub) * 3, 68);
  gfx->print(sub);

  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    drawPickCell(n, PICK_X(drawn), PICK_Y(drawn), top);
    drawn++;
  }
  uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
  if (!pages) pages = 1;
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == pickPage) gfx->fillCircle(dx, 332, 5, UI_INK);
    else gfx->drawCircle(dx, 332, 4, UI_INK);
  }

  bool ok = pickChosen() > 0 && pickChosen() <= cap;
  gfx->fillRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_TRACK);
  gfx->drawRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(PICK_BACK_X + (PICK_BTN_W - (int)tcols(T(S_BACK)) * 12) / 2,
                 PICK_GO_Y + 14);
  gfx->print(T(S_BACK));
  gfx->fillRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12,
                     ok ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(ok ? UI_BG_DAY : 0x8410);
  gfx->setTextSize(2);
  gfx->setCursor(PICK_GO_X + (PICK_BTN_W - (int)tcols(T(S_FIGHT)) * 12) / 2,
                 PICK_GO_Y + 14);
  gfx->print(T(S_FIGHT));
  gfx->flush();
}

void pickTap(int16_t x, int16_t y) {
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_BACK_X && x <= PICK_BACK_X + PICK_BTN_W) {   // BACK
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) { lanOpen = true; }
    else { gymOpen = true; }
    return;
  }
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_GO_X && x <= PICK_GO_X + PICK_BTN_W) {
    uint8_t cap = squadCap(pickTrainer, pickHard);
    if (pickChosen() == 0 || pickChosen() > cap) return;   // GO stays inert
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) {
      // The squad is chosen BEFORE the radio comes up, so what gets offered to
      // the peer is what the player picked -- lanOffer() builds lan.mine from
      // squadMask, and the fight is then rebuilt from lan.mine rather than from
      // the party (see startLinkBattle).
      lanOffer(lanWantHost);
      lanOpen = true;
      return;
    }
    // The fight has to be the one the picker just showed you. startTrainerBattle()
    // snapshots gymRegion into btlRegion, so handing the picker's own version back
    // to the chooser here is what keeps the cap you were shown, the boss you
    // were shown and the ladder the badge lands on all the same version -- without
    // giving the function a second source of truth to disagree with.
    gymRegion = pickRegion;
    startTrainerBattle(pickTrainer, pickHard);
    return;
  }
  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    int cx0 = PICK_X(drawn), cy0 = PICK_Y(drawn);
    drawn++;
    if (x < cx0 || x > cx0 + PICK_CELL_W || y < cy0 || y > cy0 + PICK_CELL_H) continue;
    squadMask ^= (1 << n);
    sfxPlay(SFX_TAP);
    return;
  }
}

// ---------- LAN battle ----------
// Pairing on a touch-only screen: one device hosts, the other joins, and the
// protocol does the rest. There is no MAC entry because there is no keyboard
// worth typing one on.
void renderLan() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_LAN)) * 6, 44);
  gfx->print(T(S_LAN));

  const char *msg = T(S_LAN_PICK);
  switch (lan.state) {
    case LINK_HANDSHAKE:
    case LINK_LISTENING: msg = T(S_LAN_WAIT); break;
    case LINK_SQUADS:    msg = T(S_LAN_WAIT); break;
    case LINK_READY:     msg = T(S_LAN_READY); break;
    case LINK_REFUSED:   msg = T(S_LAN_REFUSED); break;
    case LINK_LOST:      msg = T(S_LAN_GONE); break;
    case LINK_DONE:      msg = lan.youWon ? T(S_BTL_WIN) : T(S_BTL_LOSE); break;
    default: break;
  }
  gfx->setTextColor((lan.state == LINK_REFUSED || lan.state == LINK_LOST)
                      ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(msg) * 3, 76);
  gfx->print(msg);

  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    const char *lab[2] = { T(S_LAN_HOST), T(S_LAN_JOIN) };
    for (int i = 0; i < 2; i++) {
      int y = 120 + i * 70;
      gfx->fillRoundRect(90, y, 286, 56, 12, UI_BG_DAY);
      gfx->drawRoundRect(90, y, 286, 56, 12, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)tcols(lab[i]) * 6, y + 20);
      gfx->print(lab[i]);
    }
  } else if (lan.state == LINK_READY) {
    char l[40];
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)tcols(lan.peerName) * 6, 130);
      gfx->print(lan.peerName);
    }
    snprintf(l, sizeof(l), T(S_LAN_VS), lan.theirsN);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(l) * 6, 150);
    gfx->print(l);
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BAR_OK);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(CX - tcols(T(S_FIGHT)) * 6, 240);
    gfx->print(T(S_FIGHT));
  } else if (lan.state == LINK_DONE) {
    // Both squads are still in hand on both devices, so going again costs one
    // packet -- there is nothing to re-exchange.
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)tcols(lan.peerName) * 6, 140);
      gfx->print(lan.peerName);
    }
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BG_DAY);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(T(S_LAN_REMATCH)) * 6, 240);
    gfx->print(T(S_LAN_REMATCH));
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// Hands our chosen squad to the link, then announces.
// Leaving deliberately: tell the peer so it reports at once instead of sitting
// out the timeout, then free the radio -- it costs real current.
void lanLeave() {
  if (lan.live()) lan.sendBye();
  linkNowEnd();
  lan.state = LINK_OFF;
}

static void lanOffer(bool host) {
  // The squad is built BEFORE the radio is touched. What we advertise has to be
  // exactly what the player just chose in the picker, and that does not depend
  // on whether the radio comes up -- doing it the other way round meant a
  // failed radio skipped the squad entirely and left nothing to inspect.
  lan.begin(host, pet.trainerName);
  snprintf(lan.peerName, sizeof(lan.peerName), "%s", pet.trainerName);
  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  for (uint8_t i = 0; i < btlSquadN; i++) {
    LinkMon m;
    linkMonFrom(m, btlSquad[i]);
    lan.addMon(m);
  }
  if (!linkNowBegin(&lan)) {          // no radio: say so rather than hanging
    lan.state = LINK_REFUSED;
    return;
  }
  // BOTH sides announce. Which of them ends up hosting is settled by id inside
  // the hello, so the buttons are only a preference -- two players who both tap
  // HOST still get a working fight instead of two authorities, and two who both
  // tap JOIN still get one instead of mutual silence.
  lan.start();
}

void lanTap(int16_t x, int16_t y) {
  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    for (int i = 0; i < 2; i++) {
      int by = 120 + i * 70;
      if (x < 90 || x > 376 || y < by || y > by + 56) continue;
      sfxPlay(SFX_TAP);
      lanWantHost = (i == 0);
      lanOpen = false;
      pickTrainer = PICK_LAN;
      pickHard = false;
      pickRegion = gymRegion;   // no ladder in a LAN fight; kept in step anyway
      pickPage = 0;
      pickDefault(squadCap(PICK_LAN, false));
      pickOpen = true;
      return;
    }
  } else if (lan.state == LINK_READY) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lanOpen = false;
      startLinkBattle();
      return;
    }
  } else if (lan.state == LINK_DONE) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lan.sendRematch();      // both sides go back to READY and tap FIGHT
      return;
    }
  }
  if (y > 370) { lanLeave(); lanOpen = false; }   // back
}

// ---------- boss list ----------
void renderGyms() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // The ladder's own version in the title, since a vertical swipe moves between
  // three of them and "GYMS" alone would not say which you are looking at.
  char title[28];
  snprintf(title, sizeof(title), "%s %s", TRAINER_SETS[gymRegion % GYM_REGIONS].region,
           T(S_GYMS));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(title) * 6, 42);
  gfx->print(title);
  // The badge count used to sit here, directly behind the difficulty pill. The
  // version chooser already shows it per version, which is where you are choosing
  // from, so it was both redundant and in the way.
  // difficulty pill: hard caps YOUR team to the boss's size and level, so it
  // is a different ladder with its own badges rather than a damage multiplier
  const char *dif = T(gymHard ? S_HARD : S_EASY);
  int dw = (int)tcols(dif) * 12 + 48;      // wider as well as taller
  if (dw < 120) dw = 120;
  gfx->fillRoundRect(CX - dw / 2, GYMDIF_Y, dw, GYMDIF_H, 12,
                     gymHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(gymHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(dif) * 6, GYMDIF_Y + 14);
  gfx->print(dif);

  for (int i = 0; i < GYM_ROWS; i++) {
    uint8_t idx = gymPage * GYM_ROWS + i;
    if (idx >= TRAINER_COUNT) break;
    const Trainer &t = TRAINERS[idx];
    int rx, y, rw, rh;
    gymRowRect(i, &rx, &y, &rw, &rh);
    bool done = pet.hasBadge(gymRegion, idx, gymHard);
    bool open_ = gymUnlocked(idx, gymHard);
    gfx->fillRoundRect(rx, y, rw, rh, 10, done ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(rx, y, rw, rh, 10, open_ ? UI_INK : UI_TRACK);
    gfx->setTextColor(open_ ? UI_INK : UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(rx + 14, y + 8);
    gfx->print(L(t.name));
    gfx->setTextSize(1);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(rx + 14, y + 28);
    gfx->print(open_ ? L(t.place) : T(S_LOCKED));
    // the level of the strongest creature: the honest measure of the wall
    uint8_t top = 0;
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u x%u", top, t.count);
    gfx->setTextColor(done ? UI_BAR_OK : (open_ ? UI_INK : UI_TRACK));
    gfx->setCursor(rx + rw - 12 - (int)tcols(lv) * 6, y + 28);
    gfx->print(lv);
    if (done) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setCursor(rx + rw - 26, y + 8);
      gfx->print("*");
    }
  }
  gfx->fillRoundRect(GYMBTN_X, GYMBTN_Y, GYMBTN_W, GYMBTN_H, 11, UI_BG_DAY);
  gfx->drawRoundRect(GYMBTN_X, GYMBTN_Y, GYMBTN_W, GYMBTN_H, 11, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(GYMBTN_X + (GYMBTN_W - (int)tcols(T(S_LAN)) * 12) / 2, GYMBTN_Y + 14);
  gfx->print(T(S_LAN));
  uiChrome();
  gfx->flush();
}

// The version pill under a waiting egg. Tapping it cycles; Pet::setRegion swaps
// the egg to that version's creature, keeping the rarity it was granted and
// remembering each version's answer so flipping back and forth is not a re-roll.
#define EGGREG_X 133
#define EGGREG_Y 374
#define EGGREG_W 200
#define EGGREG_H 34
// The hit area is BIGGER than the pill, like the BOX button and the battle
// grid, and for the same reason: a 34 px target is under UI_TAP_MIN and a
// finger is not a stylus.
//
// The guard band matters more than the padding. Missing this pill fell through
// to pet.eggTap(), and THREE taps hatch the egg -- so fumbling at the version
// selector hatched the very egg you were trying to re-aim. A near miss now
// does nothing at all, which is the correct answer for a control whose
// neighbour is irreversible.
#define EGGREG_PAD 16
#define EGGREG_GUARD 14

static void drawEggRegion() {
  char l[24];
  snprintf(l, sizeof(l), "%s >", L(pet.regionName()));
  gfx->fillRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_WHITE);
  gfx->drawRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(EGGREG_X + (EGGREG_W - (int)tcols(l) * 12) / 2, EGGREG_Y + 9);
  gfx->print(l);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(T(S_EGG_REGION)) * 3, EGGREG_Y + EGGREG_H + 6);
  gfx->print(T(S_EGG_REGION));
}

// True if the tap was on the version pill, so the egg does not also get cracked.
// The egg's version pill: its graphic, and the area that actually accepts a tap.
// Exposed so a test can prove the second is bigger than the first and that a
// near miss does not reach pet.eggTap().
void uiEggPillRect(int *x, int *y, int *w, int *h, bool hitArea) {
  int pad = hitArea ? EGGREG_PAD : 0;
  if (x) *x = EGGREG_X - pad;
  if (y) *y = EGGREG_Y - pad;
  if (w) *w = EGGREG_W + 2 * pad;
  if (h) *h = EGGREG_H + 2 * pad;
}

// 1 = cycled the version, -1 = a near miss that must NOT reach the egg, 0 = not
// ours at all.
static int eggRegionTap(int16_t x, int16_t y) {
  if (!pet.isEgg()) return 0;
  int inset = EGGREG_PAD, guard = EGGREG_PAD + EGGREG_GUARD;
  bool hit = x >= EGGREG_X - inset && x <= EGGREG_X + EGGREG_W + inset &&
             y >= EGGREG_Y - inset && y <= EGGREG_Y + EGGREG_H + inset;
  if (hit) {
    pet.setRegion(nextHatchableRegion(pet.region));
    sfxPlay(SFX_TAP);
    return 1;
  }
  bool near = x >= EGGREG_X - guard && x <= EGGREG_X + EGGREG_W + guard &&
              y >= EGGREG_Y - guard && y <= EGGREG_Y + EGGREG_H + guard;
  if (near) { sfxPlay(SFX_DENY); return -1; }
  return 0;
}

// The version chooser used by the Digidex and the boss ladder. Each row carries
// its own progress, so the screen answers "where am I up to" as well as "where
// do I want to go".
#define RPICK_X 74
#define RPICK_W 318
#define RPICK_H 62
#define RPICK_Y(i) (108 + (i) * 72)
// The page dots sit BETWEEN the last row and the LAN button, not over it.
// They used to be at y=366, which is inside the LAN button (336..380) and on
// top of its label -- invisible until a version chooser had more than one page,
// so it appeared the moment Ver.4 made GYM_REGIONS 4 and went unnoticed
// because swipe_test drives the paging and never looks at the pixels.
#define RPICK_DOTS_Y 325
#define RPICK_DOT_R 5
// Guards, not decoration: both of these are the collision that shipped, and a
// static_assert is the only check that cannot be forgotten when somebody moves
// a button. The dots must clear the LAN button below and the last row above.
static_assert(RPICK_DOTS_Y + RPICK_DOT_R < LANBTN_Y,
              "the region-chooser page dots overlap the LAN button");
static_assert(RPICK_DOTS_Y - RPICK_DOT_R > RPICK_Y(RPICK_PER_PAGE - 1) + RPICK_H,
              "the region-chooser page dots overlap the last region row");

// How many versions this mode lists. Bosses genuinely only exist for three
// (GYM_REGIONS); the Digidex and the starter screen list every REAL version,
// which is GAL_REGIONS -- REGION_COUNT minus the ALL pseudo-version.
//
// This used to be GYM_REGIONS for ALL THREE MODES, with the names read out of
// TRAINER_SETS -- a tamer table driving the Digidex chooser. So the moment
// Ver.4 landed it had no row, while the gallery's vertical swipe cycled it
// happily: built, reachable, and looking absent. That is the exact failure the
// chooser was added to prevent.
uint8_t rpickRegions(uint8_t mode) {
  return (mode == RPICK_FOR_GYMS) ? (uint8_t)GYM_REGIONS : (uint8_t)GAL_REGIONS;
}

uint8_t rpickPageCount(uint8_t mode) {
  uint8_t n = rpickRegions(mode);
  uint8_t p = (uint8_t)((n + RPICK_PER_PAGE - 1) / RPICK_PER_PAGE);
  return p ? p : 1;
}

// The chooser WRAPS rather than closing off the end, unlike the other paged
// screens. It is the root of its own screen, and at first boot there is
// nowhere to go back to at all -- exiting would strand the player before they
// have chosen anything.
// Which chooser is on the panel, or 0xFF for none. THE single answer: the
// swipe handler and swipe_test both ask this rather than each deciding.
uint8_t rpickModeNow() {
  switch (uiCurrentScreen()) {
    case SCR_REGION:  return RPICK_FOR_START;
    case SCR_DEXPICK: return RPICK_FOR_DEX;
    case SCR_GYMPICK: return RPICK_FOR_GYMS;
    default: return 0xFF;
  }
}

static bool rpickSwipe(int dir) {
  uint8_t mode = rpickModeNow();
  if (mode == 0xFF) return false;
  uint8_t pages = rpickPageCount(mode);
  int p = (int)rpickPage + (dir > 0 ? -1 : 1);
  if (p < 0) p = pages - 1;
  if (p >= pages) p = 0;
  rpickPage = (uint8_t)p;
  sfxPlay(SFX_TAP);
  return true;
}

static void renderRegionPick(uint8_t mode) {
  bool forGyms = (mode == RPICK_FOR_GYMS);
  uint8_t nreg = rpickRegions(mode);
  uint8_t pages = rpickPageCount(mode);
  if (rpickPage >= pages) rpickPage = 0;
  uint8_t first = (uint8_t)(rpickPage * RPICK_PER_PAGE);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char ttl[40];
  if (mode == RPICK_FOR_START) snprintf(ttl, sizeof(ttl), "%s", T(S_CHOOSE_REGION));
  else if (forGyms) snprintf(ttl, sizeof(ttl), "%s", T(S_GYMS));
  else snprintf(ttl, sizeof(ttl), T(S_DIGIDEX_FMT), pet.registeredCount(), DEX_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(ttl) * 6, 48);
  gfx->print(ttl);

  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(first + row);
    if (i >= nreg) break;
    int y = RPICK_Y(row);
    // The sprite pack is the gate. A version without it is drawn GREYED with a
    // reason rather than dropped from the list: hiding it would say "this
    // version does not exist" when what we mean is "download its pack".
    //
    // AND THAT NOW INCLUDES THE GYMS. This read `forGyms || regionAvailable(i)`,
    // so a ladder was always enterable whether its pack was on the card or not --
    // the rule was enforced for the Digidex and the egg pool and not for its twin,
    // which is the shape CLAUDE.md keeps warning about. The cost was a boss
    // opening with a bare dex NUMBER where a creature should be, which tamers.h
    // calls out as the one thing that must not happen. Ver.4 could already do it;
    // Ver.4 and Ver.5 would have made it ordinary.
    //
    // It fails in the safe direction: gRegionArt defaults to ALL SET, so a board
    // with no card at all keeps every ladder exactly as before.
    // A Digi-Egg also needs its Baby I to have art (Ver.3 lacks Poyomon).
    bool open = (mode == RPICK_FOR_START) ? regionHatchable(i) : regionAvailable(i);
    gfx->fillRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_WHITE : UI_BG_DAY);
    gfx->drawRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_INK : UI_TRACK);
    const char *nm = L(forGyms ? TRAINER_SETS[i].region : REGIONS[i].name);
    gfx->setTextColor(open ? UI_INK : UI_TRACK);
    gfx->setTextSize(3);
    gfx->setCursor(RPICK_X + 18, y + 12);
    gfx->print(nm);
    // At first boot there is no subtitle: naming the starter here would give
    // away the next screen, and the counts the other two modes show would all
    // read zero on a new save anyway.
    char sub[28];
    sub[0] = 0;
    if (!open)
      snprintf(sub, sizeof(sub), "%s", T(S_NEED_PACK));
    else if (mode == RPICK_FOR_GYMS)
      snprintf(sub, sizeof(sub), T(S_BADGES_FMT), pet.badgeCountIn(i, gymHard));
    else if (mode == RPICK_FOR_DEX)
      snprintf(sub, sizeof(sub), "%u/%u",
               pet.registeredCountIn(REGIONS[i].lo, REGIONS[i].hi),
               (unsigned)(REGIONS[i].hi - REGIONS[i].lo + 1));
    if (sub[0]) {
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(2);
      gfx->setCursor(RPICK_X + RPICK_W - 18 - (int)tcols(sub) * 12, y + 22);
      gfx->print(sub);
    }
  }
  if (forGyms) {
    gfx->fillRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_BG_DAY);
    gfx->drawRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(T(S_LAN)) * 6, LANBTN_Y + 14);
    gfx->print(T(S_LAN));
  }
  if (pages > 1) {                        // dots: which page of versions this is
    int total = pages * 16 - 8;
    for (uint8_t d = 0; d < pages; d++) {
      int cx = CX - total / 2 + d * 16;
      if (d == rpickPage) gfx->fillCircle(cx, RPICK_DOTS_Y, RPICK_DOT_R, UI_INK);
      else gfx->drawCircle(cx, RPICK_DOTS_Y, RPICK_DOT_R, UI_TRACK);
    }
  }
  if (mode != RPICK_FOR_START) {          // first boot has nowhere to go back to
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 392);
    gfx->print(T(S_BACK));
  }
  uiChrome();
  gfx->flush();
}

// Returns the version tapped, or -1. Takes the mode because the row on screen is
// an offset into the current page, not the version index -- and because a version
// whose sprite pack is missing must not be selectable, which is the whole point
// of the gate. The chooser still SHOWS it, greyed, saying why.
// Where a version-chooser row is, so a test can tap one without restating the
// geometry. Same reason uiMenuRowCenterY() and uiTrainRowTop() exist.
int uiRegionRowCenterY(int row) { return RPICK_Y(row) + RPICK_H / 2; }
int uiRegionRowX() { return RPICK_X + RPICK_W / 2; }
int uiRegionRowsPerPage() { return RPICK_PER_PAGE; }

int regionPickTap(int16_t x, int16_t y, uint8_t mode) {
  if (x < RPICK_X || x > RPICK_X + RPICK_W) return -1;
  uint8_t nreg = rpickRegions(mode);
  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(rpickPage * RPICK_PER_PAGE + row);
    if (i >= nreg) break;
    if (y >= RPICK_Y(row) && y <= RPICK_Y(row) + RPICK_H) {
      // Every mode, boss ladders included -- the draw path greys the row, so the tap path
      // has to refuse it or the two disagree and a greyed row still works.
      if (mode == RPICK_FOR_START ? !regionHatchable(i) : !regionAvailable(i)) {
        sfxPlay(SFX_DENY);      // locked: say no out loud rather than do nothing
        return -1;
      }
      return i;
    }
  }
  return -1;
}

// ---------- level-up learn prompt ----------
void renderLearn() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t mv = pet.learnOffer();
  char head[40];
  const char *nm = pet.nick[0] ? pet.nick : L(DEX_TBL[pet.speciesId].name);
  snprintf(head, sizeof(head), T(S_LEARN_Q), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(head) * 3, 48);
  gfx->print(head);
  gfx->setTextColor(DEX_TBL[pet.speciesId].accent);
  const char *move = moveName(mv);
  int moveSize = tcols(move) <= 17 ? 3 : 2;
  gfx->setTextSize(moveSize);
  gfx->setCursor(CX - (int)tcols(move) * moveSize * 3, moveSize == 3 ? 66 : 72);
  gfx->print(move);

  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(LEARN_ROW_Y(i), pet.moves[i], false, pet.speciesId);

  gfx->fillRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_TRACK);
  gfx->drawRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_LEARN_SKIP)) * 6, LEARN_SKIP_Y + 14);
  gfx->print(T(S_LEARN_SKIP));
  gfx->flush();
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[20];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(head) * 9, 48);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(x + 16, y + 13);
      gfx->print("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(x + 44, y + 14);
    gfx->print(medalDesc(i));
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(T(S_PROGRESS)) * 9, 44);
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  gfx->setTextSize(5);
  gfx->setCursor(CX - tcols(lv) * 15, 86);
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[26];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(nx) * 6, by + 32);
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setCursor(CX - tcols(T(S_EVO_LABEL)) * 6, 230);
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[28];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    // The SAME sum canEvolveNow() uses -- including the day owed for retiring
    // the previous creature early. A card that left evoPenalty() out would
    // promise an digivolution that then does not happen.
    int needed = d.evolveLevel + pet.evoPenalty();
    if (pet.level() >= needed) {
      // The clock is done. Either a branch is open, or the one thing missing
      // is battles won (Champion -> Ultimate), which is worth saying in numbers.
      if (pet.digiTarget()) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else {
        snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_BLOCKED), pet.wins, pet.winsNeeded());
        evo = evoBuf;
        evoCol = UI_BAR_WARN;
      }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(CX - tcols(evo) * 6, 256);
  gfx->print(evo);

  // the day inherited from an early retire, said out loud -- otherwise this
  // creature simply digivolves late and the player has no way to know why
  if (pet.evoPenalty()) {
    gfx->setTextSize(1);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(CX - (int)tcols(T(S_EVO_SLOW)) * 3, 286);
    gfx->print(T(S_EVO_SLOW));
    gfx->setTextSize(2);
  }

  // care mistakes this stage: they pick the branch, so they are worth seeing
  char ms[28];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  gfx->setCursor(CX - tcols(ms) * 6, 312);
  gfx->print(ms);
  // ...and the training total, the other half of what picks the branch
  char tt[28];
  snprintf(tt, sizeof(tt), T(S_TRAIN_TOTAL_FMT), pet.trainingTotal());
  gfx->setTextColor(UI_INK);
  gfx->setCursor(CX - tcols(tt) * 6, 338);
  gfx->print(tt);
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMoves();
  else renderCardProgress();

  // indicador de paginas + ayuda
  for (int i = 0; i < CARD_PAGES; i++) {
    int dx = CX - (CARD_PAGES - 1) * 13 + i * 26;
    if (i == cardPage) gfx->fillCircle(dx, 374, 5, UI_INK);
    else gfx->drawCircle(dx, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 398);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

// ---------- menu overlay ----------

// Row labels are built fresh each frame because two of them carry live counts.
static void menuRowLabel(int i, char *out, size_t n) {
  switch (i) {
    case 0: snprintf(out, n, "%s", T(S_STATS)); break;
    case 1: snprintf(out, n, "%s", T(S_BAG)); break;
    case 2: snprintf(out, n, T(S_DIGIDEX_FMT), pet.registeredCount(), DEX_COUNT); break;
    case 3: snprintf(out, n, "%s", T(S_SETTINGS)); break;
    case 4: snprintf(out, n, "%s", T(S_RETIRE)); break;
    default: snprintf(out, n, "%s", T(S_CLOSE)); break;
  }
}

// Where a menu row is, asked of the firmware rather than restated.
//
// touch_test held the literal `104 + 16 + 22` next to a comment claiming it was
// MENU_ROW_Y(0) + 22 -- which it never was. It landed one pixel inside row 0
// and passed for that reason alone, until the panel gained a sixth row and the
// same tap started opening the BAG. A test that keeps its own copy of a layout
// proves the transcription; see CLAUDE.md on rpickPageCount() and
// uiButtonHeights(), which exist for exactly this.
int uiMenuRowCenterY(int i) { return MENU_ROW_Y(i) + MENU_ROW_H / 2; }

void drawMenu() {
  // dim the game behind the panel so the overlay reads as modal, and so it is
  // obvious that tapping the darkened area is a way out
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_WHITE);
  gfx->drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_INK);

  for (int i = 0; i < MENU_ROWS; i++) {
    int y = MENU_ROW_Y(i);
    bool close = (i == MENU_ROWS - 1);
    bool dead = (i == 4 && !pet.canRetireNow());   // an egg or a companion
    gfx->fillRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12,
                       close || dead ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(MENU_X + 18, y, MENU_W - 36, MENU_ROW_H, 12, UI_INK);
    char lbl[28];
    menuRowLabel(i, lbl, sizeof(lbl));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(lbl) * 6, y + MENU_ROW_H / 2 - 8);
    gfx->print(lbl);
  }
}

// ---------- who you are raising ----------

// Exchange the creature on the main screen with a stored one.
//
// A TRUE exchange: the live pet takes the slot the newcomer vacates, so this
// needs no free slot and nothing can be lost. Both records carry their full
// care state, so swapping away and back returns the creature exactly as it was
// rather than handing back a blanked copy -- which is the whole reason
// PartyMon grew a care block.
//
// An EGG is the one asymmetric case: it has nothing to bank, so the slot simply
// empties. That is what the old frozen BRING BACK did, and it is the only way
// this can cost you anything.
// JOURNALLED, because the exchange spans two checkpoint records and cannot be one
// commit. Party::focusBegin() carries the essay; the shape here is:
//
//   1. write the intent, holding BOTH creatures        (one small record)
//   2. put the outgoing creature in the slot           (the pair record)
//   3. make the incoming creature the live one         (the pet record)
//   4. retract the intent
//
// A cut anywhere in 2..3 is repaired at boot by focusRecover(), which forces the
// end state rather than guessing which half landed. A cut before 2 leaves the
// game exactly as it was and the journal is replayed onto an unchanged state,
// which lands in the same place.
void focusSwap(uint8_t slot) {
  if (slot >= PARTY_SLOTS) return;
  PartyMon incoming = party.slots[slot];   // by value: the slot is about to change
  if (incoming.empty()) return;
  const bool wasEgg = pet.isEgg();
  const PartyMon outgoing = wasEgg ? PartyMon() : pet.toPartyMon();
  // If the intent cannot be written, do the swap anyway rather than refuse it: an
  // unjournalled swap is what this firmware has always done, and blocking the
  // button on a failing NVS would be a worse outcome than the old risk.
  if (!party.focusBegin(slot, outgoing, wasEgg, incoming, pet.petGeneration()))
    Serial.println("save: focus journal failed; swapping unprotected");
  if (wasEgg) party.releaseAt(slot);
  else party.replaceAt(slot, outgoing);
  pet.switchTo(incoming);
  party.focusEnd();
}

// Finishes a swap that lost power part way through. Called once at boot, AFTER
// pet.begin() -- it needs the pet checkpoint's generation loaded to tell an
// unfinished swap from a stale journal.
void focusRecover() {
  uint8_t slot = 0;
  PartyMon outgoing, incoming;
  bool wasEgg = false;
  uint32_t armedGen = 0;
  if (!party.focusPending(&slot, &outgoing, &wasEgg, &incoming, &armedGen)) return;
  if (slot >= PARTY_SLOTS || incoming.empty()) { party.focusEnd(); return; }

  // The pet record's generation is the identity PartyMon lacks. switchTo() saves
  // once, so armedGen + 1 means the pet half committed; anything above that means
  // the game saved again afterwards and this journal is merely stale. Replaying
  // either would undo real play.
  if (pet.petGeneration() > armedGen) {
    party.focusEnd();
    return;
  }
  Serial.printf("save: finishing an interrupted focus swap into slot %u\n",
                (unsigned)slot);
  if (wasEgg) party.releaseAt(slot);
  else party.replaceAt(slot, outgoing);
  pet.switchTo(incoming);
  party.focusEnd();
}

// ---------- the bag ----------

// How many pages the field bag needs. One even when empty, so the screen can
// say so instead of drawing nothing and reading as a crash.
uint8_t bagPages() {
  uint8_t n = bag.distinctCount();
  return n ? (uint8_t)((n + BAG_PER_PAGE - 1) / BAG_PER_PAGE) : 1;
}

int uiBagRowCenterY(int i) { return BAG_ROW_Y(i) + 25; }

// A toast over the bag: what the last item did. Not a dialog -- using a vitamin
// is not a decision, and a confirmation for every tap would make training worse
// than the punching bag it competes with.
static char bagMsg[40] = "";
static uint32_t bagMsgUntil = 0;

static void bagSay(const char *fmt, const char *arg) {
  snprintf(bagMsg, sizeof(bagMsg), fmt, arg);
  bagMsgUntil = millis() + 2200;
}

// Applies a field item to the live pet. Returns false when the item was valid
// but had nothing to do -- a fully trained stat, say -- which the caller reports
// rather than silently consuming the item.
static bool bagUseOnPet(ItemKey k) {
  const ItemEntry &e = itemEntry(k);
  if (e.category != IC_TRAIN) return false;
  // The IV-bound ceiling applies here exactly as it does to the punching bag and
  // to a boss reward. An item that could push past trMaxFor() would make the IV
  // roll decorative, which is the one thing it must not be.
  uint8_t *cur;
  uint8_t cap;
  switch ((uint8_t)e.param) {
    case TRAIN_ATK: cur = &pet.trAtk; cap = pet.trMaxAtk(); break;
    case TRAIN_DEF: cur = &pet.trDef; cap = pet.trMaxDef(); break;
    default:        cur = &pet.trSpe; cap = pet.trMaxSpe(); break;
  }
  if (*cur >= cap) return false;
  uint16_t next = (uint16_t)*cur + e.amount;
  *cur = next > cap ? cap : (uint8_t)next;
  pet.saveNow();
  return true;
}

void renderBag() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_BAG)) * 6, 40);
  gfx->print(T(S_BAG));

  uint8_t n = bag.distinctCount();
  uint8_t pages = bagPages();
  if (bagPage >= pages) bagPage = 0;
  if (!n) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(CX - (int)tcols(T(S_BAG_EMPTY)) * 6, CY - 8);
    gfx->print(T(S_BAG_EMPTY));
  }
  for (uint8_t i = 0; i < BAG_PER_PAGE; i++) {
    uint8_t idx = (uint8_t)(bagPage * BAG_PER_PAGE + i);
    if (idx >= n) break;
    ItemKey k = bag.keyAt(idx);
    int y = BAG_ROW_Y(i);
    bool usable = itemUsableInField(k);
    gfx->fillRoundRect(76, y, 314, 50, 10, usable ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(76, y, 314, 50, 10, UI_INK);
    gfx->setTextColor(usable ? UI_INK : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(92, y + 17);
    gfx->print(L(itemEntry(k).name));
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "x%u", bag.count(k));
    gfx->setCursor(374 - (int)tcols(cnt) * 12, y + 17);
    gfx->print(cnt);
  }
  // A page NUMBER, not a dot row: the catalogue is append-only and a dot per
  // page stopped fitting the round panel once the Digidex reached 25 of them.
  if (pages > 1) {
    char pg[12];
    snprintf(pg, sizeof(pg), "%u/%u", bagPage + 1, pages);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(pg) * 6, 386);
    gfx->print(pg);
  }
  if (bagMsgUntil && millis() < bagMsgUntil) {
    gfx->fillRoundRect(66, CY - 26, 334, 52, 12, UI_WHITE);
    gfx->drawRoundRect(66, CY - 26, 334, 52, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)tcols(bagMsg) * 6, CY - 8);
    gfx->print(bagMsg);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_BACK)) * 6, 414);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

void bagTap(int16_t x, int16_t y) {
  uint8_t n = bag.distinctCount();
  for (uint8_t i = 0; i < BAG_PER_PAGE; i++) {
    uint8_t idx = (uint8_t)(bagPage * BAG_PER_PAGE + i);
    if (idx >= n) break;
    int ry = BAG_ROW_Y(i);
    if (x < 76 || x > 390 || y < ry || y > ry + 50) continue;
    ItemKey k = bag.keyAt(idx);
    if (!itemUsableInField(k) || pet.isEgg()) { sfxPlay(SFX_DENY); return; }
    if (!bagUseOnPet(k)) { sfxPlay(SFX_DENY); bagSay("%s", T(S_ITEM_NOUSE)); return; }
    bag.consume(k);
    sfxPlay(SFX_TAP);
    bagSay(T(S_ITEM_USED), L(itemEntry(k).name));
    return;
  }
  bagOpen = false;   // anywhere else closes, like every other list screen
}

// ---------- wild encounters ----------

// Rolls a whole wild individual and opens the fight.
//
// The Digimon, the level band, the tier and the variant roll are all decided
// HERE and remembered, because a capture has to hand back the creature that was
// actually fought. Re-rolling any of it at capture time would mean the thing
// you caught was not the thing on screen.
bool startWildBattle(uint8_t region, bool hard) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return false;
  if (region >= REGION_COUNT || !regionAvailable(region)) return false;
  // The level first: it decides which STAGE can turn up. A level-10 encounter
  // is a Rookie, never a Champion that has not had time to exist.
  uint8_t lo = wildLevelMin(pet.level(), hard);
  uint8_t hi = wildLevelMax(pet.level(), hard);
  uint8_t lvl = (uint8_t)(lo + random(hi - lo + 1));
  uint8_t stage = wildStageFor(lvl, (uint8_t)random(100));
  int16_t dex = wildPickSpecies(region, stage, (uint32_t)random(65536) * 31u + random(31));
  // A stage can be empty in a version (Ver.1 has no Mega): step down rather
  // than refuse, since "nothing happened" reads as a broken button.
  for (int st = (int)stage - 1; !dex && st >= ST_BABY2; st--)
    dex = wildPickSpecies(region, (uint8_t)st, random(65536));
  if (!dex) return false;
  wildDex = dex;
  wildLvl = lvl;
  for (int i = 0; i < 4; i++) wildIv[i] = (uint8_t)(8 + random(24));
  // Bonus 0: nothing in this build earns one yet, so every wild variant is the
  // flat 1/4096. The parameter stays because it is where a farewell reward
  // would land, and wild_test already pins the curve it would follow.
  wildVariant = wildVariantForRoll((uint32_t)random(WILD_VARIANT_SCALE), 0);
  wildApplyVariant(wildVariant, wildIv[0], wildIv[1], wildIv[2], wildIv[3]);
  wildCaught = false;
  wildDropN = 0;

  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return false;

  // Built through Pet like every other opponent, so it uses the same stat
  // formula and the same learnset-driven moveset the player's creature does.
  Pet foe;
  foe.dbgHatchAs(dex, wildVariant);
  foe.ivAtk = wildIv[0]; foe.ivDef = wildIv[1];
  foe.ivSpe = wildIv[2]; foe.ivHp = wildIv[3];
  foe.ageMinutes = (uint32_t)(wildLvl ? wildLvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlFoe.variant = wildVariant;

  btlWild = true;
  btlLink = false;
  btlTrainer = -1;
  btlHard = hard;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlBagPage = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
  btlSay(T(S_WILD_MET), L(DEX_TBL[dex].name));
  return true;
}

// The wild creature as a storable record. It goes through the same PartyMon the
// party and box already hold, so a caught creature is a first-class member the
// moment it lands: it can be swapped in, raised, digivolved and battled with.
PartyMon wildToPartyMon() {
  PartyMon m;
  m.dex = wildDex;
  m.level = wildLvl;
  m.ivAtk = wildIv[0]; m.ivDef = wildIv[1];
  m.ivSpe = wildIv[2]; m.ivHp = wildIv[3];
  m.variant = wildVariant ? 1 : 0;
  for (int i = 0; i < MOVE_SLOTS; i++) m.moves[i] = btlFoe.moves[i];
  // A caught creature has real care state from the start rather than being a
  // v0 record, so switching to it does not reset a creature that was never
  // banked in the first place. Its age IS its level: nothing raised it.
  m.stateVersion = 1;
  m.ageMinutes = (uint32_t)(wildLvl ? wildLvl - 1 : 0) * MINUTES_PER_LEVEL;
  m.lastLearnLevel = wildLvl;   // do not replay gates it already passed
  return m;
}

// ---------- training submenu (5th icon) ----------

// Bars here show progress toward the IV-capped ceiling, not a raw stat: 100%
// means this individual cannot train the stat any higher, which is the whole
// point of trMaxFor() gating training by IV.
static uint8_t trainPct(uint8_t cur, uint8_t cap) {
  return cap ? (uint8_t)((uint16_t)cur * 100 / cap) : 0;
}

void renderTrain() {
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_WHITE);
  gfx->drawRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_INK);

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(T(S_TRAIN)) * 6, TRAIN_Y + 20);
  gfx->print(T(S_TRAIN));

  const char *lbl[TRAIN_ROWS] = { T(S_TR_ATK), T(S_TR_SPE), T(S_TR_DEF), T(S_MEMO) };
  uint8_t cur[TRAIN_ROWS] = { pet.trAtk, pet.trSpe, pet.trDef, 0 };
  uint8_t cap[TRAIN_ROWS] = { pet.trMaxAtk(), pet.trMaxSpe(), pet.trMaxDef(), 0 };

  for (int i = 0; i < TRAIN_ROWS; i++) {
    int y = TRAIN_ROW_Y(i);
    gfx->fillRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_BG_DAY);
    gfx->drawRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(TRAIN_X + 32, y + 8);
    gfx->print(lbl[i]);

    // The MEMORY row has no bar, because it trains no stat -- it pays in joy and
    // bond. Drawing a progress bar for it would promise a ceiling to fill, and
    // there is none; the record goes there instead, which is what it does have.
    if (i == TRAIN_ROW_MEMO) {
      char r[22];
      snprintf(r, sizeof(r), T(S_REC_FMT), pet.memoHi);
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(1);
      gfx->setCursor(TRAIN_X + 32, y + 30);
      gfx->print(r);
      continue;
    }
    uint8_t pct = trainPct(cur[i], cap[i]);
    int bx = TRAIN_X + 32, bw = TRAIN_W - 64, bh = 10, by = y + 30;
    gfx->fillRoundRect(bx, by, bw, bh, 4, UI_TRACK);
    int fw = (bw - 4) * pct / 100;
    if (fw > 0)
      gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, pct >= 100 ? UI_BAR_OK : UI_BAR_WARN);
  }

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(T(S_TR_DEF_HINT)) * 3, TRAIN_Y + TRAIN_H - 22);
  gfx->print(T(S_TR_DEF_HINT));
  gfx->flush();   // without this the panel never updates and the screen freezes
}

// ---------- the box ----------
// Storage past the six that fight. A creature is moved by picking a party slot
// and then a box slot, which swaps them -- so one gesture covers deposit,
// withdraw and exchange rather than needing three.

// Should box slot `a` be drawn AFTER slot `b` under the current order?
//
// Empty slots always last, whatever the key: a grid with holes punched through it
// is harder to read than a full one, and the empty cells are only there to be
// swap targets anyway.
static bool boxSortAfter(uint8_t a, uint8_t b) {
  const PartyMon &ma = party.box[a], &mb = party.box[b];
  if (ma.empty() != mb.empty()) return ma.empty();
  if (ma.empty()) return false;                  // both empty: keep slot order
  if (boxSort == BSORT_DEX) {
    if (ma.dex != mb.dex) return ma.dex > mb.dex;
  } else if (boxSort == BSORT_LEVEL) {
    if (ma.level != mb.level) return ma.level < mb.level;   // strongest first
  }
  return false;      // equal keys: leave them in slot order, so this is STABLE
}

// Which REAL box slot a visual position shows.
//
// A VIEW PERMUTATION, NEVER A DATA ONE, and that is not a style preference.
// party.box[] is half of a CRC-checked checkpoint written as one record with the
// party; reordering the array would rewrite the largest blob in the store every
// time somebody glanced at the box, and boxDetail/boxSel/boxSwapFrom all carry
// REAL indices into it, so a sort landing mid-gesture would retarget a pending
// swap onto a different creature. Sorting the LOOK costs one pass over BOX_SLOTS
// entries and cannot lose anything.
//
// box_test asserts this is a BIJECTION for every mode, because the one bug class
// that matters here is two visual cells resolving to the same real slot -- which
// is how a creature gets overwritten.
uint8_t boxSlotFor(uint8_t visual) {
  if (visual >= BOX_SLOTS) return 0;
  if (boxSort == BSORT_SLOT) return visual;
  uint8_t order[BOX_SLOTS];
  for (uint8_t i = 0; i < BOX_SLOTS; i++) order[i] = i;
  // Insertion sort: few entries, and STABLE, which is what makes equal keys
  // resolve to a deterministic order instead of one the algorithm chose.
  for (uint8_t i = 1; i < BOX_SLOTS; i++) {
    const uint8_t v = order[i];
    int j = (int)i - 1;
    while (j >= 0 && boxSortAfter(order[j], v)) { order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
  return order[visual];
}

const char *boxSortName() {
  switch (boxSort) {
    case BSORT_DEX:   return T(S_SORT_DEX);
    case BSORT_LEVEL: return T(S_SORT_LVL);
    default: return T(S_SORT_SLOT);
  }
}

void renderBox() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[32];
  snprintf(head, sizeof(head), T(S_BOX_FMT), party.boxCount(), BOX_SLOTS);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(head) * 6, 40);
  gfx->print(head);
  if (boxSwapFrom) {
    const PartyMon &p = party.slots[boxSwapFrom - 1];
    char sub[40];
    snprintf(sub, sizeof(sub), T(S_BOX_SWAP),
             p.empty() ? "-" : (p.nick[0] ? p.nick : L(DEX_TBL[p.dex].name)));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(sub) * 3, PSLOT_PROMPT_Y);
    gfx->print(sub);
  }
  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t visual = boxPage * BOX_PER_PAGE + i;
    if (visual >= BOX_SLOTS) break;
    drawPartySlot(i, party.box[boxSlotFor(visual)]);
  }
  {
    // The hub carries the page AND the order, and tapping it cycles the order --
    // see boxTap(). It is the easiest place to hit on a round panel, and it used
    // to answer a tap by closing the box, which is what BACK and a swipe already
    // do twice over.
    char pg[12];
    snprintf(pg, sizeof(pg), "%u/%u", (unsigned)(boxPage + 1),
             (unsigned)(BOX_SLOTS / BOX_PER_PAGE));
    drawPartyHub(pg, boxSortName(), boxSwapFrom != 0);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

void boxTap(int16_t x, int16_t y) {
  // The sheet is checked first and is modal in the same way the party's is.
  if (boxDetail) {
    if (releaseConfirm) { monSheetConfirmTap(x, y, true); return; }
    if (monSheetBtn(x, y, true)) {          // TO PARTY
      int free = party.firstFree();
      if (free < 0) {
        boxSel = boxDetail;
        boxDetail = 0;
        boxOpen = false;
        sfxPlay(SFX_TAP);
        return;
      }
      party.swapPartyBox((uint8_t)free, boxDetail - 1);
      boxDetail = 0;
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (monSheetBtn(x, y, false)) {         // RELEASE -- ask first, always
      releaseConfirm = true;
      sfxPlay(SFX_TAP);
      return;
    }
    boxDetail = 0;                          // anywhere else backs out
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  // The hub cycles the order. Checked BEFORE the slots: the hub sits in the
  // middle of the ring and does not overlap them, but ordering it first keeps the
  // rule "the thing drawn on top answers first" true here as everywhere else.
  if (partyHubAt(x, y)) {
    boxSort = (uint8_t)((boxSort + 1) % BSORT_COUNT);
    // A pending swap is armed against a REAL slot, so re-ordering the view does
    // not invalidate it -- boxSlotFor() is a permutation of the same slots. The
    // open sheet is closed anyway, because it was opened from a visual position
    // the player can no longer see.
    boxDetail = 0;
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint8_t visual = boxPage * BOX_PER_PAGE + i;
    if (visual >= BOX_SLOTS) break;
    uint8_t idx = boxSlotFor(visual);
    if (partySlotAt(x, y) != (int)i) continue;
    if (boxSwapFrom) {           // a party slot is waiting: complete the trade
      if (party.slots[boxSwapFrom - 1].empty() && party.box[idx].empty()) {
        sfxPlay(SFX_DENY);
        return;
      }
      party.swapPartyBox(boxSwapFrom - 1, idx);
      boxSwapFrom = 0;
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    // Otherwise open its SHEET. It used to go straight to the party, which is a
    // lot to happen from one tap and left nowhere to put RELEASE; the sheet
    // offers TO PARTY explicitly and shows what you are about to move.
    if (party.box[idx].empty()) { sfxPlay(SFX_DENY); return; }
    boxDetail = idx + 1;
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  boxOpen = false;               // anywhere else backs out
  boxSwapFrom = 0;
  boxDetail = 0;
  releaseConfirm = false;
}

// ---------- party ----------

void drawPartySlot(int i, const PartyMon &m) {
  int cx, cy;
  partySlotPos(i, &cx, &cy);
  gfx->fillCircle(cx, cy, PSLOT_R, m.empty() ? UI_TRACK : UI_WHITE);
  gfx->drawCircle(cx, cy, PSLOT_R, UI_INK);
  gfx->drawCircle(cx, cy, PSLOT_R - 1, UI_INK);
  if (m.empty()) {
    gfx->setTextColor(0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(cx - (int)tcols(T(S_PARTY_EMPTY)) * 6, cy - 8);
    gfx->print(T(S_PARTY_EMPTY));
    return;
  }
  const uint8_t *th = thumbs.get(m.dex);
  if (th) drawThumb(th, cx - 24, cy - 44, 1, false);
  const DexEntry &d = DEX_TBL[m.dex];
  const char *nm = m.nick[0] ? m.nick : L(d.name);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(1);
  gfx->setCursor(cx - (int)tcols(nm) * 3, cy + 8);
  gfx->print(nm);
  if (m.variant) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(cx + (int)tcols(nm) * 3 + 3, cy + 8);
    gfx->print("*");
  }
  char lv[12];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), (unsigned)m.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(cx - (int)tcols(lv) * 6, cy + 20);
  gfx->print(lv);
}

// The hub in the middle of the ring. On the party it is the BOX button; in the
// box it is the page. Either way the centre of a round panel is the easiest
// place on it to hit, so the primary action belongs there.
void drawPartyHub(const char *top, const char *bot, bool armed) {
  gfx->fillCircle(CX, PSLOT_CY, PSLOT_HUB_R, armed ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawCircle(CX, PSLOT_CY, PSLOT_HUB_R, UI_INK);
  gfx->drawCircle(CX, PSLOT_CY, PSLOT_HUB_R - 1, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(top) * 6, PSLOT_CY - 14);
  gfx->print(top);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)tcols(bot) * 3, PSLOT_CY + 10);
  gfx->print(bot);
}

void renderParty() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  char head[24];
  snprintf(head, sizeof(head), T(S_PARTY_FMT), party.count());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)tcols(head) * 9, 42);
  gfx->print(head);

  // the box lives behind the hub in the middle of the ring; it also shows how
  // full it is, so the player knows there is anything in there without opening
  if (!partyPick) {
    char bl[24];
    snprintf(bl, sizeof(bl), "%u/%u", party.boxCount(), BOX_SLOTS);
    drawPartyHub(bl, T(S_BOX_BTN), boxSwapFrom != 0);
  }

  if (boxSel) {
    const PartyMon &b = party.box[boxSel - 1];
    char sw[44];
    snprintf(sw, sizeof(sw), T(S_BOX_SWAP),
             b.nick[0] ? b.nick : L(DEX_TBL[b.dex].name));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(sw) * 3, PSLOT_PROMPT_Y);
    gfx->print(sw);
  }

  // when a newcomer is waiting, say so instead of the usual hint. The urgency
  // is carried by the red CLOSE button below, not by tinting the sentence:
  // UI_BAR_BAD as small text on cream is 3.2:1.
  if (partyPick) {
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(T(S_PARTY_FULL)) * 3, PSLOT_PROMPT_Y);
    gfx->print(T(S_PARTY_FULL));
  }

  for (int i = 0; i < PARTY_SLOTS; i++) drawPartySlot(i, party.slots[i]);

  // exit: an explicit button, always in the same place
  const char *ex = partyPick ? T(S_PARTY_LETGO) : T(S_CLOSE);
  gfx->fillRoundRect(PARTYCLOSE_X, PARTYCLOSE_Y, PARTYCLOSE_W, PARTYCLOSE_H, 12,
                     partyPick ? UI_BAR_BAD : UI_TRACK);
  gfx->drawRoundRect(PARTYCLOSE_X, PARTYCLOSE_Y, PARTYCLOSE_W, PARTYCLOSE_H, 12, UI_INK);
  gfx->setTextColor(partyPick ? UI_WHITE : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(ex) * 6, PARTYCLOSE_Y + 14);
  gfx->print(ex);
  uiChrome();
  gfx->flush();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

// The keyboard is shared, so it has to be told what it is naming. It used to
// hardcode pet.rename() on commit, which is why a second caller needed this.
void openKeyboardFor(uint8_t target) {
  kbTarget = target;
  kbOpen = true;
  const char *cur = (target == KB_TRAINER) ? pet.trainerName : pet.nick;
  strncpy(nameBuf, cur, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}
void openKeyboard() { openKeyboardFor(KB_PET); }

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(T(S_NAME)) * 6, 56);
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(95, 94);
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    if (i < 28) {
      gfx->setCursor(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      gfx->print(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      gfx->setCursor(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      gfx->print(lab);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col < 0 || col >= KB_COLS || row < 0 || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    if (kbTarget == KB_TRAINER) pet.renameTrainer(nameBuf);
    else pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria digidex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL 80

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      uint8_t idx = d[r * w + c];
      if (idx == 0xFF) continue;
      uint16_t col = sil ? INK_K : (uint16_t)(pal[idx * 2] | (pal[idx * 2 + 1] << 8));
      gfx->fillRect(ox + c * s, oy + r * s, s, s, col);
    }
  }
}

// ---------------------------------------------------------------------------
// The filter, as four small functions. THE draw path and THE tap path both go
// through galleryAt(), which is the whole point: with a filter on, the grid is no
// longer contiguous, so the cell arithmetic that used to be repeated in both
// places would put a tap on a different creature than the one drawn.

// Is this Digimon in the player's hands RIGHT NOW -- the live creature, a party
// slot, or a box slot?
static bool dexHeldNow(int16_t dex) {
  if (!pet.isEgg() && pet.speciesId == dex) return true;
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (party.slots[i].dex == dex) return true;
  for (int i = 0; i < BOX_SLOTS; i++)
    if (party.box[i].dex == dex) return true;
  return false;
}

bool galleryPass(int16_t dex) {
  switch (galleryFilter) {
    case GFILT_RAISED: return pet.isRegistered(dex);
    case GFILT_CAUGHT: return dexHeldNow(dex);
    case GFILT_VARIANT:  return pet.isVariantRegistered(dex);
    default: return true;
  }
}

uint16_t galleryCount() {
  uint16_t n = 0;
  for (int16_t d = GAL_LO; d <= GAL_HI && d <= DEX_COUNT; d++)
    if (galleryPass(d)) n++;
  return n;
}

// The dex number at a visual grid position, or 0 past the end.
int16_t galleryAt(uint16_t visual) {
  uint16_t n = 0;
  for (int16_t d = GAL_LO; d <= GAL_HI && d <= DEX_COUNT; d++) {
    if (!galleryPass(d)) continue;
    if (n == visual) return d;
    n++;
  }
  return 0;
}

// At least one page even when the filter matches nothing, so the screen can SAY
// so rather than drawing an empty grid that reads as a crash.
uint8_t galleryPages() {
  const uint16_t n = galleryCount();
  const uint8_t p = (uint8_t)((n + GAL_PER_PAGE - 1) / GAL_PER_PAGE);
  return p ? p : 1;
}

const char *galleryFilterName() {
  switch (galleryFilter) {
    case GFILT_RAISED: return T(S_FILT_RAISED);
    case GFILT_CAUGHT: return T(S_FILT_CAUGHT);
    case GFILT_VARIANT:  return T(S_FILT_VARIANT);
    default: return T(S_FILT_ALL);
  }
}

// Cycling the filter can leave the page past the end of a shorter result, and a
// page number beyond the count draws nothing at all. One place fixes it, called
// by everything that changes either the filter or the version.
void galleryClampPage() {
  const uint8_t n = galleryPages();
  if (galleryPage >= n) galleryPage = n - 1;
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    char head[24];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             pet.isVariantRegistered(galleryDetail) ? "*" : "", reg ? L(d.name) : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    int glen = tcols(head);
    int gts = (glen <= 13) ? 3 : 2;  // auto-encoge nombres largos (no caben a t3)
    gfx->setTextSize(gts);
    gfx->setCursor(CX - glen * (gts == 3 ? 9 : 6), gts == 3 ? 56 : 60);
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, reg ? millis() : 0, true, !reg, 6);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !reg);
    }
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(T(S_DETAIL_BACK)) * 6, 408);
    gfx->print(T(S_DETAIL_BACK));
    uiChrome();
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // the version's own name and its own tally: "how much of Ver.2 have I seen"
  // is the question you are actually asking here
  char head[32];
  const RegionInfo &grg = REGIONS[galleryRegion % GAL_REGIONS];
  snprintf(head, sizeof(head), "%s %u/%u", L(grg.name),
           pet.registeredCountIn(grg.lo, grg.hi), (unsigned)GAL_SPAN);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(head) * 9, 36);
  gfx->print(head);
  // The filter, under the tally and tappable -- it has to be VISIBLE, or a filter
  // left on reads as a Digidex that lost half its entries. Says what it is
  // showing and how many, since "ALL 151" and "VARIANT 3" answer different
  // questions and the count is the interesting half.
  {
    char fl[32];
    snprintf(fl, sizeof(fl), "%s %u", galleryFilterName(), (unsigned)galleryCount());
    gfx->setTextColor(galleryFilter == GFILT_ALL ? UI_INK_SOFT : UI_BAR_WARN);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)tcols(fl) * 3, 66);
    gfx->print(fl);
  }

  if (galleryCount() == 0) {
    // SAY SO. An empty grid is indistinguishable from a crash, and the filter
    // that caused it is one tap away.
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - tcols(T(S_FILT_NONE)) * 6, 220);
    gfx->print(T(S_FILT_NONE));
  }
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      const uint16_t visual = (uint16_t)galleryPage * GAL_PER_PAGE + r * 4 + c;
      const int16_t dex = galleryAt(visual);
      if (!dex) break;                 // past the end of the filtered list
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        drawThumb(t, x, y, 2, !pet.isRegistered(dex));
        if (pet.isVariantRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(2);
          gfx->setCursor(x + 62, y + 4);
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_INK_SOFT);
        gfx->setTextSize(2);
        gfx->setCursor(x + 24, y + 32);
        gfx->print(num);
      }
    }
  }
  // A page number, not a row of dots. 25 dots do not fit across the bottom of
  // a round panel -- the chord at that height is only ~228 px -- and counting
  // them to find where you are is worse than reading the number.
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", galleryPage + 1, (int)galleryPages());
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(pg) * 6, 410);
  gfx->print(pg);
  uiChrome();
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  // The filter strip, BEFORE the exit band. It sits at y 66 inside what used to
  // be "tap the header to leave", so the two are split by height: the title band
  // still exits, the strip under it cycles.
  if (y >= 58 && y < 80) {
    galleryFilter = (uint8_t)((galleryFilter + 1) % GFILT_COUNT);
    galleryClampPage();
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (y < 58) {  // tocar la cabecera = salir
    galleryOpen = false;
    galleryPmd.unload();
    return;
  }
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c < 0 || c > 3 || r < 0 || r > 3) return;
  // The SAME function the grid was drawn from, so a tap cannot land on a
  // different creature than the one under the finger.
  const int16_t dex = galleryAt((uint16_t)galleryPage * GAL_PER_PAGE + r * 4 + c);
  if (!dex) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isVariantRegistered(dex));
  // Only for an entry the player has actually filled in. Playing the cry of a
  // silhouette would hand over the one thing the "???" is withholding.
  if (pet.isRegistered(dex)) audioCry(dex);
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - 14, y = 12, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

// Drawn beside the battery when Pet::saveHealthy() goes false (NVS would not
// open, or it has refused three writes in a row) OR when gNvsLow is set (the
// partition itself is close to full).
//
// The low-space case matters just as much: a full partition is erased WHOLE by
// the Arduino core on the NEXT BOOT (see nvsinfo.cpp), so a save can still be
// succeeding right up until a restart wipes every badge and every save on the
// device. `nvsLowOnSpace()` existed only in the serial HEALTH log before this,
// which is exactly how a player found out: by getting wiped, with no earlier
// sign on the one screen they were actually looking at.
//
// Deliberately not a dialog. The condition persists, so a modal would be
// dismissed once and then forgotten, and it is not actionable in the moment --
// what the player needs is to know before they put in another week, so they can
// EXPORT. The firmware could not say this at all before: the only symptom was a
// line on a serial port no player has attached.
void drawSaveWarning() {
  if (pet.saveHealthy() && !gNvsLow) return;
  const int x = CX - 40, y = 11, w = 13, h = 13;
  gfx->fillTriangle(x + w / 2, y, x, y + h, x + w, y + h, UI_BAR_BAD);
  gfx->fillRect(x + w / 2, y + 5, 2, 4, RGB565_BLACK);   // the "!"
  gfx->fillRect(x + w / 2, y + 10, 2, 2, RGB565_BLACK);
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  drawSaveWarning();
  gfx->setTextColor(nameColor);
  gfx->setTextSize(3);
  gfx->setCursor(CX - tcols(name) * 9, HEADER_NAME_Y);
  gfx->print(name);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(CX - tcols(msg) * 6, 90);
  gfx->print(msg);
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// dialogo de decision (2 botones apilados): evolucionar/mantener o despedirse/quedaros
// The two confirm buttons' hit areas, so a test can hold them to UI_TAP_MIN and
// prove they do not overlap without restating the numbers.
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot) {
  *b1Top = CONFIRM_B1_Y;
  *b1Bot = CONFIRM_B1_Y + CONFIRM_BTN_H;
  *b2Top = CONFIRM_B2_Y;
  *b2Bot = CONFIRM_B2_Y + CONFIRM_BTN_H;
}

// Draws the panel and its two buttons. sub1/sub2 are optional lines between the
// question and the buttons -- what the choice COSTS, which for anything
// irreversible has to be on screen before the tap, not after it.
void drawConfirmPanel(const char *q, const char *sub1, const char *sub2,
                      uint16_t subCol, const char *o1, uint16_t c1, uint16_t t1,
                      const char *o2, uint16_t c2, uint16_t t2) {
  gfx->fillRoundRect(CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H, 16, UI_WHITE);
  gfx->drawRoundRect(CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(q) * 6, 176);
  gfx->print(q);
  if (sub1 || sub2) {
    gfx->setTextSize(1);
    gfx->setTextColor(subCol);
    if (sub1) {
      gfx->setCursor(CX - (int)tcols(sub1) * 3, sub2 ? 188 : 194);
      gfx->print(sub1);
    }
    if (sub2) {
      gfx->setCursor(CX - (int)tcols(sub2) * 3, 197);
      gfx->print(sub2);
    }
    gfx->setTextSize(2);
    gfx->setTextColor(UI_INK);
  }
  gfx->fillRoundRect(CONFIRM_BTN_X, CONFIRM_B1_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 12, c1);
  gfx->setTextColor(t1);
  gfx->setCursor(CX - (int)tcols(o1) * 6, CONFIRM_B1_Y + 18);
  gfx->print(o1);
  gfx->fillRoundRect(CONFIRM_BTN_X, CONFIRM_B2_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 12, c2);
  gfx->setTextColor(t2);
  gfx->setCursor(CX - (int)tcols(o2) * 6, CONFIRM_B2_Y + 18);
  gfx->print(o2);
}

void drawChoiceDialog() {
  const char *q, *o1, *o2;
  const char *sub1 = nullptr, *sub2 = nullptr;
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else if (choiceKind == 3) {   // retirada a peticion
    q = T(S_RETIRE_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
    // The price, spelled out, and only when there is one: retiring a creature
    // that has already earned its farewell costs nothing and must not claim to.
    // An EARLY one now costs TWO things and says both -- the creature is not
    // banked at all, which is the half a player would not otherwise discover
    // until the party screen came up empty.
    if (!pet.retireIsFree()) { sub1 = T(S_RETIRE_COST); sub2 = T(S_RETIRE_GONE); }
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  drawConfirmPanel(q, sub1, sub2, UI_BAR_BAD, o1, c1, t1, o2, c2, t2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  const char *t = T(S_EVO_TAP);
  gfx->setCursor(CX - (int)tcols(t) * 9, y + h / 2 - 11);
  gfx->print(t);
}

// boton-CTA dorado de despedida: "<nombre> quiere decirte algo..."
void drawFarewellButton() {
  uint32_t now = millis();
  int p = (int)(4 * sinf(now * 0.005f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, UI_BAR_WARN);
  gfx->drawRoundRect(x, y, w, h, 16, UI_INK);
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : L(DEX_TBL[pet.speciesId].name);
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(3 * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, 16, C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : L(DEX_TBL[pet.speciesId].name);
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)tcols(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  // No sprite on the card: say so plainly. There is no built-in art -- every
  // Digimon comes from the SD.
  gfx->setTextColor(inkColor());
  gfx->setTextSize(6);
  gfx->setCursor(CX - 18, PET_CY - 80);
  gfx->print("?");
  gfx->setTextSize(2);
  const char *l1 = T(S_NO_SPRITES);
  gfx->setCursor(CX - (int)tcols(l1) * 6, PET_CY - 4);
  gfx->print(l1);
  const char *l2 = T(S_LOAD_SPRITES);
  gfx->setCursor(CX - (int)tcols(l2) * 6, PET_CY + 20);
  gfx->print(l2);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (now > bathUntil) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // pose de alegria al quedar limpio
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  while (t >= a.ms[i]) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdCore(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS, bool flip) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  uint8_t sBase = m.acts[PMD_IDLE].h ? 170 / m.acts[PMD_IDLE].h : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    for (int c = 0; c < a.w; c++) {
      uint8_t idx = row[flip ? a.w - 1 - c : c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x0 + c * s, y0 + r * s, s, s, sil ? INK_K : m.pal[idx]);
    }
  }
}
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdCore(m, actId, cx, groundY, t, loop, sil, maxS, false);
}
// V-Pet sprites face LEFT. In a fight your own Digimon stands on the left, so
// it is mirrored to face the foe.
void drawPmdActFlip(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdCore(m, actId, cx, groundY, t, loop, sil, maxS, true);
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS);
}

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = 150 + random(176);
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else {
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        behNext();
        act = PMD_IDLE;
      } else {
        beh.x += (d > 0 ? 3.0f : -3.0f);
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 5);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
}

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
void drawPetSD() {
  int s = mon.scale;
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= 6;  // saltito
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    for (int c = 0; c < mon.w; c++) {
      uint8_t idx = row[c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, sil ? INK_K : mon.pal[idx]);
    }
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - 30, y - 50, 2, false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

// The four care stats, on the panel's RIM.
//
// They were 2x2 bars across y 318..361 -- four 100 px tracks laid over the
// widest part of the circle, which is the one place a round panel has room to
// spare, while the home row below them had to make do with 60 px icons. An arc
// lives where no rectangular layout can put anything at all.
//
// Colour still means SEVERITY, exactly as the bars did; the label says which
// stat it is. The gap at 12 o'clock is for the name, and the arcs stop short of
// +-102 degrees so the ends stay clear of the poop row on the left.
#define CARE_R 216        // the track's radius
#define CARE_W 12         // and its thickness
#define CARE_LBL_R 190    // labels hug the track rather than floating mid-field
static const int16_t CARE_ARC[4][2] = {
  { -108, -82 }, { -78, -52 }, { 52, 78 }, { 82, 108 },
};
// One hue each. Four arcs in the same colour are four arcs you have to read the
// labels of; and the severity colours they used to share were 1.64:1 against
// their own groove, so the filled part barely differed from the empty part.
static const uint16_t CARE_COL[4] = {
  UI_CARE_FOOD, UI_CARE_JOY, UI_CARE_ENE, UI_CARE_HYG,
};
#define CARE_LOW 25       // below this the LABEL carries the warning

int uiCareArcTop(int i) {
  if (i < 0 || i >= 4) return -1;
  int top = 466;
  for (int a = CARE_ARC[i][0]; a <= CARE_ARC[i][1]; a++) {
    float rad = ((float)a - 90.0f) * 0.01745329f;
    int y = CY + (int)(sinf(rad) * CARE_R) - CARE_W / 2;
    if (y < top) top = y;
  }
  return top;
}

int uiHeaderNameBottom() { return HEADER_NAME_Y + HEADER_NAME_H; }

static void drawCareArc(int i, const char *label, uint8_t val) {
  int a0 = CARE_ARC[i][0], a1 = CARE_ARC[i][1];
  uiArc(CX, CY, CARE_R, a0, a1, CARE_W, UI_TRACK);
  if (val) uiArc(CX, CY, CARE_R, a0, a0 + (a1 - a0) * val / 100, CARE_W, CARE_COL[i]);
  float mid = ((float)(a0 + a1) / 2.0f - 90.0f) * 0.01745329f;
  int lx = CX + (int)(cosf(mid) * CARE_LBL_R);
  int ly = CY + (int)(sinf(mid) * CARE_LBL_R);
  gfx->setTextColor(val < CARE_LOW ? UI_BAR_BAD : inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(lx - (int)tcols(label) * 6, ly - 8);
  gfx->print(label);
}

void drawBars() {
  drawCareArc(0, T(S_BAR_FOOD), pet.fullness);
  drawCareArc(1, T(S_BAR_JOY), pet.joy);
  drawCareArc(2, T(S_BAR_ENE), pet.energy);
  drawCareArc(3, T(S_BAR_HYG), pet.hygiene);
}

void drawButtons() {
  for (int i = 0; i < BTN_COUNT; i++) {
    bool off = uiButtonDisabled(i);   // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 16, UI_WHITE);
    gfx->drawRoundRect(bx, by, 2 * BTN_HALF, 2 * BTN_HALF, 16, inkColor());
    if (!off) drawMap(buttons[i].icon, 16, buttons[i].cx - 16, buttons[i].cy - 16, 2, false);
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.weight > 60) return T(S_CHUBBY);
  if (pet.variant && pet.ageMinutes < 15) return T(S_IS_VARIANT);
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
// An 8bpp indexed avatar, same shape as the badge art: 0xFF is transparent.
void drawAvatar(uint8_t which, int x, int y, int s) {
  const AvatarArt &a = AVATARS[which % AVATAR_COUNT];
  for (int r = 0; r < AVATAR_PX; r++)
    for (int c = 0; c < AVATAR_PX; c++) {
      uint8_t v = a.idx[r * AVATAR_PX + c];
      if (v == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, a.pal[v]);
    }
}

void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      char ch = map[r][c];
      if (ch == '.') continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, silhouette ? INK_K : spriteColor(ch));
    }
  }
}
