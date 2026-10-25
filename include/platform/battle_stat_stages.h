#ifndef GUARD_PLATFORM_BATTLE_STAT_STAGES_H
#define GUARD_PLATFORM_BATTLE_STAT_STAGES_H

/*
 * Stat stages as Pokemon Showdown shows them, and kept until a Pokemon faints or the battle ends (PC only; the owner's
 * request of 2026-10-24: "a way for the game to show you what everyone's debuffs and buffs are like in Pokemon
 * Showdown, with that being said buff and debuffs should carry out til the pokemon dies or match/battle ends").
 * src/platform/battle_stat_stages.c has the whole story.
 */
#include "global.h"
#include "constants/battle.h"
#include "constants/pokemon.h"

// A PC-only battle controller command (beside the game's CONTROLLER_* list, which ends at 53): in a battle between
// players the link master sends every battler's stat stages to every game whenever one changes, since the other games
// never run the battle engine. Bytes: the command, the battler count, 0, 0, then for each of the four battlers its eight
// stages (gBattleMons[].statStages, 0-12, 6 = no change). The app's battle route knows it (battle_controller.py).
#define CONTROLLER_PC_STAT_STAGES 60
#define PC_STAT_STAGES_RAW_SIZE   (4 + MAX_BATTLERS_COUNT * NUM_BATTLE_STATS)

// What a tag shows: a stat (STAT_ATK...STAT_EVASION) that is not at stage 0, and its stage (-6..+6).
struct StatStageTag
{
    u8 stat;
    s8 stage;
    char text[8]; // "ATK+2", "SPE-1": the label the screens draw
};

// The battle's memory (the engine: a single-player battle, or the link master)
void StatStages_BattleStart(void);
void StatStages_BattleEnd(void);
void StatStages_SwitchOut(enum BattlerId battler);
void StatStages_SwitchIn(enum BattlerId battler);
void StatStages_Fainted(enum BattlerId battler);
s8 StatStages_Stored(enum BattleTrainer trainer, u32 partyIndex, enum Stat stat);

// A battle between players
bool32 StatStages_SyncTick(bool32 battleUnderway);
bool32 StatStages_RunCommand(enum BattlerId battler);
enum BattlerId StatStages_BuildCommand(u8 *out);
bool32 StatStages_CommandValid(const u8 *p, u32 battlerCount);

// What the screens show
void StatStages_Shown(enum BattlerId battler, s8 out[NUM_BATTLE_STATS]);
u32 StatStages_Tags(enum BattlerId battler, struct StatStageTag *tags);
void StatStages_DrawGba(u16 *pixels);

// Where a battler's healthbox is drawn in the GBA picture (battle_interface.c), while its sprite is still that battler's
// healthbox (a menu's return rebuilds the sprites) and shown: the 64x32 main sprite's corner and the plate's box.
struct PcHealthbox
{
    s16 x, y;
    s16 plateX, plateY, plateW, plateH;
    u8 paletteNum;
    bool8 playerStyle;
};
bool32 Battle_PcHealthbox(u32 battler, struct PcHealthbox *out);
// What a tag may not cover: every healthbox's plate, the ability pop-ups, the last used Ball, the gimmick button.
struct PcRect
{
    s16 x, y, w, h;
};
u32 Battle_PcProtectedRects(struct PcRect *out, u32 max);

#endif // GUARD_PLATFORM_BATTLE_STAT_STAGES_H
