// TamaDigi desktop emulator: runs the real sketch, draws the real framebuffer,
// and feeds mouse clicks in as touch events. Serial commands come from stdin.
#include <SDL.h>   // sdl2-config puts the SDL2 dir on the include path
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "i18n.h"
#include "party.h"
#include "battle.h"
#include <chrono>
#include <string>
#include <deque>
#include <unistd.h>
#include <sys/select.h>

// --- Arduino runtime globals ---
uint32_t g_seed = 0xC0FFEE;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

// millis() lives in clock.cpp so the headless tests share it
void FakeESP::restart() { Serial.println("emu: ESP.restart() -> exiting"); exit(0); }

// --- stdin-backed serial ---
static std::deque<std::string> g_lines;
static std::string g_partial;
static void pumpStdin() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  struct timeval tv = { 0, 0 };
  while (select(1, &fds, nullptr, nullptr, &tv) > 0) {
    char buf[512];
    ssize_t n = read(0, buf, sizeof(buf));
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') { g_lines.push_back(g_partial); g_partial.clear(); }
      else g_partial += buf[i];
    }
    FD_ZERO(&fds); FD_SET(0, &fds); tv = { 0, 0 };
  }
}
// --- simulating a crash ---
//
// The board keeps its breadcrumb in RTC memory, which survives a panic but not
// a power cycle. A process has no such thing, so the emulator parks the crumb
// in a file beside the save and re-execs itself: the window closes and reopens
// reporting the crash, which is exactly what a player sees.
//
// PANIC is handled HERE and never reaches the firmware, because a serial
// command that fakes a crash has no business existing on real hardware.
static std::string g_crashFile;
void emuSetResetReason(int r);
extern uint32_t gCrumbMagic, gCrumbScreen, gCrumbHeap;
static char **g_argv = nullptr;

static void crashArmAndReexec(int reason) {
  FILE *f = fopen(g_crashFile.c_str(), "wb");
  if (f) {
    fprintf(f, "%d %u %u %u\n", reason, gCrumbMagic, gCrumbScreen, gCrumbHeap);
    fclose(f);
  }
  printf("\nemu: simulating a %s -- restarting the way the board would\n\n",
         reason == ESP_RST_PANIC ? "PANIC" : "TASK WATCHDOG");
  fflush(stdout);
  execv("/proc/self/exe", g_argv);      // linux
  execv(g_argv[0], g_argv);             // macos and anything else
  perror("emu: could not re-exec");
  exit(1);
}

// Reads back what the "crash" left behind, so the firmware's bootReport() sees
// exactly the state a real panic would have left in RTC memory.
static void crashRestore() {
  FILE *f = fopen(g_crashFile.c_str(), "rb");
  if (!f) return;
  unsigned magic = 0, screen = 0, heap = 0; int reason = 0;
  if (fscanf(f, "%d %u %u %u", &reason, &magic, &screen, &heap) == 4) {
    emuSetResetReason(reason);
    gCrumbMagic = magic; gCrumbScreen = screen; gCrumbHeap = heap;
  }
  fclose(f);
  remove(g_crashFile.c_str());
}

int FakeSerial::available() {
  // intercept the emulator-only commands before the firmware ever sees them
  while (!g_lines.empty()) {
    const std::string &l = g_lines.front();
    if (l == "PANIC") { g_lines.pop_front(); crashArmAndReexec(ESP_RST_PANIC); }
    else if (l == "WDT") { g_lines.pop_front(); crashArmAndReexec(ESP_RST_TASK_WDT); }
    else break;
  }
  return g_lines.empty() ? 0 : 1;
}
String FakeSerial::readStringUntil(char) {
  if (g_lines.empty()) return String("");
  String s(g_lines.front());
  g_lines.pop_front();
  return s;
}

// the sketch
void emuSetSpriteDir(const char *);
void startBattle(int16_t dex, uint8_t lvl);
extern Combatant btlYou, btlFoe;
extern uint32_t btlLungeUntil[2], btlHitUntil[2];
extern uint8_t btlMenu;
void startTrainerBattle(uint8_t idx, bool hard);
uint8_t currentExploreRegion();
bool startWildBattle(uint8_t region, bool hard);
void onTap(int16_t x, int16_t y);   // the first-boot shots tap their way in
extern bool exploreOpen, gymOpen, playerOpen;
extern bool galleryDirty;
extern uint8_t galleryRegion;
extern uint8_t gymRegion;
extern bool gymPick, galleryPick;
int wavMain(const char *path, const char *demo);
extern bool lanOpen;
extern uint8_t btlMyAct;
extern uint8_t btlTrainGain, btlTrainWhich;
extern bool lanWantHost;
extern bool gShowAllAvatars;
// Unlocks the emulator-only console commands (MAX). The firmware compiles them
// and never sets this; see the note beside it in TamaDigi.ino.
extern bool gEmuDebug;
#define PICK_LAN 0xFF
uint8_t squadCap(uint8_t, bool);
#include "link.h"
extern Link lan;
void startLinkBattle();
extern uint8_t playerPage;
extern bool gymHard, pickOpen;
extern uint8_t partyDetail;
extern bool boxOpen; extern uint8_t boxSwapFrom;
extern bool btlNewBadge; extern uint32_t btlWinUntil;
extern uint8_t pickTrainer, pickPage;
void pickDefault(uint8_t cap);
extern bool pickHard;
void startSpeedGame();
void setup();
void loop();
void render();
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick, trainOpen, movePickOpen;
extern uint8_t cardPage;

#define PANEL 466

// Headless capture, so the layout can be checked without a display. Writes a
// PPM (no image library needed); convert with `sips -s format png`.
static void writePPM(const char *path) {
  const uint16_t *fb = gfx->buffer();
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
  fprintf(f, "P6\n%d %d\n255\n", PANEL, PANEL);
  for (int i = 0; i < PANEL * PANEL; i++) {
    uint16_t c = fb[i];
    int x = i % PANEL, y = i / PANEL, dx = x - 233, dy = y - 233;
    uint8_t r = ((c >> 11) & 31) * 255 / 31;
    uint8_t g = ((c >> 5) & 63) * 255 / 63;
    uint8_t b = (c & 31) * 255 / 31;
    if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }  // off-panel
    fputc(r, f); fputc(g, f); fputc(b, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static int shotLangArg = -1;
extern uint32_t gTextBroken;
static int shotMode(const char *screen, const char *out, int lvl, int iv, int dex) {
  setup();
  if (shotLangArg >= 0 && shotLangArg < LANG_COUNT) gLang = (Lang)shotLangArg;
  for (int i = 0; i < 4; i++) loop();          // let the sketch settle
  bool firstBoot = !strcmp(screen, "starter") || !strcmp(screen, "starterj") ||
                   !strcmp(screen, "region");
  if (pet.awaitingStarter() && !firstBoot) pet.chooseStarter(4);
  if (!strcmp(screen, "egg")) {
    // a few Digimon registered, so the lottery is past the starter case and the
    // version pill has something to switch between
    for (int d = 1; d <= 40; d++) pet.dbgHatchAs(d, false);
    pet.newEgg();
  }
  // dbgHatchAs() clears starterPick, so hatching here would skip the very
  // screen the first-boot shots are trying to photograph.
  if (pet.isEgg() && !firstBoot && strcmp(screen, "egg")) pet.dbgHatchAs(dex, false);
  if (lvl > 0) pet.ageMinutes = (uint32_t)(lvl - 1) * MINUTES_PER_LEVEL;
  if (iv >= 0) {
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = iv;
    pet.trAtk = pet.trMaxAtk(); pet.trDef = pet.trMaxDef(); pet.trSpe = pet.trMaxSpe();
  }
  // The learn prompt is modal and would cover whatever screen was asked for.
  // It started firing for every shot once digimon_data.py gained the cheap early
  // attacks, because a creature now genuinely has moves waiting.
  while (pet.hasLearnOffer()) pet.declineLearn();
  // "version" is step one of the first boot and "starter" is step two, so the
  // second one gets there the way a player does: by tapping a version.
  if (!strcmp(screen, "starter")) onTap(233, 108 + 30);        // VER.1
  if (!strcmp(screen, "starterj")) onTap(233, 108 + 72 + 30);  // VER.2
  for (int i = 0; i < 2; i++) loop();          // pick up the sprite for the new Digimon
  cardOpen = galleryOpen = clockOpen = kbOpen = false;
  menuOpen = partyOpen = partyPick = trainOpen = movePickOpen = false;
  if (!strcmp(screen, "battle"))      { cardOpen = true; cardPage = 1; }
  else if (!strcmp(screen, "profile")){ cardOpen = true; cardPage = 0; }
  else if (!strcmp(screen, "medals")) { cardOpen = true; cardPage = 3; }
  else if (!strcmp(screen, "progress")){cardOpen = true; cardPage = 4; }
  else if (!strcmp(screen, "gallery")) {
    // The grid is static and guarded by galleryDirty, so setting galleryOpen
    // alone leaves the screenshot black -- the real UI sets both.
    galleryOpen = true;
    galleryDirty = true;
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
  }
  else if (!strcmp(screen, "gallery2")) {   // the second version
    galleryOpen = true;
    galleryDirty = true;
    galleryRegion = 1;
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
  }
  else if (!strcmp(screen, "clock"))   clockOpen = true;
  else if (!strcmp(screen, "menu"))    menuOpen = true;
  else if (!strcmp(screen, "train"))   trainOpen = true;
  else if (!strcmp(screen, "moves"))   { cardOpen = true; cardPage = 2; }
  else if (!strcmp(screen, "movepick")) { movePickOpen = true; }
  else if (!strcmp(screen, "battle2")) { startBattle(9, 50); }
  else if (!strcmp(screen, "wild")) { startWildBattle(currentExploreRegion(), false); }
  else if (!strcmp(screen, "btlmenu")) { startTrainerBattle(3, false); }
  else if (!strcmp(screen, "btlswitch")) { startTrainerBattle(3, false); btlMenu = 2; }
  else if (!strcmp(screen, "btlmoves")) { startTrainerBattle(3, false); btlMenu = 1; }
  else if (!strcmp(screen, "battleanim")) {
    startBattle(9, 50);
    btlFoe.hp = btlFoe.maxHp / 3;      // bar mid-drain
    btlLungeUntil[0] = millis() + 130; // you mid-lunge
    btlHitUntil[1] = millis() + 300;   // foe flinching
  }
  else if (!strcmp(screen, "explore")) exploreOpen = true;
  else if (!strcmp(screen, "gyms")) { gymOpen = true; }
  else if (!strcmp(screen, "gympick")) { gymOpen = true; gymPick = true; }
  else if (!strcmp(screen, "dexpick")) {
    for (int d = 1; d <= 200; d++) pet.dbgHatchAs(d, false);
    galleryOpen = true; galleryPick = true;
  }
  else if (!strcmp(screen, "gymsj")) { gymOpen = true; gymRegion = 1; }
  else if (!strcmp(screen, "gymsh")) { gymOpen = true; gymRegion = 2; }
  else if (!strcmp(screen, "lan")) { lanOpen = true; lan.state = LINK_OFF; }
  else if (!strcmp(screen, "lanpick")) {
    static const int f[]={10,20,12,7,21,5};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=f[i]; m.level=40+i*5;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    lanWantHost = true;
    pickTrainer = PICK_LAN; pickHard = false; pickPage = 0;
    pickDefault(squadCap(PICK_LAN, false));
    pickOpen = true;
  }
  else if (!strcmp(screen, "lanlost")) { lanOpen = true; lan.begin(true,"D"); lan.state = LINK_LOST; }
  else if (!strcmp(screen, "lanready") || !strcmp(screen, "lanbattle") ||
           !strcmp(screen, "landone") || !strcmp(screen, "lanwait")) {
    // No radio here, so the pairing is faked at the point the radio would have
    // finished it: both squads present, state READY. Everything past this --
    // layout, the battle screen, the guest's restrictions -- is the real code.
    pet.renameTrainer("DYLAN");
    lan.begin(true, "DYLAN");
    snprintf(lan.peerName, sizeof(lan.peerName), "%s", "RIVAL");
    static const int mineDex[] = {10, 20, 12}, theirDex[] = {6, 14, 21};
    for (int i = 0; i < 3; i++) {
      Pet t; t.dbgHatchAs(mineDex[i], false);
      t.ivAtk = t.ivDef = t.ivSpe = t.ivHp = 25;
      t.ageMinutes = 49UL * MINUTES_PER_LEVEL; t.relearnFromLevel();
      PartyMon m; m.dex = mineDex[i]; m.level = 50;
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      for (int k = 0; k < MOVE_SLOTS; k++) m.moves[k] = t.moves[k];
      party.replaceAt(i, m);
      Combatant mc; combatantFromParty(mc, m);
      LinkMon mine; linkMonFrom(mine, mc); lan.addMon(mine);

      Pet u; u.dbgHatchAs(theirDex[i], false);
      u.ivAtk = u.ivDef = u.ivSpe = u.ivHp = 25;
      u.ageMinutes = 49UL * MINUTES_PER_LEVEL; u.relearnFromLevel();
      Combatant c; combatantFromPet(c, u);
      LinkMon lm; linkMonFrom(lm, c);
      lan.theirs[i] = lm;
    }
    lan.theirsN = 3;
    lan.state = LINK_READY;
    if (!strcmp(screen, "lanbattle")) { startLinkBattle(); }
    else if (!strcmp(screen, "landone")) { lan.state = LINK_DONE; lan.youWon = true; lanOpen = true; }
    else if (!strcmp(screen, "lanwait")) {
      startLinkBattle();
      btlMyAct = LINK_ACT_MOVE(0);      // tapped, rival has not chosen yet
    }
    else lanOpen = true;
  }
  else if (!strcmp(screen, "box")) {
    static const int f[]={10,20,12,7,21,5};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=f[i]; m.level=40+i*5;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    static const int b[]={6,14,13,12,26,21,7,20};
    for(int i=0;i<8;i++){ PartyMon m; m.dex=b[i]; m.level=30+i*4;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; m.variant=(i==3);
      party.box[i]=m; }
    party.boxSave();
    partyOpen=true; boxOpen=true; boxSwapFrom=1;
  }
  else if (!strcmp(screen, "win")) {
    startTrainerBattle(2, true);
    btlNewBadge = true; btlWinUntil = 60000; pet.badgesHard = 0x07;
    btlTrainGain = 8; btlTrainWhich = 2;
  }
  else if (!strcmp(screen, "pmon")) {
    Pet t; t.dbgHatchAs(21,false); t.ivAtk=t.ivDef=t.ivSpe=t.ivHp=27;
    t.ageMinutes=53UL*MINUTES_PER_LEVEL; t.relearnFromLevel();
    PartyMon m; m.dex=21; m.level=54; m.variant=1;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=27; m.trAtk=m.trDef=m.trSpe=40;
    for(int k=0;k<MOVE_SLOTS;k++) m.moves[k]=t.moves[k];
    snprintf(m.nick,sizeof(m.nick),"NESSIE");
    party.replaceAt(0,m);
    partyOpen = true; partyDetail = 1;
  }
  else if (!strcmp(screen, "pick")) {
    static const int fill[]={10,20,12,7,21,5};
    static const int fill6[]={10,20,12,7,21,5};
    for(int i=0;i<6;i++){ PartyMon m; m.dex=fill6[i]; m.level=40+i*6;
      m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=25; party.replaceAt(i,m); }
    pickTrainer = 7; pickHard = false; pickPage = 0;
    pickDefault(6); pickOpen = true;
  }
  else if (!strcmp(screen, "gymshard")) { gymOpen = true; gymHard = true; pet.badgesHard = 0x03; }
  else if (!strcmp(screen, "speed")) { startSpeedGame(); }
  else if (!strcmp(screen, "avatars")) {
    pet.renameTrainer("DYLAN");
    playerOpen = true;
    gShowAllAvatars = true;
  }
  else if (!strcmp(screen, "player")) { pet.renameTrainer("DYLAN"); pet.badges = 0xBF; pet.badgesHard = 0x0A; pet.streak = 5; playerOpen = true; }
  else if (!strcmp(screen, "player2")) {
    pet.renameTrainer("DYLAN");
    pet.badgesX[0] = 0x3F; pet.badgesHardX[0] = 0x05;   // Ver.2
    playerOpen = true; playerPage = 1;
  }
  else if (!strcmp(screen, "medals2")) { pet.medals = 0x5B; pet.totalMedals = 12; playerOpen = true; playerPage = 1; }
  else if (!strcmp(screen, "gymfight")) { startTrainerBattle(0, false); }
  else if (!strcmp(screen, "learn")) {
    // fill the four slots, then cross a gate so the offer has to be answered
    pet.relearnFromLevel();
    pet.lastLearnLevel = 0;
    pet.checkLearnGates();
  }
  else if (!strcmp(screen, "party") || !strcmp(screen, "partyfull")) {
    partyOpen = true;
    static const int fill[] = { 6, 20, 26, 7, 12, 21 };
    static const char *nk[] = { "EMBER", "SPARK", "", "SPOOK", "", "" };
    int n = !strcmp(screen, "partyfull") ? 6 : 3;
    for (int i = 0; i < n; i++) {
      PartyMon m;
      m.dex = fill[i]; m.level = 40 + i * 7; m.variant = (i == 2);
      m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 25;
      snprintf(m.nick, sizeof(m.nick), "%s", nk[i]);
      party.replaceAt(i, m);
    }
    if (!strcmp(screen, "partyfull")) partyPick = true;
  }
  render();
  writePPM(out);
  // text the canvas could not draw as written (a cut UTF-8 sequence, a glyph
  // the CJK font lacks): the --lang sweep in zh_shots.sh fails on any
  if (gTextBroken) fprintf(stderr, "text: %u broken on '%s'\n", (unsigned)gTextBroken, screen);
  return gTextBroken ? 3 : 0;
}

int main(int argc, char **argv) {
  int scale = 2;
  g_argv = argv;
  const char *save = "tamadigi.nvs";
  const char *shot = nullptr, *shotOut = "shot.ppm";
  const char *wav = nullptr, *demo = nullptr;
  int shotLvl = 0, shotIv = -1, shotDex = 5, shotLang = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--fast") && i + 1 < argc) emuSetTimeScale(atoi(argv[++i]));
    else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
    else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = argv[++i];
    else if (!strcmp(argv[i], "--demo") && i + 1 < argc) demo = argv[++i];
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--out") && i + 1 < argc) shotOut = argv[++i];
    else if (!strcmp(argv[i], "--lvl") && i + 1 < argc) shotLvl = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--iv") && i + 1 < argc) shotIv = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--dex") && i + 1 < argc) shotDex = atoi(argv[++i]);
    // the UI language for a shot (index into Lang; 6 = Chinese)
    else if (!strcmp(argv[i], "--lang") && i + 1 < argc) shotLang = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sprites") && i + 1 < argc) emuSetSpriteDir(argv[++i]);
    else if (!strcmp(argv[i], "--wipe")) { remove(save); }
  }
  // This is the emulator, so the emulator-only console commands are available.
  // Set HERE and nowhere else -- the firmware compiles them and leaves the flag
  // false, which is what keeps both builds compiling the same sources.
  gEmuDebug = true;
  // Audio preview: no SDL, no board -- just a file you can listen to.
  if (wav) return wavMain(wav, demo);
  shotLangArg = shotLang;
  if (shot) return shotMode(shot, shotOut, shotLvl, shotIv, shotDex);   // headless: no SDL at all
  g_crashFile = std::string(save) + ".crash";
  crashRestore();     // a simulated panic left its breadcrumb here
  nvsLoad(save);

  if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
  SDL_Window *win = SDL_CreateWindow("TamaDigi (emulator)", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, PANEL * scale, PANEL * scale,
                                     SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
                                       SDL_TEXTUREACCESS_STREAMING, PANEL, PANEL);

  printf("TamaDigi emulator — click to touch, drag to swipe, hold 3s to release.\n");
  printf("Type serial commands here (STATS, IV 31 31 31 31, EGG 150 1, LVL 73, WIPE...)\n");
  printf("Time scale x%u (suspended while you touch). Ctrl-C or close the window to quit.\n\n",
         emuTimeScale());

  setup();

  bool run = true;
  std::vector<uint32_t> px(PANEL * PANEL);
  while (run) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) run = false;
      // Every mouse event pulses the touch INT, the way the CST9217 does on the
      // board. The sketch ignores the panel entirely until that fires.
      else if (e.type == SDL_MOUSEBUTTONDOWN) {
        g_touchX = e.button.x / scale; g_touchY = e.button.y / scale; g_touchDown = true;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEBUTTONUP) {
        g_touchDown = false;
        emuFireInterrupt();
      } else if (e.type == SDL_MOUSEMOTION && g_touchDown) {
        g_touchX = e.motion.x / scale; g_touchY = e.motion.y / scale;
        emuFireInterrupt();
      } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) run = false;
    }
    pumpStdin();
    loop();

    if (gfx && gfx->frameReady) {
      gfx->frameReady = false;
      const uint16_t *fb = gfx->buffer();
      for (int y = 0; y < PANEL; y++)
        for (int x = 0; x < PANEL; x++) {
          uint16_t c = fb[y * PANEL + x];
          uint32_t r = ((c >> 11) & 31) * 255 / 31;
          uint32_t g = ((c >> 5) & 63) * 255 / 63;
          uint32_t b = (c & 31) * 255 / 31;
          // dim what falls outside the round panel: those pixels exist in the
          // framebuffer but are not visible on the real hardware
          int dx = x - 233, dy = y - 233;
          if (dx * dx + dy * dy > 233 * 233) { r /= 4; g /= 4; b /= 4; }
          px[y * PANEL + x] = (r << 16) | (g << 8) | b;
        }
      SDL_UpdateTexture(tex, nullptr, px.data(), PANEL * 4);
      SDL_RenderCopy(ren, tex, nullptr, nullptr);
      SDL_RenderPresent(ren);
    }
    SDL_Delay(5);
  }

  nvsSave(save);
  printf("\nemu: state saved to %s\n", save);
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
