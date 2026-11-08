#pragma once
#include <Arduino.h>

// Idiomas soportados. La fuente del firmware no tiene acentos: ambos textos van
// sin tildes ni enes (igual que ya iba el espanol).
enum Lang : uint8_t { LANG_ES = 0, LANG_EN, LANG_FR, LANG_DE, LANG_IT, LANG_PT, LANG_ZH, LANG_COUNT };
// LANG_ZH is appended, never inserted: the language is stored in NVS as this index.
#define LANG_DEFAULT LANG_EN  // idioma por defecto: ingles

extern Lang gLang;  // idioma activo (definido en i18n.cpp)

// IDs de cadena. El orden debe coincidir con la tabla STRINGS de i18n.cpp.
enum StrId : uint8_t {
  // estado del bicho (statusMsg)
  S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH,
  S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_VARIANT, S_HAPPY,
  // ceremonias de despedida
  S_FAREWELL, S_RUNAWAY, S_GOODBYE,
  // huevo
  S_EGG_HDR, S_EGG_LEGEND, S_EGG_RARE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST,
  // formatos compartidos
  S_DIGIDEX_FMT,   // "DIGIDEX %u/151"
  S_NAME_FMT,      // "%s%s Nv.%u"
  // dialogo soltar
  S_RELEASE_FMT, S_YES, S_NO,
  // minijuego y saco
  S_HITS_FMT, S_STR_GAIN_FMT, S_NEW_RECORD, S_RECORD_FMT, S_HIT_FAST,
  S_SCORE_FMT, S_GREAT_JOY, S_PLUS_JOY,
  // reloj / ajustes
  S_SET_TIME, S_HOUR, S_MIN, S_CLOCK_CANCEL, S_LANG_LABEL,
  // celebracion
  S_MEDAL_BANNER, S_GREAT, S_STREAK_DAYS_FMT,
  // ficha: perfil
  S_STREAK_FMT, S_VIN, S_BERRY_UNK, S_BERRY_RED, S_BERRY_BLUE, S_BERRY_GREEN,
  S_INFO_FMT, S_RENAME_HINT,
  // ficha: combate
  S_BATTLE, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_WGT, S_TRAIN_STR,
  // ficha: medallas
  S_MEDALS_FMT, S_BACK,
  // teclado y galeria
  S_NAME, S_DETAIL_BACK,
  // barras
  S_BAR_FOOD, S_BAR_JOY, S_BAR_ENE, S_BAR_HYG,
  // marcador en vivo del minijuego
  S_REC_FMT,
  // ficha: pagina de progreso
  S_PROGRESS, S_LVL_FMT, S_NEXT_LVL_FMT, S_EVO_LABEL, S_FINAL_FORM,
  S_EVO_READY, S_EVO_BLOCKED, S_EVO_IN_FMT, S_MISTAKES_FMT,
  // interruptor de sonido (ajustes)
  S_SND_ON, S_SND_OFF,
  S_EVO_TAP,        // texto del boton de evolucion
  S_FAREWELL_BTN,   // texto del boton de despedida (lleva el nombre: "%s ...")
  S_RUNAWAY_BTN,    // texto del boton de escapada por abandono (final triste)
  // dialogos de decision (evolucionar/mantener, despedirse/quedaros)
  S_EVO_Q, S_EVO_KEEP, S_FAR_Q, S_FAR_GO, S_FAR_STAY,
  S_CHOOSE_STARTER,  // titulo de la eleccion del inicial (primera vez)
  S_NO_SPRITES, S_LOAD_SPRITES,  // aviso cuando falta el sprite en la SD
  S_STAT_VIT,   // vitalidad (abreviatura de PS/HP en cada idioma)
  S_IV_FMT,     // valor individual junto a cada stat ("IV %u")
  // menu overlay (tap the name on the main screen) and the party
  S_MENU_TITLE, S_SETTINGS, S_CLOSE,
  S_PARTY_FMT,      // "PARTY %u/6"
  S_PARTY,          // short navigation label
  S_PARTY_EMPTY,    // empty slot marker
  S_PARTY_JOINED,   // "%s joined the party!"
  S_PARTY_FULL,     // prompt: pick someone to replace
  S_PARTY_LETGO,    // decline the swap
  // stats shortcut in the menu, and the training submenu behind the 5th icon
  S_STATS,          // menu row: jump straight to the stats card page
  S_TRAIN,          // training submenu title
  S_TR_ATK,         // row: train strength (the punching bag)
  S_TR_SPE,         // row: train speed (the ball game)
  S_TR_DEF,         // row: defence -- passive, shown for information only
  S_TR_DEF_HINT,    // why the defence row does nothing when tapped
  // moves card page + the move picker behind it
  S_MOVES,          // card page title
  S_MOVE_EMPTY,     // an unused move slot
  S_MOVE_PICK,      // picker title: choose what goes in this slot
  S_MOVE_TAP,       // hint on the moves page: tap a slot to change it
  S_MOVE_PWR,       // "PWR %u" next to an attacking move
  S_MOVE_STATUS,    // shown instead of power for a status move
  // level-up learn prompt
  S_LEARN_Q,        // "%s wants to learn" (name of the creature)
  S_LEARN_SKIP,     // decline the new move
  // battle
  S_BTL_USED,       // "%s used %s"
  S_BTL_SUPER, S_BTL_WEAK, S_BTL_IMMUNE,
  S_BTL_MISS, S_BTL_CRIT, S_BTL_FAINT,
  S_BTL_HURTSELF,   // confusion
  S_BTL_STATUS,     // "%s was %s" -- ailment name comes from S_AIL_*
  S_AIL_PARA, S_AIL_BURN, S_AIL_POISON, S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE,
  S_BTL_WIN, S_BTL_LOSE,
  S_BTL_SENDS,      // "%s sends out %s"
  S_BTL_GO,         // "Go, %s!"
  S_GYMS,           // boss list title
  S_BADGES_FMT,     // "BADGES %u/8"
  S_TRAINER,        // player card title
  S_SPD_GAIN_FMT,   // "SPEED +%u" after the reaction test
  S_AVATAR_HINT,    // "tap: change avatar"
  S_MEDALS_TOTAL_FMT,  // "%u earned in all"
  S_EASY, S_HARD,   // boss ladder difficulty toggle
  S_PICK_FMT,       // "CHOSEN %u/%u"
  S_FIGHT,          // start the battle
  S_LOCKED,         // a boss not yet unlocked
  S_BTL_SWITCH,     // battle menu: bring on another creature
  S_BTL_BEAT,       // "%s defeated!" on the win screen
  S_BTL_NEWBADGE,   // a badge earned for the first time
  S_VOL_FMT,        // volume level, e.g. "VOL 7"
  S_BOX_FMT,        // "BOX %u/%u"
  S_BOX_SWAP,       // "swap with %s: pick a slot"
  S_BOX_BTN,        // the button on the party screen
  S_REVIVE,         // bring a banked creature back as a companion
  S_REVIVE_EGG,     // why it is unavailable right now
  // LAN battle
  S_LAN, S_LAN_HOST, S_LAN_JOIN, S_LAN_WAIT, S_LAN_READY, S_LAN_REFUSED,
  S_LAN_PICK, S_LAN_VS,
  S_LAN_GONE, S_LAN_WAITFOE, S_LAN_REMATCH,
  S_BTL_RUN,        // the third option in a fight: leave it
  S_EGG_REGION,     // hint under the egg's version pill
  S_WIN_TRAIN_FMT,  // "%s +%u" -- the training a boss win is worth
  S_WIN_MAXED,      // ... or that there is nowhere left to train
  S_CHOOSE_REGION,  // first boot: pick the version your starter comes from
  S_RETIRE,         // menu row: retire the creature on demand
  S_RETIRE_Q,       // ...and the confirmation
  S_RETIRE_COST,    // what retiring early costs the next creature
  S_EVO_SLOW,       // shown on the card while a creature carries that debt
  S_NEED_PACK,      // version chooser: this version's sprite pack is not on the SD
  // Letting a BANKED creature go for good, from the party or box detail sheet.
  // The confirmation reuses S_RELEASE_FMT / S_NO; its destructive action repeats
  // S_RELEASE_BTN instead of the ambiguous generic YES.
  S_RELEASE_BTN,    // the button on the detail sheet
  S_RELEASE_GONE,   // ...and the warning under it: this one does not come back
  S_BOX_TAKE,       // box detail sheet: move this creature into the party
  S_RETIRE_GONE,    // retire confirm: an early retire is not banked either
  // Wild encounters, the bag, and choosing which creature you raise.
  S_BAG,            // menu row, and the battle menu's fourth option
  S_BAG_EMPTY,      // ...with nothing in it
  S_EXPLORE,        // boss screen: go looking for a wild creature
  S_WILD_MET,       // "A wild %s appeared!"
  S_WILD_FLED,      // "%s fled!"
  S_GOT_AWAY,       // running from a wild fight succeeded
  S_NO_ESCAPE,      // ...and failed, which costs the turn
  S_THREW,          // "You threw a %s"
  S_CAUGHT,         // "%s was caught!"
  S_BROKE_FREE,     // the ball failed
  S_ITEM_USED,      // "Used %s"
  S_ITEM_NOUSE,     // the item was valid but had nothing to do
  S_FOUND_ITEM,     // a wild win's drop
  S_FOCUS,          // party sheet: make this one the active creature
  S_FOCUS_NOW,      // ...and the label once it already is the one being raised
  // settings, page 2
  S_BRIGHT_FMT,     // screen brightness level, e.g. "LIGHT 7"
  S_RESET,          // the row, and the confirm's YES: start the game over
  S_RESET_Q,        // "Erase everything?"
  S_RESET_COST,     // what it costs, on screen BEFORE the tap
  // Digidex filters. RAISED and CAUGHT are different sets: the dex records what
  // has been raised as the live creature, the party and box record what you HAVE.
  S_FILT_ALL, S_FILT_RAISED, S_FILT_CAUGHT, S_FILT_VARIANT,
  S_FILT_NONE,      // the filter matched nothing, said out loud
  // how the box is ordered on screen
  S_SORT_SLOT, S_SORT_DEX, S_SORT_LVL,
  // Personality, derived from the IVs and never stored. In the same order as the
  // PERS_* enum in pet.h -- personalityName() indexes this list with it.
  S_PERS_BOLD, S_PERS_STURDY, S_PERS_BRISK, S_PERS_HARDY, S_PERS_EAGER, S_PERS_CALM,
  // Today's care checklist. Every one is read from state the save already holds,
  // so nothing new is written and the list resets itself with the day.
  S_GOALS, S_GOALS_FMT,
  S_GOAL_CARED, S_GOAL_FED, S_GOAL_CLEAN, S_GOAL_HAPPY, S_GOAL_RESTED,
  // the memory game
  S_MEMO,           // the training-menu row
  S_MEMO_FMT,       // the result: how long a sequence was remembered
  S_MEMO_WATCH,     // the sequence is playing -- do not tap yet
  S_MEMO_GO,        // ...and now repeat it
  S_PLUS_JOY_FMT,   // "JOY +%u", the reward
  S_BOX_PUT,        // party detail sheet: move this creature into the box
  // shown while a sprite pack streams in over USB
  S_XFER, S_XFER_FMT, S_XFER_HINT,
  // Digimon: the progress card's branch inputs, and the attribute label
  S_TRAIN_TOTAL_FMT,  // "Training: %u" -- trAtk + trDef + trSpe
  S_ATTR,             // card label: Vaccine / Data / Virus / Free
  S_STAGE_0, S_STAGE_1, S_STAGE_2, S_STAGE_3, S_STAGE_4, S_STAGE_5,
  S_TAP_MORE,         // under a battle message: tap for the next one
  STR_COUNT
};

const char *T(StrId id);       // texto en el idioma activo

// --- Chinese ---------------------------------------------------------------
// The data names (Digimon, items, elements, areas, avatars) are English in the
// six Latin-script languages. L() is the one place that swaps one for its
// Chinese name when LANG_ZH is active; anything it does not know (a nickname,
// a string that is already translated) comes back unchanged.
const char *L(const char *english);
// Display width in columns of the 6 px bitmap font: ASCII is one, a CJK
// character two. Every "centre this text" calculation asks this, not strlen():
// a Chinese character is three bytes of UTF-8 but two columns wide.
int tcols(const char *s);
// The 12x12 glyph for a code point, or nullptr if the font has none.
#ifndef CJK_CELL
#define CJK_CELL 12   // also emitted by tools/gen_zh.py into cjk_font.h
#endif
const uint8_t *cjkGlyph(uint32_t cp);
const char *moveName(uint8_t move);  // localized display name; move ID stays stable
const char *medalName(int i);  // banner de medalla (MED_COUNT)
const char *medalLabel(int i); // etiqueta corta de medalla
const char *medalDesc(int i);  // descripcion larga de medalla

void loadLang();             // lee el idioma de NVS (llamar en setup)
void setLang(Lang l);        // cambia y persiste el idioma
