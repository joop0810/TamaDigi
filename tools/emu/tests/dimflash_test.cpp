// A tap must never dim the panel.
//
// loop() reads millis() once at the top and hands that `now` to
// updateBrightness() further down, but a tap handled in between sets
// lastInteract a few ms LATER. `now - lastInteract` was unsigned, so it wrapped
// to ~49 days and the panel dropped to brightness 8 -- practically black --
// for a moment after a tap. Reported from a board as "the display goes dark
// for a while during fights", where you tap every turn (TamaNaruto, same engine).
//
// The real updateBrightness(), the real dim ladder; nothing restated here.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
void updateBrightness(uint32_t now);
extern uint32_t lastInteract;
extern uint8_t dimStage, gBrightNow;
extern bool screenOff;
extern Pet pet;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  if (pet.awaitingStarter()) pet.chooseStarter(0);
  if (pet.isEgg()) pet.dbgHatchAs(1,false);
  pet.sleeping = false; screenOff = false;

  const uint32_t now = 1000000;
  lastInteract = now; updateBrightness(now);
  const uint8_t awake = gBrightNow;
  ck(dimStage == 0 && awake > 8, "baseline: an attended screen is at full brightness");

  // the tap landed 4 ms after loop() read the clock
  lastInteract = now + 4; updateBrightness(now);
  ck(dimStage == 0, "a tap later in the same pass is not 'idle for 49 days'");
  ck(gBrightNow == awake, "and the panel keeps its brightness instead of dropping to 8");

  // the mechanism still engages: the ladder is not simply switched off
  lastInteract = now; updateBrightness(now + 91000);
  ck(dimStage == 1, "after 90 s untouched it still dims");
  updateBrightness(now + 301000);
  ck(dimStage == 2 && gBrightNow == 8, "and after 5 minutes it drops to 8");
  lastInteract = now + 301000; updateBrightness(now + 301000);
  ck(dimStage == 0 && gBrightNow == awake, "and a touch brings it straight back");

  printf("%s\n", bad ? "FAILURES" : "a tap never darkens the panel");
  return bad ? 1 : 0;
}
