#pragma once
#include <stdint.h>
#include "dex.h"
#include "i18n.h"   // L(): the Chinese element/attribute names

// Type effectiveness helpers. The chart itself (TYPE_FX) and the PkType enum
// are generated into dex.h by tools/gen_dex.py from tools/dex_types.py.
//
// Everything is integer percentages: 100 = neutral, 200 = super effective,
// 50 = resisted, 0 = immune, and 400 / 25 for the doubled-up dual-type cases.
// No floats anywhere, so this is cheap on the MCU.

// Effectiveness of an attacking type against a defender with one or two types.
// Pass T_NONE as def2 for single-typed defenders.
static inline uint16_t typeEffPct(uint8_t atk, uint8_t def1, uint8_t def2) {
  if (atk >= TYPE_COUNT || def1 >= TYPE_COUNT) return 100;
  uint16_t e = TYPE_FX[atk][def1];
  e *= (def2 < TYPE_COUNT) ? TYPE_FX[atk][def2] : 10;
  return e;  // tenths x tenths = percent
}

// Same, but for a Digimon straight out of the Digidex.
static inline uint16_t typeEffVsDex(uint8_t atk, int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return 100;
  return typeEffPct(atk, DEX_TBL[dex].type1, DEX_TBL[dex].type2);
}

// The whole matchup, as one percentage: the attack's ELEMENT against the
// defender's element, times the ATTRIBUTE triangle of the two Digimon
// (Vaccine > Virus > Data > Vaccine). Status moves never get here.
static inline uint16_t battleEffPct(int16_t atkDex, uint8_t moveType, int16_t defDex) {
  uint32_t e = typeEffVsDex(moveType, defDex);
  if (atkDex >= 1 && atkDex <= DEX_COUNT && defDex >= 1 && defDex <= DEX_COUNT)
    e = e * attrEffPct(DEX_TBL[atkDex].attr, DEX_TBL[defDex].attr) / 100;
  return (uint16_t)e;
}

// Same-Type Attack Bonus: 1.5x when the move matches one of the user's types.
static inline bool hasStab(int16_t dex, uint8_t moveType) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  return DEX_TBL[dex].type1 == moveType || DEX_TBL[dex].type2 == moveType;
}

// Short display names and chip colours for the elements and attributes are
// GENERATED into dex.h (ELEMENT_NAME, TYPE_COL, TYPE_COL_LIGHT, ATTR_NAME) from
// tools/digimon_data.py, so a new element cannot be missing a colour.
static inline const char *typeName(uint8_t t) {
  return (t < TYPE_COUNT) ? L(ELEMENT_NAME[t]) : "?";
}
static inline const char *attrName(uint8_t a) {
  return (a < ATTR_COUNT) ? L(ATTR_NAME[a]) : "?";
}

static inline uint16_t typeColor(uint8_t t) {
  return t < TYPE_COUNT ? TYPE_COL[t] : 0x8410;
}
static inline bool typeColorIsLight(uint8_t t) {
  return t < TYPE_COUNT && TYPE_COL_LIGHT[t];
}
