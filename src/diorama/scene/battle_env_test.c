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
#include "battle_setup.h"
#include "data.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "constants/battle.h"
#include "constants/items.h"
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
} s;

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
    if (s.spec[0] != '\0' || s.stem[0] != '\0')
        SDL_Log("Battle env test: battle '%s' after %d field frames, capture '%s'%s",
                s.spec, s.delay, s.stem, s.exitAfter ? ", then exit" : "");
}

static void StartTrainer(int a, int b, bool doubles)
{
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
    else
        SDL_Log("Battle env test: unknown battle kind '%s'", kind);
}

void DioramaBattleEnvTest_FieldTick(void)
{
    if (!s.init)
        Init();
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
    if (s.stem[0] != '\0')
        SDL_AtomicSet(&s.menuSeen, 1);
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
    if (s.exitAfter)
    {
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
}

#endif
