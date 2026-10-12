#if defined(ENABLE_DIORAMA) && defined(DIORAMA_BATTLE_ENV_TEST)

/*
 * The battle-environment test driver (include/diorama/battle_env_test.h).
 * TEST BUILDS ONLY. It starts one battle when the field is ready, as the
 * debug menu's "Try battle" does, and asks the 3D battle for a picture once
 * the action menu is up.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle.h"
#include "battle_partner.h"
#include "battle_setup.h"
#include "data.h"
#include "event_object_movement.h"
#include "load_save.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "constants/battle.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "diorama/battle_env_test.h"

static struct
{
    bool init;
    bool started;
    int fieldFrames;
    int delay;
    int captureDelay;
    bool exitAfter;
    char spec[96];
    char stem[512];
    SDL_atomic_t menuSeen;
    SDL_atomic_t menuFrames;
    SDL_atomic_t captured;
    /* PKMN_TEST_AUTOPILOT=1: the keys a person would press (FilterKeys) */
    bool autopilot;
    SDL_atomic_t fieldSeen;
    SDL_atomic_t battleSeen;
    unsigned int startMs;
    int timeoutSeconds;       /* PKMN_TEST_TIMEOUT: give up (and exit) after this long */
    /* the autopilot's press in progress: held for some frames, then released for as many */
    unsigned short pressButton;
    int pressLeft, gapLeft;
    bool startPressed, fieldPhase;
    unsigned int nextMs;
    /* PKMN_TEST_FIGHT=<seconds>: after the picture, A through the battle to its end */
    int fightSeconds;
    unsigned int fightStartMs;
    bool fightOver;
    /* PKMN_TEST_STRONG=1: the party at level 100 and healed, once, when the field is up */
    bool strong, strengthened;
} s;

/* Frames a press is held, and released after it: a game running at a frame or two a second
 * (4K on software OpenGL) misses no press, as it would one timed in milliseconds. */
#define AUTOPILOT_HOLD 3

static int EnvInt(const char *name, int fallback)
{
    const char *value = SDL_getenv(name);
    return value != NULL && value[0] != '\0' ? atoi(value) : fallback;
}

static void Init(void)
{
    const char *value;

    s.init = true;
    value = SDL_getenv("PKMN_TEST_BATTLE");
    if (value != NULL)
        SDL_strlcpy(s.spec, value, sizeof(s.spec));
    value = SDL_getenv("PKMN_TEST_CAPTURE");
    if (value != NULL)
        SDL_strlcpy(s.stem, value, sizeof(s.stem));
    s.delay = EnvInt("PKMN_TEST_BATTLE_DELAY", 150);
    s.captureDelay = EnvInt("PKMN_TEST_CAPTURE_DELAY", 40);
    s.exitAfter = EnvInt("PKMN_TEST_EXIT", 0) != 0;
    s.autopilot = EnvInt("PKMN_TEST_AUTOPILOT", 0) != 0;
    s.timeoutSeconds = EnvInt("PKMN_TEST_TIMEOUT", 0);
    s.fightSeconds = EnvInt("PKMN_TEST_FIGHT", 0);
    s.strong = EnvInt("PKMN_TEST_STRONG", 0) != 0;
    if (s.spec[0] != '\0' || s.stem[0] != '\0')
        SDL_Log("Battle env test: battle '%s' after %d field frames, capture '%s'%s",
                s.spec, s.delay, s.stem, s.exitAfter ? ", then exit" : "");
}

static void StartTrainer(int a, int b, bool doubles)
{
    /* As a trainer's script does (lockall): the field stands still through the transition,
     * so a visible wild Pokemon neither walks into the player nor, spawning, empties the
     * party just made (KNOWN_ISSUES N33). The wild battles below lock it themselves. */
    LockPlayerFieldControls();
    FreezeObjectEvents();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER;
    TRAINER_BATTLE_PARAM.opponentA = a;
    TRAINER_BATTLE_PARAM.opponentB = 0xFFFF;
    CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_A], GetTrainerStructFromId(a));
    if (doubles || b > 0)
        gBattleTypeFlags |= BATTLE_TYPE_DOUBLE;
    if (b > 0)
    {
        TRAINER_BATTLE_PARAM.opponentB = b;
        CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_B], GetTrainerStructFromId(b));
        gBattleTypeFlags |= BATTLE_TYPE_TWO_OPPONENTS;
    }
    gBattleEnvironment = BattleSetup_GetEnvironmentId();
    CalculateEnemyPartyCount();
    BattleSetup_StartTrainerBattle_Debug();
}

/* A Multi battle with an in-game partner, as the debug menu's "Try battle" with a
 * partner sets one up (and a 2-vs-2 script such as Mossdeep's Space Center does):
 * the player's three, the partner's party, one or two opponent trainers. */
static void StartPartnerBattle(int a, int b, int partner)
{
    LockPlayerFieldControls();
    FreezeObjectEvents();
    gBattleTypeFlags = BATTLE_TYPE_TRAINER | BATTLE_TYPE_DOUBLE;
    TRAINER_BATTLE_PARAM.opponentA = a;
    TRAINER_BATTLE_PARAM.opponentB = 0xFFFF;
    CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_A], GetTrainerStructFromId(a));
    if (b > 0)
    {
        TRAINER_BATTLE_PARAM.opponentB = b;
        CreateNPCTrainerPartyFromTrainer(gParties[B_TRAINER_OPPONENT_B], GetTrainerStructFromId(b));
        gBattleTypeFlags |= BATTLE_TYPE_TWO_OPPONENTS;
    }
    SavePlayerParty();
    gPartnerTrainerId = TRAINER_PARTNER(partner);
    gBattleTypeFlags |= BATTLE_TYPE_MULTI | BATTLE_TYPE_INGAME_PARTNER;
    for (u32 i = 0; i < MAX_FRONTIER_PARTY_SIZE; i++)
    {
        gSelectedOrderFromParty[i] = i + 1;
        gSaveBlock2Ptr->frontier.selectedPartyMons[i] = gSelectedOrderFromParty[i];
    }
    FillPartnerParty(gPartnerTrainerId);
    gBattleEnvironment = BattleSetup_GetEnvironmentId();
    CalculateEnemyPartyCount();
    BattleSetup_StartTrainerBattle_Debug();
}

static void StartBattle(void)
{
    char kind[16] = {0};
    int a = 0, b = 0, c = 0, d = 0;

    if (sscanf(s.spec, "%15[a-z0-9]:%d:%d:%d:%d", kind, &a, &b, &c, &d) < 2)
    {
        SDL_Log("Battle env test: cannot read PKMN_TEST_BATTLE '%s'", s.spec);
        return;
    }
    SDL_Log("Battle env test: starting %s %d %d %d %d on map %d.%d at %d,%d", kind, a, b, c, d,
            gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
            gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    if (strcmp(kind, "wild") == 0)
    {
        CreateScriptedWildMon(a, b > 0 ? b : 30, ITEM_NONE);
        BattleSetup_StartScriptedWildBattle();
    }
    else if (strcmp(kind, "wild2") == 0)
    {
        CreateScriptedDoubleWildMon(a, b > 0 ? b : 30, ITEM_NONE, c, d > 0 ? d : 30, ITEM_NONE);
        BattleSetup_StartScriptedDoubleWildBattle();
    }
    else if (strcmp(kind, "legendary") == 0)
    {
        CreateScriptedWildMon(a, b > 0 ? b : 50, ITEM_NONE);
        BattleSetup_StartLegendaryBattle();
    }
    else if (strcmp(kind, "trainer") == 0)
        StartTrainer(a, 0, false);
    else if (strcmp(kind, "double") == 0)
        StartTrainer(a, 0, true);
    else if (strcmp(kind, "two") == 0)
        StartTrainer(a, b, true);
    else if (strcmp(kind, "partner") == 0)
        StartPartnerBattle(a, b, c);
    else
        SDL_Log("Battle env test: unknown battle kind '%s'", kind);
}

/* The test save's party at level 100, healed (PKMN_TEST_STRONG): a battle fought through
 * with A (PKMN_TEST_FIGHT) is won and so runs through every opponent's Pokemon. A presses
 * the first move, so that is one every type takes damage from (Flamethrower: no type is
 * immune to Fire); the test save's own first move was a status move, used until its PP ran
 * out. In memory only: the survey never writes a save. */
static void StrengthenParty(void)
{
    for (u32 i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u32 species = GetMonData(mon, MON_DATA_SPECIES);
        u32 exp;

        if (species == SPECIES_NONE || GetMonData(mon, MON_DATA_IS_EGG))
            continue;
        exp = gExperienceTables[gSpeciesInfo[species].growthRate][MAX_LEVEL];
        SetMonData(mon, MON_DATA_EXP, &exp);
        CalculateMonStats(mon);
        SetMonMoveSlot(mon, MOVE_FLAMETHROWER, 0);
    }
    /* The Set battle style: in a single battle the Shift style asks whether to switch when
     * the opponent sends its next Pokemon, and A's "Yes" opens the party on the battler
     * already out ("already in battle!"), over and over. */
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    HealPlayerParty();
    SDL_Log("Battle env test: the party at level %d, healed", MAX_LEVEL);
}

void DioramaBattleEnvTest_FieldTick(void)
{
    if (!s.init)
        Init();
    if (s.strong && !s.strengthened)
    {
        s.strengthened = true;
        StrengthenParty();
    }
    SDL_AtomicSet(&s.fieldSeen, 1);
    if (gMain.inBattle)
        SDL_AtomicSet(&s.battleSeen, 1);
    /* PKMN_TEST_FIGHT: the field is back after the fought battle - how it ended */
    if (s.fightSeconds > 0 && !s.fightOver && SDL_AtomicGet(&s.menuSeen) != 0 && !gMain.inBattle)
    {
        s.fightOver = true;
        SDL_Log("Battle env test: battle over, outcome %d (%s), back on map %d.%d at %d,%d",
                gBattleOutcome, gBattleOutcome == B_OUTCOME_WON ? "won"
                              : gBattleOutcome == B_OUTCOME_LOST ? "lost" : "other",
                gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
        if (s.exitAfter)
        {
            fflush(stdout);
            fflush(stderr);
            _exit(gBattleOutcome == B_OUTCOME_WON ? 0 : 4);
        }
    }
    if (s.spec[0] == '\0' || s.started)
        return;
    if (ArePlayerFieldControlsLocked() || gPaletteFade.active || ScriptContext_IsEnabled())
    {
        s.fieldFrames = 0;
        return;
    }
    if (++s.fieldFrames < s.delay)
        return;
    s.started = true;
    StartBattle();
}

void DioramaBattleEnvTest_OnActionMenu(void)
{
    if (!s.init)
        Init();
    if (s.stem[0] != '\0' || s.fightSeconds > 0)
        SDL_AtomicSet(&s.menuSeen, 1);
}

/*
 * The keys a person would press, for a run with nobody at the keyboard (the owner's
 * graphics-card measurements): Start past the intro, then A every three seconds (the title
 * screen, Continue) until the field is up; then B every second and a half - a scripted scene
 * on entering the map, the battle's opening lines - until the action menu is up; nothing
 * after that. As --benchmark's (platform/benchmark.c) and the survey's (survey.py) presses.
 */
unsigned short DioramaBattleEnvTest_FilterKeys(unsigned short keys, unsigned int nowMs)
{
    unsigned int t;

    if (!s.init)
        Init();
    if (s.startMs == 0)
        s.startMs = nowMs == 0 ? 1 : nowMs;
    t = nowMs - s.startMs;
    /* no picture in time (a battle drawn in 2D has none to take): say so and stop */
    if (s.timeoutSeconds > 0 && t > (unsigned int)s.timeoutSeconds * 1000u
     && SDL_AtomicGet(&s.captured) == 0)
    {
        SDL_Log("Battle env test: no picture after %d seconds (the battle %s); giving up",
                s.timeoutSeconds, SDL_AtomicGet(&s.menuSeen) ? "reached its menu but was not "
                "drawn in 3D" : "did not reach its menu");
        if (s.exitAfter)
        {
            fflush(stdout);
            fflush(stderr);
            _exit(3);
        }
        s.timeoutSeconds = 0;
    }
    /* PKMN_TEST_FIGHT: once the picture is taken (or none was asked for), A held for
     * AUTOPILOT_HOLD frames and released for twice as long, until the field is back: the
     * first move on the first target, every line, every question's first answer */
    if (s.fightSeconds > 0 && SDL_AtomicGet(&s.menuSeen) != 0 && !s.fightOver
     && (s.stem[0] == '\0' || SDL_AtomicGet(&s.captured) != 0))
    {
        if (s.fightStartMs == 0)
            s.fightStartMs = nowMs == 0 ? 1 : nowMs;
        if (nowMs - s.fightStartMs > (unsigned int)s.fightSeconds * 1000u)
        {
            SDL_Log("Battle env test: the battle did not end in %d seconds; giving up", s.fightSeconds);
            s.fightOver = true;
            if (s.exitAfter)
            {
                fflush(stdout);
                fflush(stderr);
                _exit(5);
            }
            return keys;
        }
        if (s.pressLeft > 0)
        {
            s.pressLeft--;
            return keys | A_BUTTON;
        }
        if (s.gapLeft > 0)
        {
            s.gapLeft--;
            return keys;
        }
        s.pressLeft = AUTOPILOT_HOLD - 1;
        s.gapLeft = AUTOPILOT_HOLD * 2;
        return keys | A_BUTTON;
    }
    if (!s.autopilot || SDL_AtomicGet(&s.menuSeen) != 0)
        return keys;
    if (s.pressLeft > 0)
    {
        s.pressLeft--;
        return keys | s.pressButton;
    }
    if (s.gapLeft > 0)
    {
        s.gapLeft--;
        return keys;
    }
    /* Start at 8 s (the title), then A every 3 s from 12 s (the title, Continue, a
     * scene's text) until the field is up; then B every 1.5 s (the battle's opening text)
     * until the action menu. */
    s.pressButton = 0;
    if (SDL_AtomicGet(&s.fieldSeen) == 0)
    {
        if (!s.startPressed && t >= 8000)
        {
            s.startPressed = true;
            s.pressButton = START_BUTTON;
            s.nextMs = 12000;
        }
        else if (s.startPressed && t >= s.nextMs)
        {
            s.pressButton = A_BUTTON;
            s.nextMs = t + 3000;
        }
    }
    else
    {
        if (!s.fieldPhase)
        {
            s.fieldPhase = true;
            s.nextMs = t + 1500;
        }
        if (t >= s.nextMs)
        {
            s.pressButton = B_BUTTON;
            s.nextMs = t + 1500;
        }
    }
    if (s.pressButton == 0)
        return keys;
    s.pressLeft = AUTOPILOT_HOLD - 1;
    s.gapLeft = AUTOPILOT_HOLD;
    return keys | s.pressButton;
}

const char *DioramaBattleEnvTest_CaptureStem(void)
{
    if (SDL_AtomicGet(&s.menuSeen) == 0 || SDL_AtomicGet(&s.captured) != 0 || s.stem[0] == '\0')
        return NULL;
    if (SDL_AtomicAdd(&s.menuFrames, 1) + 1 < s.captureDelay)
        return NULL;
    return s.stem;
}

void DioramaBattleEnvTest_CaptureWritten(void)
{
    SDL_AtomicSet(&s.captured, 1);
    SDL_Log("Battle env test: captured %s", s.stem);
    if (s.exitAfter && s.fightSeconds <= 0)
    {
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
}

#endif
