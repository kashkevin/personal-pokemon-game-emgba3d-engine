/*
 * Stat stages as Pokemon Showdown shows them, kept until a Pokemon faints or the battle ends (PC only; the owner's
 * request of 2026-10-24: "can we add a way for the game to show you what everyone's debuffs and buffs are like in
 * Pokemon Showdown, with that being said buff and debuffs should carry out til the pokemon dies or match/battle ends").
 *
 * What the player sees: beside every Pokemon's healthbox a small tag for each stat that is not at stage 0, "ATK+2" in
 * blue, "SPE-1" in red, in Showdown's order (Attack, Defense, Sp. Atk, Sp. Def, Speed, accuracy, evasion), for every
 * battler on the field, both sides, singles, doubles, multi and free-for-all battles. Classic 2D draws them into the
 * GBA picture beside the healthboxes (StatStages_DrawGba, from platform/sdl2.c VDraw), never over a healthbox, the
 * message box and menus below it, an ability pop-up, the last used Ball or the Mega Evolution button; the 3D game's
 * battle HUD draws them beside its databoxes (native_ui/screens/ui_battle.c), and the 3D battles that keep the game's
 * own healthboxes (the Battle Arena, the UI turned off) get the Classic 2D ones.
 *
 * The rule, in every battle (wild, trainer, Battle Frontier, between players): a Pokemon that leaves the field keeps its
 * stages - switched out, dragged out by Roar, Whirlwind, Dragon Tail or Red Card, gone by U-turn, Volt Switch, Eject
 * Button, Eject Pack, Emergency Exit - and has them again when it comes back in. They go when it faints, and with the
 * battle. Baton Pass still gives the user's stages to the Pokemon coming in (in place of any that Pokemon had kept), and
 * the Pokemon that passed starts at 0 when it comes back, so nothing is counted twice. Haze, Clear Smog and the rest
 * still reset the Pokemon on the field (one in the party keeps what it had). Nothing else carries over: confusion,
 * Substitute, Leech Seed, Focus Energy and every other volatile effect still go with the switch, as in the game.
 * The stages are kept per party member of each of the battle's four parties (gParties: the player's, the opponents', a
 * partner's; in a battle between players each player's, as the master's engine has them), in battle memory only:
 * cleared as each battle starts and ends, never saved. The engine keeps doing the rest: SwitchInClearSetData resets the
 * incoming Pokemon (or keeps Baton Pass's), then SwitchIn puts back what that Pokemon had kept.
 *
 * Hooks (GBA files, PORTABLE blocks): battle_script_commands.c Cmd_getswitchedmondata (SwitchOut, before the place's
 * party index changes) and Cmd_switchindataupdate (SwitchIn, after the game's switch-in clearing); battle_main.c
 * FaintClearSetData (Fainted) and BattleMainCB1 (SyncTick, RunCommand); battle_util2.c Allocate/FreeBattleResources
 * (BattleStart/End); battle_interface.c Battle_PcHealthbox; multiplayer_battle_controller.c (the command's size and
 * check).
 *
 * A battle between players: only the link master runs the battle engine; the other games replay its controller commands
 * and their gBattleMons stay empty. Whenever a battler's stages differ from what the games were last told, the master
 * sends CONTROLLER_PC_STAT_STAGES (every battler's stages) the moment its engine waits for nothing, and the engine waits
 * until every game has handled it (SyncTick); every game, the master too, shows what that command said (RunCommand), so
 * both screens change their tags together. A Pokemon switched back in on the master is sent with its stages put back
 * before its healthbox comes up. Each game checks the command as every other (multiplayer_battle_controller.c), and so
 * does the host's battle route (the app's battle_controller.py).
 *
 * Project-owned.
 */
#include "global.h"
#ifdef PORTABLE
#include <string.h>
#include "battle.h"
#include "battle_controllers.h"
#include "battle_interface.h"
#include "battle_main.h"
#include "battle_util.h"
#include "main.h"
#include "move.h"
#include "palette.h"
#include "sprite.h"
#include "constants/battle.h"
#include "constants/battle_move_effects.h"
#include "constants/pokemon.h"
#include "platform/battle_stat_stages.h"
#ifdef ENABLE_NATIVE_UI
// native_ui/native_ui.h (it brings SDL's headers, which the game's code cannot take): the 3D battle HUD is up
extern _Bool NativeUi_ReplacesBattleTextBox(void);
#endif

#define STAGE_CHANGE(stage) ((s8)((s32)(stage) - DEFAULT_STAT_STAGE))

// Stages as changes (-6..+6, 0 none): a zeroed table is a battle where nothing changed. Kept: the Pokemon that left the
// field alive. Shown: a battle between players, what the master said. Sent: the master, what the games were told.
// Passing: leaving by Baton Pass (the game passes them). In flight: the master's command not yet handled everywhere.
static EWRAM_DATA s8 sStatStagesStored[MAX_BATTLE_TRAINERS][PARTY_SIZE][NUM_BATTLE_STATS] = {0};
static EWRAM_DATA s8 sStatStagesShown[MAX_BATTLERS_COUNT][NUM_BATTLE_STATS] = {0};
static EWRAM_DATA s8 sStatStagesSent[MAX_BATTLERS_COUNT][NUM_BATTLE_STATS] = {0};
static EWRAM_DATA bool8 sStatStagesPassing[MAX_BATTLERS_COUNT] = {0};
static EWRAM_DATA bool8 sStatStagesInFlight = FALSE;

void StatStages_BattleStart(void)
{
    memset(sStatStagesStored, 0, sizeof(sStatStagesStored));
    memset(sStatStagesShown, 0, sizeof(sStatStagesShown));
    memset(sStatStagesSent, 0, sizeof(sStatStagesSent));
    memset(sStatStagesPassing, 0, sizeof(sStatStagesPassing));
    sStatStagesInFlight = FALSE;
}

void StatStages_BattleEnd(void)
{
    StatStages_BattleStart();
}

static s8 *StoredOf(enum BattlerId battler)
{
    enum BattleTrainer trainer;
    u32 slot;

    if (battler >= MAX_BATTLERS_COUNT)
        return NULL;
    trainer = GetBattlerTrainer(battler);
    slot = gBattlerPartyIndexes[battler];
    if (trainer >= MAX_BATTLE_TRAINERS || slot >= PARTY_SIZE)
        return NULL;
    return sStatStagesStored[trainer][slot];
}

// Cmd_getswitchedmondata, the place still holding the Pokemon that leaves: its stages are kept, unless it fainted or
// passes them on with Baton Pass (the game's switchindataupdate gives them to the next one on the same condition).
void StatStages_SwitchOut(enum BattlerId battler)
{
    s8 *stored = StoredOf(battler);
    bool32 alive;

    if (stored == NULL)
        return;
    alive = gBattleMons[battler].hp != 0;
    sStatStagesPassing[battler] = alive && GetMoveEffect(gCurrentMove) == EFFECT_BATON_PASS;
    for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
        stored[i] = alive && !sStatStagesPassing[battler] ? STAGE_CHANGE(gBattleMons[battler].statStages[i]) : 0;
}

// Cmd_switchindataupdate, after the game's own clearing (SwitchInClearSetData): the incoming Pokemon gets back what it
// had kept, unless Baton Pass gave it the passer's (then its own go).
void StatStages_SwitchIn(enum BattlerId battler)
{
    s8 *stored = StoredOf(battler);

    if (stored == NULL)
        return;
    if (!sStatStagesPassing[battler])
    {
        for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
        {
            s32 stage = DEFAULT_STAT_STAGE + stored[i];

            gBattleMons[battler].statStages[i] = stage < MIN_STAT_STAGE ? MIN_STAT_STAGE
                                               : stage > MAX_STAT_STAGE ? MAX_STAT_STAGE : stage;
        }
    }
    sStatStagesPassing[battler] = FALSE;
    memset(stored, 0, NUM_BATTLE_STATS);
}

// FaintClearSetData: a fainted Pokemon has nothing left to bring back (Revival Blessing, a Revive).
void StatStages_Fainted(enum BattlerId battler)
{
    s8 *stored = StoredOf(battler);

    if (battler < MAX_BATTLERS_COUNT)
        sStatStagesPassing[battler] = FALSE;
    if (stored != NULL)
        memset(stored, 0, NUM_BATTLE_STATS);
}

s8 StatStages_Stored(enum BattleTrainer trainer, u32 partyIndex, enum Stat stat)
{
    if (trainer >= MAX_BATTLE_TRAINERS || partyIndex >= PARTY_SIZE || stat >= NUM_BATTLE_STATS)
        return 0;
    return sStatStagesStored[trainer][partyIndex][stat];
}

/* ---------------------------------------------------------------- a battle between players */

// The command for every battler's stages now, and the first battler whose stages differ from what the games were last
// told (MAX_BATTLERS_COUNT: nothing to tell). A place with no Pokemon (a free-for-all's empty one) is at 0.
enum BattlerId StatStages_BuildCommand(u8 *out)
{
    enum BattlerId changed = MAX_BATTLERS_COUNT;

    memset(out, 0, PC_STAT_STAGES_RAW_SIZE);
    out[0] = CONTROLLER_PC_STAT_STAGES;
    out[1] = gBattlersCount;
    for (enum BattlerId b = 0; b < MAX_BATTLERS_COUNT; b++)
    {
        for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
        {
            s32 stage = DEFAULT_STAT_STAGE;

            if (b < gBattlersCount && gBattleMons[b].species != SPECIES_NONE)
                stage = gBattleMons[b].statStages[i];
            if (stage < MIN_STAT_STAGE || stage > MAX_STAT_STAGE)
                stage = DEFAULT_STAT_STAGE;
            out[4 + b * NUM_BATTLE_STATS + i] = stage;
            if (STAGE_CHANGE(stage) != sStatStagesSent[b][i] && changed == MAX_BATTLERS_COUNT)
                changed = b;
        }
    }
    return changed;
}

bool32 StatStages_CommandValid(const u8 *p, u32 battlerCount)
{
    if (p[0] != CONTROLLER_PC_STAT_STAGES || p[1] == 0 || p[1] > MAX_BATTLERS_COUNT || p[1] != battlerCount
     || p[2] != 0 || p[3] != 0)
        return FALSE;
    for (u32 i = 4; i < PC_STAT_STAGES_RAW_SIZE; i++)
    {
        if (p[i] > MAX_STAT_STAGE)
            return FALSE;
    }
    return TRUE;
}

// BattleMainCB1 on the link master, before its engine's step: TRUE while the engine waits this frame. The stages go out
// only when the engine itself waits for nothing (no controller busy), so the command is like one more the engine sent;
// the engine then waits for every game to handle it, as it would for its own, before it goes on. Nothing is sent before
// the battle's Pokemon are in (the intro) or once its outcome is known.
bool32 StatStages_SyncTick(bool32 battleUnderway)
{
    u8 command[PC_STAT_STAGES_RAW_SIZE];
    enum BattlerId target;

    if ((gBattleTypeFlags & (BATTLE_TYPE_LINK | BATTLE_TYPE_IS_MASTER)) != (BATTLE_TYPE_LINK | BATTLE_TYPE_IS_MASTER)
     || gBattleResources == NULL)
        return FALSE;
    if (sStatStagesInFlight)
    {
        if (gBattleControllerExecFlags)
            return TRUE;
        sStatStagesInFlight = FALSE;
    }
    if (!battleUnderway || gBattleOutcome != 0 || gBattleControllerExecFlags || gBattlersCount == 0)
        return FALSE;
    target = StatStages_BuildCommand(command);
    if (target >= gBattlersCount)
        return FALSE;
    for (enum BattlerId b = 0; b < MAX_BATTLERS_COUNT; b++)
    {
        for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
            sStatStagesSent[b][i] = STAGE_CHANGE(command[4 + b * NUM_BATTLE_STATS + i]);
    }
    PrepareBufferDataTransferLink(target, B_COMM_TO_CONTROLLER, PC_STAT_STAGES_RAW_SIZE, command);
    MarkBattlerForControllerExec(target);
    sStatStagesInFlight = TRUE;
    return TRUE;
}

// BattleMainCB1, before a battler's controller runs: the stages command, on every game of a battle between players (the
// master's own included). The game's controllers know nothing of it (their tables end before it), so it is handled here
// and the controller completes as for any other command.
bool32 StatStages_RunCommand(enum BattlerId battler)
{
    const u8 *p;

    if (gBattleResources == NULL || battler >= MAX_BATTLERS_COUNT || !IsBattleControllerActiveOnLocal(battler))
        return FALSE;
    p = gBattleResources->bufferA[battler];
    if (p[0] != CONTROLLER_PC_STAT_STAGES)
        return FALSE;
    if (StatStages_CommandValid(p, gBattlersCount))
    {
        for (enum BattlerId b = 0; b < MAX_BATTLERS_COUNT; b++)
        {
            for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
                sStatStagesShown[b][i] = STAGE_CHANGE(p[4 + b * NUM_BATTLE_STATS + i]);
        }
    }
    BtlController_Complete(battler);
    return TRUE;
}

/* ---------------------------------------------------------------- what the screens show */

void StatStages_Shown(enum BattlerId battler, s8 out[NUM_BATTLE_STATS])
{
    memset(out, 0, NUM_BATTLE_STATS);
    if (battler >= MAX_BATTLERS_COUNT || battler >= gBattlersCount || (gBattleTypeFlags & BATTLE_TYPE_SAFARI))
        return;
    if (gBattleTypeFlags & BATTLE_TYPE_LINK)
    {
        memcpy(out, sStatStagesShown[battler], NUM_BATTLE_STATS);
        return;
    }
    if (gBattleMons[battler].species == SPECIES_NONE)
        return;
    for (u32 i = 0; i < NUM_BATTLE_STATS; i++)
    {
        s32 stage = gBattleMons[battler].statStages[i];

        if (stage >= MIN_STAT_STAGE && stage <= MAX_STAT_STAGE)
            out[i] = STAGE_CHANGE(stage);
    }
}

// Showdown's order; the labels the tags print
static const u8 sTagOrder[] = {STAT_ATK, STAT_DEF, STAT_SPATK, STAT_SPDEF, STAT_SPEED, STAT_ACC, STAT_EVASION};
static const char sTagLabels[NUM_BATTLE_STATS][4] = {
    [STAT_HP] = "HP", [STAT_ATK] = "ATK", [STAT_DEF] = "DEF", [STAT_SPEED] = "SPE", [STAT_SPATK] = "SPA",
    [STAT_SPDEF] = "SPD", [STAT_ACC] = "ACC", [STAT_EVASION] = "EVA",
};

// The tags a battler shows now: at most NUM_BATTLE_STATS - 1 (HP has none).
u32 StatStages_Tags(enum BattlerId battler, struct StatStageTag *tags)
{
    s8 shown[NUM_BATTLE_STATS];
    u32 count = 0;

    StatStages_Shown(battler, shown);
    for (u32 i = 0; i < ARRAY_COUNT(sTagOrder); i++)
    {
        u32 stat = sTagOrder[i], n = 0;
        s32 stage = shown[stat];

        if (stage == 0)
            continue;
        tags[count].stat = stat;
        tags[count].stage = stage;
        for (const char *c = sTagLabels[stat]; *c != '\0'; c++)
            tags[count].text[n++] = *c;
        tags[count].text[n++] = stage > 0 ? '+' : '-';
        tags[count].text[n++] = '0' + (stage > 0 ? stage : -stage);
        tags[count].text[n] = '\0';
        count++;
    }
    return count;
}

/* ---------------------------------------------------------------- Classic 2D: the tags in the GBA picture */

// The 3D UI's Micro 3x5 capitals (native_ui/data/ui_caps.c), with the letters and digits the tags need; bit x of a row
// lights column x.
static const struct { char ch; u8 rows[5]; } sMicro[] = {
    {'A', {2, 5, 7, 5, 5}}, {'C', {6, 1, 1, 1, 6}}, {'D', {3, 5, 5, 5, 3}}, {'E', {7, 1, 3, 1, 7}},
    {'F', {7, 1, 3, 1, 1}}, {'K', {5, 5, 3, 5, 5}}, {'P', {3, 5, 3, 1, 1}}, {'S', {6, 1, 2, 4, 3}},
    {'T', {7, 2, 2, 2, 2}}, {'V', {5, 5, 5, 5, 2}}, {'+', {0, 2, 7, 2, 0}}, {'-', {0, 0, 7, 0, 0}},
    {'1', {2, 3, 2, 2, 7}}, {'2', {3, 4, 2, 1, 7}}, {'3', {3, 4, 2, 4, 3}}, {'4', {5, 5, 7, 4, 4}},
    {'5', {7, 1, 3, 4, 3}}, {'6', {6, 1, 7, 5, 7}}, {'H', {5, 5, 7, 5, 5}},
};

#define RGB15(r, g, b) ((u16)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10)))
#define TAG_INK    RGB15(16, 16, 32)
#define TAG_TEXT   RGB15(248, 248, 248)
#define TAG_BOOST  RGB15(40, 104, 200)
#define TAG_DROP   RGB15(200, 56, 48)
#define TAG_HEIGHT 9        // rim, a pixel, the 5 rows of text, a pixel, rim
#define TAG_GAP    1
#define SCENE_BOTTOM 112    // the message box and the menus start here: never drawn over

static bool32 Overlaps(const struct PcRect *a, const struct PcRect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static u32 TagWidth(const char *text)
{
    return strlen(text) * 4 - 1 + 4;
}

static void Plot(u16 *pixels, s32 x, s32 y, u16 colour)
{
    if (x >= 0 && x < DISPLAY_WIDTH && y >= 0 && y < SCENE_BOTTOM)
        pixels[y * DISPLAY_WIDTH + x] = colour;
}

static void DrawTag(u16 *pixels, s32 x, s32 y, const struct StatStageTag *tag)
{
    s32 w = TagWidth(tag->text);
    u16 fill = tag->stage > 0 ? TAG_BOOST : TAG_DROP;

    for (s32 dy = 0; dy < TAG_HEIGHT; dy++)
    {
        for (s32 dx = 0; dx < w; dx++)
        {
            bool32 edgeX = dx == 0 || dx == w - 1, edgeY = dy == 0 || dy == TAG_HEIGHT - 1;

            if (edgeX && edgeY)
                continue; // round corners
            Plot(pixels, x + dx, y + dy, edgeX || edgeY ? TAG_INK : fill);
        }
    }
    x += 2;
    for (const char *c = tag->text; *c != '\0'; c++, x += 4)
    {
        for (u32 g = 0; g < ARRAY_COUNT(sMicro); g++)
        {
            if (sMicro[g].ch != *c)
                continue;
            for (s32 row = 0; row < 5; row++)
                for (s32 col = 0; col < 3; col++)
                    if (sMicro[g].rows[row] & (1 << col))
                        Plot(pixels, x + col, y + 2 + row, TAG_TEXT);
            break;
        }
    }
}

// The healthbox's colours are being faded (the battle's fade in or out, a menu's): the tags wait.
static bool32 HealthboxFaded(const struct PcHealthbox *box)
{
    u32 base = OBJ_PLTT_ID(box->paletteNum);

    return memcmp(&gPlttBufferFaded[base], &gPlttBufferUnfaded[base], PLTT_SIZE_4BPP) != 0;
}

// The tags of one healthbox: on the side towards the middle of the screen. A box on the right (the player's) has them
// on its left, right-aligned; a box on the left (the opponent's) under it when nothing is below (singles, the lower box
// of a double battle), else on the right of its tail. Rows of two (three once more than six), four under a box.
static void DrawTagsOf(u16 *pixels, enum BattlerId battler, const struct PcHealthbox *box, const struct PcRect *keep,
                       u32 keepCount)
{
    struct StatStageTag tags[NUM_BATTLE_STATS];
    u32 count = StatStages_Tags(battler, tags), perRow, row = 0;
    bool32 right = box->plateX >= DISPLAY_WIDTH / 2, under = FALSE;
    s32 x0, y;
    struct PcHealthbox other;

    if (count == 0)
        return;
    if (!right)
    {
        under = TRUE;
        for (enum BattlerId b = 0; b < gBattlersCount; b++)
        {
            if (b != battler && Battle_PcHealthbox(b, &other) && other.plateX < DISPLAY_WIDTH / 2
             && other.plateY > box->plateY && other.plateY < box->plateY + box->plateH + TAG_HEIGHT * 2)
                under = FALSE;
        }
    }
    perRow = under ? 4 : (count > 6 ? 3 : 2);
    y = under ? box->plateY + box->plateH + 1 : box->plateY + 1;
    x0 = right ? box->plateX - 2 : under ? box->plateX + 2 : box->plateX + box->plateW + 2;
    for (u32 first = 0; first < count; first += perRow, row++)
    {
        u32 last = first + perRow < count ? first + perRow : count;
        s32 rowWidth = 0, x;

        for (u32 i = first; i < last; i++)
            rowWidth += TagWidth(tags[i].text) + (i > first ? TAG_GAP : 0);
        x = right ? x0 - rowWidth : x0;
        for (u32 i = first; i < last; i++)
        {
            struct PcRect r = {x, y, TagWidth(tags[i].text), TAG_HEIGHT};
            bool32 covered = r.y + r.h > SCENE_BOTTOM || r.x < 0 || r.x + r.w > DISPLAY_WIDTH;

            for (u32 k = 0; k < keepCount && !covered; k++)
                covered = Overlaps(&r, &keep[k]);
            if (!covered)
                DrawTag(pixels, x, y, &tags[i]);
            x += r.w + TAG_GAP;
        }
        y += TAG_HEIGHT + TAG_GAP;
    }
}

// platform/sdl2.c VDraw, the GBA picture just drawn (the game parked at the frame's end).
void StatStages_DrawGba(u16 *pixels)
{
    struct PcRect keep[MAX_BATTLERS_COUNT + 12];
    u32 keepCount;
    struct PcHealthbox box;

    if (!gMain.inBattle || gMain.callback2 != BattleMainCB2 || gBattleStruct == NULL || gBattleResources == NULL
     || gBattleSpritesDataPtr == NULL || !(REG_DISPCNT & DISPCNT_OBJ_ON))
        return;
#ifdef ENABLE_NATIVE_UI
    if (NativeUi_ReplacesBattleTextBox())
        return; // the 3D battle HUD draws its own beside its databoxes
#endif
    keepCount = Battle_PcProtectedRects(keep, ARRAY_COUNT(keep));
    for (enum BattlerId b = 0; b < gBattlersCount; b++)
    {
        if (Battle_PcHealthbox(b, &box) && !HealthboxFaded(&box))
            DrawTagsOf(pixels, b, &box, keep, keepCount);
    }
}

#endif // PORTABLE
