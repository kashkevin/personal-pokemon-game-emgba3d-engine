#if defined(DIORAMA_BATTLE_ENV_TEST)

/*
 * The battle-environment test driver (include/diorama/battle_env_test.h).
 * TEST BUILDS ONLY. It starts one battle when the field is ready, as the
 * debug menu's "Try battle" does, and asks the 3D battle for a picture once
 * the action menu is up (in the Classic 2D test build: the GBA frame).
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle.h"
#include "battle_controllers.h"
#include "battle_gimmick.h"
#include "battle_partner.h"
#include "battle_setup.h"
#include "data.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_screen_effect.h"
#include "load_save.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "safari_zone.h"
#include "save.h"
#include "script.h"
#include "script_pokemon_util.h"
#include "constants/battle.h"
#include "constants/battle_anim.h"
#include "constants/characters.h"
#include "constants/flags.h"
#include "constants/form_change_types.h"
#include "constants/items.h"
#include "battle_gfx_sfx_util.h"
#include "item.h"
#include "constants/moves.h"
#include "constants/opponents.h"
#include "coins.h"
#include "money.h"
#include "constants/maps.h"
#include "constants/multiplayer_balance.h"
#include "constants/multiplayer_rival.h"
#include "platform/coin_shop.h"
#include "diorama/battle_env_test.h"
#ifndef ENABLE_DIORAMA
#include "platform/png_write.h"
#endif

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
    /* The sprite tests' additions (battle sprites, platform/battle_sprites.h): the party, the
     * foes' look, bag items, keys pressed at the action menu, a series of captures. */
    char party[256], foe[128], bag[128], keys[256];
    int keyFrames;            /* frames since the action menu was first up (FilterKeys) */
    int keysLast;             /* the last PKMN_TEST_KEYS frame: PKMN_TEST_FIGHT takes over after it */
    int seriesEvery, seriesCount, seriesDone;
    bool seriesFromBattle;
    char seriesStem[600];
    SDL_atomic_t renderFrames; /* frames the 3D battle drew (CaptureStem) */
    int menuRenderFrame;      /* renderFrames when the action menu was first seen there; -1 not yet */
    /* PKMN_TEST_PARADE: every species from first to last shown in turn by both sides */
    int paradeNext, paradeLast, paradeFrames, paradeWait;
    char paradeGender;        /* 'f' or 'm': a personality of that gender for each species */
    bool paradeShiny, paradeDone;
    /* PKMN_TEST_FIGHT=<seconds>: after the picture, A through the battle to its end */
    int fightSeconds;
    unsigned int fightStartMs;
    bool fightOver;
    /* PKMN_TEST_STRONG=1: the party at level 100 and healed, once, when the field is up */
    bool strong, strengthened;
    /* PKMN_TEST_WARP=GROUP:NUM:WARP[:X:Y]: first warp there, as the debug menu's "Warp to map
     * warp" does (a map the game does not resume on: a secret base comes back in Littleroot
     * Town), then the battle. In memory only: the survey never writes a save. */
    int warpGroup, warpNum, warpId, warpX, warpY;
    bool warp, warped;
    /* PKMN_TEST_SHINY=1: the wild Pokemon is shiny; PKMN_TEST_MEGA=1: the opponent trainer's first
     * Pokemon that can Mega Evolve holds its Mega Stone (both in memory only, as the battle starts) */
    bool shiny, mega;
    /* PKMN_TEST_BATTLE=save:1: field frames left before the process ends (the save file is written at a
     * frame boundary, Platform_FlushSaveIfDirty) */
    int saveExitFrames;
    /* PKMN_TEST_NAME=<letters>: the player's name as the battle starts (in the save with save:1), A-Z a-z 0-9 */
    char name[16];
    /* PKMN_TEST_FREEZE_FIELD=1: the field's objects stand still while the battle waits to start */
    bool freezeField;
    /* PKMN_TEST_FIRST_MOVE=<move number>: with PKMN_TEST_STRONG, the party's first move in place of
     * Flamethrower (a move whose animation follows a counter over turns: Fury Cutter, Rollout) */
    int firstMove;
    /* PKMN_TEST_LEAD_ONLY=1: the party is its first Pokemon alone (in memory only), so a battle lost ends at
     * its faint (the autopilot cannot choose the next one in the party menu) */
    bool leadOnly, trimmed;
    /* PKMN_TEST_MEGA_LOG=1: the Mega Evolution lines (MegaLogFrame); what was last logged of each battler */
    bool megaLog, megaInBattle;
    struct { int species, type1, type2, ability, gimmick, usable, trigger; } megaSeen[MAX_BATTLERS_COUNT];
    /* PKMN_TEST_MEGA_SHOT=<frames> with PKMN_TEST_MEGA_SHOT_STEM: a picture that many battle frames after the lead's
     * Mega Evolution animation ended (state: 0 none yet, 1 asked for, 2 handed to the renderer, 3 written; phase: 1
     * marked, 2 its animation playing, waited for at most MEGA_SHOT_WAIT frames) */
    int megaShotFrames, megaShotCountdown, megaShotIndex, megaShotPhase, megaShotWait;
    SDL_atomic_t megaShotState;
    char megaShotStem[512], megaShotPath[600];
    const char *megaShotWhat;   /* what the picture shows, for its log line */
    /* PKMN_TEST_MEGA_SWEEP=first:last[:level[:s]]: a battle per Mega Stone holder (SweepNext), shiny with s */
    int sweepNext, sweepLast, sweepLevel, sweepIndex;
    bool sweep, sweepStarted, sweepShiny, bagDone;
    /* PKMN_TEST_PARADE_MEGAS=1: the parade shows the Mega forms of the holder list, in its order */
    bool paradeMegas;
    int paradeHolder;
    /* PKMN_TEST_BATTLE=field:<frames> with PKMN_TEST_FIELD_KEYS: keys on the field instead of a battle, then the end;
     * with PKMN_TEST_MEGA_LOG what was last logged of the lead's item and the bag's Mega Stones */
    char fieldKeys[512];
    int fieldKeyFrames, fieldEnd;
    bool fieldKeysActive;
    /* PKMN_TEST_FIELD_SHOTS=frame,...: pictures <PKMN_TEST_MEGA_SHOT_STEM>-NN (NN 50 on) at those field-key frames
     * (the Classic 2D test build: the GBA frame) */
    int fieldShots[16], fieldShotCount;
    int fieldSeenSpecies, fieldSeenItem, fieldSeenStones, fieldSeenRing;
    /* PKMN_TEST_COINS=<n>: the Game Corner coins as the test starts; with PKMN_TEST_MEGA_LOG the coins and the money
     * logged on the field when they change (a purchase at the Mauville dealer or the Game Corner) */
    int coins;
    s64 fieldSeenCoins, fieldSeenMoney;
    /* PKMN_TEST_FIELD_AFTER=<frames>: after a fought battle (PKMN_TEST_FIGHT), back on the field, PKMN_TEST_FIELD_KEYS
     * for that many frames instead of the end */
    int fieldAfter;
    bool fieldAfterPending;
    /* PKMN_TEST_SOFT_RESET=<n>: the field keys choose the game's own soft reset (START > PLAYER > RESET, YES; the
     * A+B+START+SELECT chord never resets on PC). The restart is seen when gMain.vblankCounter1 goes back (the reset
     * restores the game's memory; this file's is kept, a platform object); then the autopilot through the title and
     * CONTINUE, the field logged, and the field keys again from their start until the game has reset n times; then
     * the end */
    bool softReset, resetSeen;
    int softResets, resetsDone;
    int resetFieldFrames, resetWaitFrames;
    u32 lastVblank;
    /* PKMN_TEST_STARTER_LOG=1: on the field, "starter field: ..." whenever the place, the party, its lead or the
     * starter claim changes (StarterLogField); what was last logged */
    bool starterLog, starterLogged;
    int starterSeen[9];
    u32 starterSeenPersonality;
} s;

/* PKMN_TEST_LOG=<file>: every line the driver logs is also appended to that file (a check on Windows reads it: the
 * game's own log may reach no console there). */
static FILE *sLogFile;

static void TestLog(const char *format, ...)
{
    char line[700];
    va_list args;

    va_start(args, format);
    SDL_vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    SDL_Log("%s", line);
    if (sLogFile != NULL)
    {
        fprintf(sLogFile, "%s\n", line);
        fflush(sLogFile);
    }
}

/* The Mega Stone holders, in the order the form change tables give them (by species, then the table's order): every
 * enabled species that is not itself a battle form, with each FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM change it has
 * (Charizard and Mewtwo twice). The test ROM's sweep (test/battle/form_change/mega_every_form.c) lists the same. */
#define MEGA_SWEEP_MAX 192
static struct { u16 species, item, mega; } sHolders[MEGA_SWEEP_MAX];
static int sHolderCount;

static void ListMegaStoneHolders(void)
{
    sHolderCount = 0;
    for (u32 species = 1; species < NUM_SPECIES; species++)
    {
        const struct SpeciesInfo *info = &gSpeciesInfo[species];
        const struct FormChange *change;

        if (!IsSpeciesEnabled(species) || info->isMegaEvolution || info->isPrimalReversion || info->isUltraBurst
         || info->isGigantamax || info->isTeraForm || info->isTotem)
            continue;
        for (change = GetSpeciesFormChanges(species); change != NULL && change->method != FORM_CHANGE_TERMINATOR; change++)
        {
            if (change->method != FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM || sHolderCount >= MEGA_SWEEP_MAX)
                continue;
            sHolders[sHolderCount].species = species;
            sHolders[sHolderCount].item = change->param1;
            sHolders[sHolderCount].mega = change->targetSpecies;
            sHolderCount++;
        }
    }
}

/* PKMN_TEST_FACTS=1: the numbers a check names things by (items, species, gimmicks), from this build, then the process
 * ends. A check on Windows has no debugger to read them with. */
#define FACT(name) {#name, name}
static const struct { const char *name; int value; } sFacts[] = {
    FACT(ITEM_MEGA_RING), FACT(ITEM_CHARIZARDITE_X), FACT(ITEM_CHARIZARDITE_Y), FACT(ITEM_MEWTWONITE_X),
    FACT(ITEM_MEWTWONITE_Y), FACT(ITEM_VENUSAURITE), FACT(ITEM_BLASTOISINITE), FACT(ITEM_BLAZIKENITE),
    FACT(ITEM_ZYGARDITE), FACT(ITEM_CHIMECHITE), FACT(ITEM_LUCARIONITE_Z),
    FACT(SPECIES_CHARIZARD), FACT(SPECIES_MEWTWO), FACT(SPECIES_VENUSAUR), FACT(SPECIES_BLASTOISE),
    FACT(SPECIES_BLAZIKEN), FACT(SPECIES_PIKACHU), FACT(SPECIES_POOCHYENA), FACT(SPECIES_ZYGARDE_COMPLETE),
    FACT(SPECIES_CHARIZARD_MEGA_X), FACT(SPECIES_CHARIZARD_MEGA_Y), FACT(SPECIES_MEWTWO_MEGA_X),
    FACT(SPECIES_MEWTWO_MEGA_Y), FACT(SPECIES_VENUSAUR_MEGA), FACT(SPECIES_BLAZIKEN_MEGA), FACT(SPECIES_ZYGARDE_MEGA),
    FACT(MOVE_EARTHQUAKE), FACT(MOVE_FLAMETHROWER), FACT(GIMMICK_NONE), FACT(GIMMICK_MEGA), FACT(NUM_SPECIES),
    FACT(ITEM_COIN_CASE), FACT(MAP_MAUVILLE_CITY_GAME_CORNER), FACT(MAP_MAUVILLE_CITY_MART), FACT(MP_MEGA_RING_COINS),
    FACT(SPECIES_TORCHIC), FACT(MAP_LITTLEROOT_TOWN), FACT(MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB),
    FACT(MP_INTRO_STARTER_PENDING),
};
#undef FACT

static void LogFacts(void)
{
    for (u32 i = 0; i < sizeof(sFacts) / sizeof(sFacts[0]); i++)
        TestLog("Battle env test: fact %s %d", sFacts[i].name, sFacts[i].value);
    /* what the Mauville dealer asks for its first stone, in coins (the coin shop's own price) */
    TestLog("Battle env test: fact PRICE_ITEM_VENUSAURITE %u", CoinShop_StonePrice(ITEM_VENUSAURITE));
    ListMegaStoneHolders();
    for (int i = 0; i < sHolderCount; i++)
        TestLog("Battle env test: fact holder %d species %u item %u mega %u", i, sHolders[i].species, sHolders[i].item,
                sHolders[i].mega);
    TestLog("Battle env test: facts done (%d Mega Stone holders)", sHolderCount);
}

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
    value = SDL_getenv("PKMN_TEST_LOG");
    if (value != NULL && value[0] != '\0')
        sLogFile = fopen(value, "a");
    if (EnvInt("PKMN_TEST_FACTS", 0) != 0)
    {
        LogFacts();
        if (sLogFile != NULL)
            fclose(sLogFile);
        fflush(NULL);
        _exit(0);
    }
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
    value = SDL_getenv("PKMN_TEST_PARTY");
    if (value != NULL)
        SDL_strlcpy(s.party, value, sizeof(s.party));
    value = SDL_getenv("PKMN_TEST_FOE");
    if (value != NULL)
        SDL_strlcpy(s.foe, value, sizeof(s.foe));
    value = SDL_getenv("PKMN_TEST_BAG");
    if (value != NULL)
        SDL_strlcpy(s.bag, value, sizeof(s.bag));
    value = SDL_getenv("PKMN_TEST_NAME");
    if (value != NULL)
        SDL_strlcpy(s.name, value, sizeof(s.name));
    value = SDL_getenv("PKMN_TEST_FIELD_KEYS");
    if (value != NULL)
        SDL_strlcpy(s.fieldKeys, value, sizeof(s.fieldKeys));
    value = SDL_getenv("PKMN_TEST_FIELD_SHOTS");
    for (const char *p = value; p != NULL && *p != '\0' && s.fieldShotCount < 16; p = strchr(p, ','), p = p ? p + 1 : p)
        s.fieldShots[s.fieldShotCount++] = atoi(p);
    s.fieldSeenSpecies = -1;
    s.fieldSeenCoins = s.fieldSeenMoney = -1;
    s.coins = EnvInt("PKMN_TEST_COINS", -1);
    value = SDL_getenv("PKMN_TEST_KEYS");
    if (value != NULL)
    {
        const char *p;
        int at, used;
        char name[16];

        SDL_strlcpy(s.keys, value, sizeof(s.keys));
        p = s.keys;
        while (*p != '\0' && sscanf(p, "%d:%15[A-Z]%n", &at, name, &used) == 2)
        {
            if (at > s.keysLast)
                s.keysLast = at;
            p += used;
            if (*p == ',')
                p++;
        }
    }
    s.seriesEvery = EnvInt("PKMN_TEST_CAPTURE_EVERY", 0);
    s.seriesCount = EnvInt("PKMN_TEST_CAPTURE_COUNT", 0);
    value = SDL_getenv("PKMN_TEST_CAPTURE_FROM");
    s.seriesFromBattle = value != NULL && strcmp(value, "battle") == 0;
    s.menuRenderFrame = -1;
    value = SDL_getenv("PKMN_TEST_PARADE");
    if (value != NULL && sscanf(value, "%d:%d", &s.paradeNext, &s.paradeLast) == 2)
    {
        const char *flags = SDL_getenv("PKMN_TEST_PARADE_FLAGS");

        s.paradeFrames = EnvInt("PKMN_TEST_PARADE_FRAMES", 12);
        s.paradeShiny = flags != NULL && strchr(flags, 's') != NULL;
        s.paradeGender = flags != NULL && strchr(flags, 'f') != NULL ? 'f'
                       : flags != NULL && strchr(flags, 'm') != NULL ? 'm' : 0;
        TestLog("Battle env test: parade of species %d to %d, %d frames each", s.paradeNext, s.paradeLast,
                s.paradeFrames);
    }
    s.fightSeconds = EnvInt("PKMN_TEST_FIGHT", 0);
    s.strong = EnvInt("PKMN_TEST_STRONG", 0) != 0;
    s.shiny = EnvInt("PKMN_TEST_SHINY", 0) != 0;
    s.mega = EnvInt("PKMN_TEST_MEGA", 0) != 0;
    s.freezeField = EnvInt("PKMN_TEST_FREEZE_FIELD", 0) != 0;
    s.leadOnly = EnvInt("PKMN_TEST_LEAD_ONLY", 0) != 0;
    s.firstMove = EnvInt("PKMN_TEST_FIRST_MOVE", 0);
    if (s.firstMove <= MOVE_NONE || s.firstMove >= MOVES_COUNT)
        s.firstMove = MOVE_FLAMETHROWER;
    s.megaLog = EnvInt("PKMN_TEST_MEGA_LOG", 0) != 0;
    s.softResets = EnvInt("PKMN_TEST_SOFT_RESET", 0);
    s.softReset = s.softResets > 0;
    s.starterLog = EnvInt("PKMN_TEST_STARTER_LOG", 0) != 0;
    s.fieldAfter = EnvInt("PKMN_TEST_FIELD_AFTER", 0);
    if (s.megaLog)
        ListMegaStoneHolders();     /* the stones MegaLogField counts */
    s.megaShotFrames = EnvInt("PKMN_TEST_MEGA_SHOT", 0);
    value = SDL_getenv("PKMN_TEST_MEGA_SHOT_STEM");
    if (value != NULL)
        SDL_strlcpy(s.megaShotStem, value, sizeof(s.megaShotStem));
    value = SDL_getenv("PKMN_TEST_MEGA_SWEEP");
    s.sweepLevel = 100;
    if (value != NULL && sscanf(value, "%d:%d:%d", &s.sweepNext, &s.sweepLast, &s.sweepLevel) >= 2)
    {
        const char *flags = strrchr(value, ':');

        s.sweepShiny = flags != NULL && strchr(flags, 's') != NULL;
        ListMegaStoneHolders();
        if (s.sweepLast >= sHolderCount)
            s.sweepLast = sHolderCount - 1;
        s.sweep = s.sweepNext >= 0 && s.sweepNext <= s.sweepLast;
        s.megaLog = true;
        TestLog("Battle env test: mega sweep of holders %d to %d (of %d), level %d%s", s.sweepNext, s.sweepLast,
                sHolderCount, s.sweepLevel, s.sweepShiny ? ", shiny" : "");
    }
    s.paradeMegas = EnvInt("PKMN_TEST_PARADE_MEGAS", 0) != 0;
    if (s.paradeMegas)
    {
        const char *flags = SDL_getenv("PKMN_TEST_PARADE_FLAGS");

        ListMegaStoneHolders();
        s.paradeShiny = flags != NULL && strchr(flags, 's') != NULL;
        if (s.paradeFrames <= 0)
            s.paradeFrames = EnvInt("PKMN_TEST_PARADE_FRAMES", 12);
        TestLog("Battle env test: parade of the %d Mega forms, %d frames each", sHolderCount, s.paradeFrames);
    }
    value = SDL_getenv("PKMN_TEST_WARP");
    s.warpX = s.warpY = -1;
    if (value != NULL && sscanf(value, "%d:%d:%d:%d:%d", &s.warpGroup, &s.warpNum, &s.warpId,
                                &s.warpX, &s.warpY) >= 3)
        s.warp = true;
    if (s.spec[0] != '\0' || s.stem[0] != '\0')
        TestLog("Battle env test: battle '%s' after %d field frames, capture '%s'%s",
                s.spec, s.delay, s.stem, s.exitAfter ? ", then exit" : "");
}

/*
 * A Pokemon for the sprite tests, with what its picture depends on chosen: flags are letters
 * after the level - s shiny, f female, m male, i<item id> held item. The personality comes from a
 * fixed search, not the random number generator. v<move id>: that move in the first slot.
 */
static void CreateTestMon(struct Pokemon *mon, int species, int level, const char *flags)
{
    u32 personality = 0x13579BDF, i;
    int gender = -1, item = 0, move = 0;
    bool shiny = false;

    for (; flags != NULL && *flags != '\0' && *flags != ',' && *flags != ';'; flags++)
    {
        if (*flags == 's')
            shiny = true;
        else if (*flags == 'f')
            gender = MON_FEMALE;
        else if (*flags == 'm')
            gender = MON_MALE;
        else if (*flags == 'i')
            item = atoi(flags + 1);
        else if (*flags == 'v')
            move = atoi(flags + 1);
    }
    for (i = 0; i < 4096 && gender >= 0 && GetGenderFromSpeciesAndPersonality(species, personality) != gender; i++)
        personality += 0x9E3779B1;
    CreateMonWithIVs(mon, species, level > 0 ? level : 30, personality, OTID_STRUCT_PLAYER_ID, MAX_PER_STAT_IVS);
    GiveMonInitialMoveset(mon);
    if (shiny)
    {
        bool32 isShiny = TRUE;

        SetMonData(mon, MON_DATA_IS_SHINY, &isShiny);
    }
    if (item > 0)
    {
        u8 heldItem[2] = {item & 0xFF, item >> 8};

        SetMonData(mon, MON_DATA_HELD_ITEM, heldItem);
    }
    if (move > 0)
        SetMonMoveSlot(mon, move, 0);
    CalculateMonStats(mon);
    TestLog("Battle env test: test Pokemon %d Lv%d%s%s, item %d, first move %d", species, level,
            shiny ? " shiny" : "", gender == MON_FEMALE ? " female" : gender == MON_MALE ? " male" : "", item,
            GetMonData(mon, MON_DATA_MOVE1));
}

/* "species:level[:flags],..." */
static void SetUpTestParty(void)
{
    const char *p = s.party;
    int count = 0;

    if (s.party[0] == '\0')
        return;
    ZeroPlayerPartyMons();
    while (*p != '\0' && count < PARTY_SIZE)
    {
        int species = 0, level = 0, used = 0;
        const char *flags = NULL;

        if (sscanf(p, "%d:%d%n", &species, &level, &used) < 2)
            break;
        p += used;
        if (*p == ':')
            flags = ++p;
        CreateTestMon(&gParties[B_TRAINER_PLAYER][count++], species, level, flags);
        while (*p != '\0' && *p != ',')
            p++;
        if (*p == ',')
            p++;
    }
    CalculatePlayerPartyCount();
    FlagSet(FLAG_SYS_POKEMON_GET);  /* as any player with a Pokemon: the start menu offers POKeMON */
}

/* "item:count,..." */
static void SetUpTestBag(void)
{
    const char *p = s.bag;
    int item, count, used;

    while (*p != '\0' && sscanf(p, "%d:%d%n", &item, &count, &used) == 2)
    {
        AddBagItem(item, count);
        p += used;
        if (*p == ',')
            p++;
    }
}

/* The foes made by wild/wild2/legendary again with PKMN_TEST_FOE's flags ("flags,flags"). */
static void RedoTestFoes(int count)
{
    const char *p = s.foe;
    int i;

    if (s.foe[0] == '\0')
        return;
    for (i = 0; i < count && p != NULL; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_OPPONENT_A][i];
        int species = GetMonData(mon, MON_DATA_SPECIES), level = GetMonData(mon, MON_DATA_LEVEL);

        CreateTestMon(mon, species, level, p);
        p = strchr(p, ',');
        if (p != NULL)
            p++;
    }
}

/* PKMN_TEST_SHINY: the wild Pokemon just made is shiny (its shiny stars play as it appears). */
static void MaybeShiny(void)
{
    bool32 shiny = TRUE;

    if (!s.shiny)
        return;
    SetMonData(&gParties[B_TRAINER_OPPONENT_A][0], MON_DATA_IS_SHINY, &shiny);
    TestLog("Battle env test: the wild Pokemon is shiny");
}

/* PKMN_TEST_MEGA: the first Pokemon of `party` that has a Mega Evolution by item holds that item, so
 * the trainer's AI Mega Evolves it as it would any trainer's (B_ANIM_MEGA_EVOLUTION, fought). */
static void MaybeGiveMegaStone(struct Pokemon *party)
{
    if (!s.mega)
        return;
    for (u32 i = 0; i < PARTY_SIZE; i++)
    {
        u32 species = GetMonData(&party[i], MON_DATA_SPECIES);
        const struct FormChange *change = species != SPECIES_NONE ? GetSpeciesFormChanges(species) : NULL;

        for (; change != NULL && change->method != FORM_CHANGE_TERMINATOR; change++)
        {
            if (change->method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM)
            {
                u32 item = change->param1;
                SetMonData(&party[i], MON_DATA_HELD_ITEM, &item);
                TestLog("Battle env test: the opponent's Pokemon %u (species %u) holds item %u to Mega Evolve",
                        (unsigned)i, (unsigned)species, (unsigned)item);
                return;
            }
        }
    }
    TestLog("Battle env test: no Pokemon of the opponent's can Mega Evolve by an item");
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
    MaybeGiveMegaStone(gParties[B_TRAINER_OPPONENT_A]);
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

void LoadWallyZigzagoon(void); /* field_specials.c (a script special: no header declares it) */

#define MEGA_SHOT_WAIT 1200     /* frames: the animation is waited for no longer (the picture is taken then) */

/* PKMN_TEST_MEGA_SHOT_STEM: a picture <stem>-NN in that many frames (the renderer takes it: CaptureStem), unless one
 * asked for is not taken yet. */
static void RequestShot(int index, int frames, const char *what)
{
    if (s.megaShotStem[0] == '\0' || SDL_AtomicGet(&s.megaShotState) == 1 || SDL_AtomicGet(&s.megaShotState) == 2)
        return;
    SDL_AtomicSet(&s.megaShotState, 0);
    s.megaShotIndex = index;
    s.megaShotWhat = what;
    s.megaShotCountdown = frames > 0 ? frames : 1;
}

static void ShotTick(void)
{
    if (s.megaShotCountdown > 0 && --s.megaShotCountdown == 0)
    {
        SDL_snprintf(s.megaShotPath, sizeof(s.megaShotPath), "%s-%02d", s.megaShotStem, s.megaShotIndex);
        SDL_AtomicSet(&s.megaShotState, 1);
    }
}

/* PKMN_TEST_MEGA_LOG: as the battle starts, the party's lead and the Mega form its held item makes by the species data
 * (the form change tables; mega 0: none), with that form's types and Ability - what the battle must show. */
static void MegaLogLead(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    u32 species = GetMonData(lead, MON_DATA_SPECIES), item = GetMonData(lead, MON_DATA_HELD_ITEM), mega = SPECIES_NONE;
    const struct FormChange *change = IsSpeciesEnabled(species) ? GetSpeciesFormChanges(species) : NULL;

    for (; change != NULL && change->method != FORM_CHANGE_TERMINATOR; change++)
        if (change->method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM && change->param1 == item)
            mega = change->targetSpecies;
    TestLog("Battle env test: mega lead: species %u item %u shiny %d mega %u types %u/%u ability %u", species, item,
            (int)GetMonData(lead, MON_DATA_IS_SHINY), mega, mega ? (unsigned)gSpeciesInfo[mega].types[0] : 0,
            mega ? (unsigned)gSpeciesInfo[mega].types[1] : 0, mega ? (unsigned)gSpeciesInfo[mega].abilities[0] : 0);
}

/* PKMN_TEST_MEGA_LOG: once the lead's Mega animation ended, whether its sprite's palette (what the Mega form is drawn
 * with) is the Mega form's shiny palette or its normal one, by the species data (both 1: the two are the same) */
static void MegaLogPalette(u32 battler)
{
    const u16 *drawn = &gPlttBufferUnfaded[OBJ_PLTT_ID(battler)];
    u32 species = gBattleMons[battler].species, personality = gBattleMons[battler].personality;
    const u16 *shiny = GetMonSpritePalFromSpeciesAndPersonality(species, TRUE, personality);
    const u16 *normal = GetMonSpritePalFromSpeciesAndPersonality(species, FALSE, personality);

    TestLog("Battle env test: mega palette: battler %u species %u shiny %d normal %d", battler, species,
            shiny != NULL && memcmp(drawn, shiny, PLTT_SIZE_4BPP) == 0,
            normal != NULL && memcmp(drawn, normal, PLTT_SIZE_4BPP) == 0);
}

/* PKMN_TEST_MEGA_LOG: in a battle, each frame (the game is between frames), every change of
 *   "mega battler B: species S types T1/T2 ability A gimmick G"  (G its active gimmick: the healthbox's Mega mark)
 *   "mega menu: battler B usable U trigger T"                     (while B's move menu is up; T the trigger is on)
 * and once the battle is over (MegaLogAfter) the party's lead again. */
static void MegaLogFrame(void)
{
    if (!s.megaLog)
        return;
    if (!gMain.inBattle || gBattleStruct == NULL || gBattlersCount < 2)
    {
        s.megaInBattle = false;
        return;
    }
    if (!s.megaInBattle)
    {
        s.megaInBattle = true;
        memset(s.megaSeen, 0xFF, sizeof(s.megaSeen));
    }
    for (u32 b = 0; b < gBattlersCount && b < MAX_BATTLERS_COUNT; b++)
    {
        struct BattlePokemon *mon = &gBattleMons[b];
        int gimmick = GetActiveGimmick(b), usable = gBattleStruct->gimmick.usableGimmick[b];
        bool menu = gBattlerControllerFuncs[b] == HandleInputChooseMove;
        int trigger = menu ? gBattleStruct->gimmick.playerSelect : -1;

        if (mon->species == SPECIES_NONE)
            continue;
        if (mon->species != s.megaSeen[b].species || mon->types[0] != s.megaSeen[b].type1
         || mon->types[1] != s.megaSeen[b].type2 || mon->ability != s.megaSeen[b].ability
         || gimmick != s.megaSeen[b].gimmick)
        {
            s.megaSeen[b].species = mon->species;
            s.megaSeen[b].type1 = mon->types[0];
            s.megaSeen[b].type2 = mon->types[1];
            s.megaSeen[b].ability = mon->ability;
            s.megaSeen[b].gimmick = gimmick;
            TestLog("Battle env test: mega battler %u: species %u types %u/%u ability %u gimmick %d", b, mon->species,
                    (unsigned)mon->types[0], (unsigned)mon->types[1], (unsigned)mon->ability, gimmick);
            if (b == 0 && gimmick == GIMMICK_MEGA && s.megaShotFrames > 0 && SDL_AtomicGet(&s.megaShotState) == 0
             && s.megaShotCountdown == 0 && s.megaShotPhase == 0)
            {
                s.megaShotPhase = 1;
                s.megaShotWait = 0;
            }
        }
        if (b == 0 && s.megaShotPhase > 0 && gBattleSpritesDataPtr != NULL)
        {
            /* the lead's Mega Evolution animation (B_ANIM_MEGA_EVOLUTION, a battle table animation) started and ended */
            bool playing = gBattleSpritesDataPtr->healthBoxesData[0].animFromTableActive;

            if (s.megaShotPhase == 1 && playing)
                s.megaShotPhase = 2;
            if ((s.megaShotPhase == 2 && !playing) || ++s.megaShotWait > MEGA_SHOT_WAIT)
            {
                if (s.megaLog)
                    MegaLogPalette(0);
                s.megaShotPhase = 0;
                RequestShot(s.sweep ? s.sweepIndex : 0, s.megaShotFrames, "the Mega");
                s.megaShotPhase = -1;   /* this battle's picture is asked for */
            }
        }
        if (menu ? usable != s.megaSeen[b].usable || trigger != s.megaSeen[b].trigger : s.megaSeen[b].trigger != -1)
        {
            s.megaSeen[b].usable = usable;
            s.megaSeen[b].trigger = trigger;
            if (menu)
                TestLog("Battle env test: mega menu: battler %u usable %d trigger %d", b, usable, trigger);
        }
    }
}

/* PKMN_TEST_MEGA_LOG off a battle: "mega field: lead species S item I party N stones K ring R" whenever one changes (K the
 * Mega Stones in the bag, every holder's stone counted once) - a stone given, taken, saved and loaded. */
static void MegaLogField(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    int species, item, stones = 0, ring;

    if (!s.megaLog || gMain.inBattle || SDL_AtomicGet(&s.fieldSeen) == 0 || gSaveBlock1Ptr == NULL)
        return;
    if (GetCoins() != s.fieldSeenCoins || GetMoney(&gSaveBlock1Ptr->money) != s.fieldSeenMoney)
    {
        s.fieldSeenCoins = GetCoins();
        s.fieldSeenMoney = GetMoney(&gSaveBlock1Ptr->money);
        TestLog("Battle env test: mega wallet: coins %u money %u", (unsigned)s.fieldSeenCoins, (unsigned)s.fieldSeenMoney);
    }
    species = GetMonData(lead, MON_DATA_SPECIES);
    item = GetMonData(lead, MON_DATA_HELD_ITEM);
    ring = CheckBagHasItem(ITEM_MEGA_RING, 1);
    for (int i = 0; i < sHolderCount; i++)
    {
        bool counted = FALSE;

        for (int j = 0; j < i && !counted; j++)
            counted = sHolders[j].item == sHolders[i].item;
        if (!counted)
            stones += CountTotalItemQuantityInBag(sHolders[i].item);
    }
    if (species == s.fieldSeenSpecies && item == s.fieldSeenItem && stones == s.fieldSeenStones && ring == s.fieldSeenRing)
        return;
    s.fieldSeenSpecies = species;
    s.fieldSeenItem = item;
    s.fieldSeenStones = stones;
    s.fieldSeenRing = ring;
    TestLog("Battle env test: mega field: lead species %d item %d party %d stones %d ring %d", species, item,
            gPartiesCount[B_TRAINER_PLAYER], stones, ring);
}

/* PKMN_TEST_STARTER_LOG off a battle: "starter field: map G.N at X,Y party N lead S personality P shiny H choice C got F"
 * whenever one of them changes - C the starter claim (VAR_MULTIPLAYER_STARTER_CHOICE: 0x62FF open, 0x6100 | n claimed),
 * F FLAG_SYS_POKEMON_GET. A claim in Birch's lab, a save and a load, a soft reset that brings the claim back. */
static void StarterLogField(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    int now[9];
    u32 personality;
    bool changed;

    if (!s.starterLog || gMain.inBattle || SDL_AtomicGet(&s.fieldSeen) == 0 || gSaveBlock1Ptr == NULL)
        return;
    now[0] = gSaveBlock1Ptr->location.mapGroup;
    now[1] = gSaveBlock1Ptr->location.mapNum;
    now[2] = gSaveBlock1Ptr->pos.x;
    now[3] = gSaveBlock1Ptr->pos.y;
    now[4] = gPartiesCount[B_TRAINER_PLAYER];
    now[5] = now[4] > 0 ? (int)GetMonData(lead, MON_DATA_SPECIES) : 0;
    now[6] = now[4] > 0 ? (int)GetMonData(lead, MON_DATA_IS_SHINY) : 0;
    now[7] = VarGet(VAR_MULTIPLAYER_STARTER_CHOICE);
    now[8] = FlagGet(FLAG_SYS_POKEMON_GET);
    personality = now[4] > 0 ? GetMonData(lead, MON_DATA_PERSONALITY) : 0;
    changed = !s.starterLogged || personality != s.starterSeenPersonality;
    for (int i = 0; i < 9 && !changed; i++)
        changed = now[i] != s.starterSeen[i];
    if (!changed)
        return;
    s.starterLogged = true;
    memcpy(s.starterSeen, now, sizeof(now));
    s.starterSeenPersonality = personality;
    TestLog("Battle env test: starter field: map %d.%d at %d,%d party %d lead %d personality %08x shiny %d choice %04x got %d",
            now[0], now[1], now[2], now[3], now[4], now[5], (unsigned)personality, now[6], now[7], now[8]);
}

static void MegaLogAfter(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];

    if (!s.megaLog)
        return;
    TestLog("Battle env test: mega after: lead species %u item %u party %d ring %d", GetMonData(lead, MON_DATA_SPECIES),
            GetMonData(lead, MON_DATA_HELD_ITEM), gPartiesCount[B_TRAINER_PLAYER], (int)CheckBagHasItem(ITEM_MEGA_RING, 1));
}

/* PKMN_TEST_MEGA_SWEEP: the next holder alone in the party, at the sweep's level, holding its stone, Flamethrower first
 * (no type is immune to Fire: the battle is over in a turn), for the next battle; FALSE when the sweep is done. */
static bool SweepPrepare(void)
{
    if (!s.sweep || s.sweepNext > s.sweepLast)
        return FALSE;
    s.sweepIndex = s.sweepNext++;
    SDL_snprintf(s.party, sizeof(s.party), "%u:%d:%si%uv%u", sHolders[s.sweepIndex].species, s.sweepLevel,
                 s.sweepShiny ? "s" : "", sHolders[s.sweepIndex].item, (unsigned)MOVE_FLAMETHROWER);
    TestLog("Battle env test: mega sweep holder %d: species %u item %u mega %u", s.sweepIndex,
            sHolders[s.sweepIndex].species, sHolders[s.sweepIndex].item, sHolders[s.sweepIndex].mega);
    return TRUE;
}

/* After a sweep battle: the driver as it was before the first one, for the next (or the end). */
static void SweepNext(void)
{
    if (!SweepPrepare())
    {
        TestLog("Battle env test: mega sweep done");
        if (s.exitAfter)
        {
            if (sLogFile != NULL)
                fclose(sLogFile);
            fflush(NULL);
            _exit(0);
        }
        s.sweep = false;
        return;
    }
    s.started = false;
    s.fieldFrames = 0;
    s.fightOver = false;
    s.fightStartMs = 0;
    s.keyFrames = 0;
    s.pressLeft = s.gapLeft = 0;
    SDL_AtomicSet(&s.menuSeen, 0);
    SDL_AtomicSet(&s.megaShotState, 0);
    s.megaShotCountdown = 0;
    s.megaShotPhase = 0;
}

/* PKMN_TEST_FIELD_KEYS from now, for that many frames */
static void StartFieldKeys(int frames)
{
    s.fieldEnd = frames;
    s.fieldKeyFrames = 0;
    s.fieldKeysActive = true;
    s.fieldSeenSpecies = -1;    /* the field and the wallet logged again: their state as the keys start */
    s.fieldSeenCoins = s.fieldSeenMoney = -1;
    s.starterLogged = false;
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;     /* the keys come on a timer: fast text */
    TestLog("Battle env test: field keys for %d frames", frames);
}

static void StartBattle(void)
{
    char kind[16] = {0};
    int a = 0, b = 0, c = 0, d = 0;

    if (sscanf(s.spec, "%15[a-z0-9]:%d:%d:%d:%d", kind, &a, &b, &c, &d) < 2)
    {
        TestLog("Battle env test: cannot read PKMN_TEST_BATTLE '%s'", s.spec);
        return;
    }
    TestLog("Battle env test: starting %s %d %d %d %d on map %d.%d at %d,%d", kind, a, b, c, d,
            gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
            gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
    SetUpTestParty();
    if (s.name[0] != '\0')
    {
        u32 i;

        for (i = 0; i < PLAYER_NAME_LENGTH && s.name[i] != '\0'; i++)
        {
            char c = s.name[i];

            gSaveBlock2Ptr->playerName[i] = c >= 'A' && c <= 'Z' ? CHAR_A + (c - 'A') : c >= 'a' && c <= 'z' ? CHAR_a + (c - 'a')
                                          : c >= '0' && c <= '9' ? CHAR_0 + (c - '0') : CHAR_SPACE;
        }
        gSaveBlock2Ptr->playerName[i] = EOS;
        TestLog("Battle env test: the player's name is %s", s.name);
    }
    if (!s.bagDone)
        SetUpTestBag();
    if (s.coins >= 0)
    {
        SetCoins(s.coins);
        TestLog("Battle env test: coins set to %u", GetCoins());
    }
    s.bagDone = s.sweep;    /* a sweep's battles share the bag: its Mega Ring once */
    if (s.megaLog)
        MegaLogLead();
    if (strcmp(kind, "field") == 0)
    {
        /* no battle: PKMN_TEST_FIELD_KEYS from here, for <a> frames, then (PKMN_TEST_EXIT) the end */
        StartFieldKeys(a);
    }
    else if (strcmp(kind, "save") == 0)
    {
        u8 status = TrySavingData(SAVE_NORMAL);

        TestLog("Battle env test: the test party (%d Pokemon) and bag saved, status %d",
                gPartiesCount[B_TRAINER_PLAYER], status);
        s.saveExitFrames = s.exitAfter ? 30 : 0;
    }
    else if (strcmp(kind, "wild") == 0)
    {
        CreateScriptedWildMon(a, b > 0 ? b : 30, ITEM_NONE);
        RedoTestFoes(1);
        MaybeShiny();
        BattleSetup_StartScriptedWildBattle();
    }
    else if (strcmp(kind, "safari") == 0)
    {
        /* A Safari Zone battle, as the Safari Zone's grass starts one: the Safari controller's menu, where
         * A throws a Safari Ball (the trainer's throw, the shakes, a catch or a break-out). The Safari flag
         * and the 30 balls are set in memory only, as EnterSafariMode sets them. */
        CreateScriptedWildMon(a, b > 0 ? b : 30, ITEM_NONE);
        MaybeShiny();
        FlagSet(FLAG_SYS_SAFARI_MODE);
        gNumSafariBalls = 30;
        BattleSetup_StartWildBattle();
    }
    else if (strcmp(kind, "tutorial") == 0)
    {
        /* Wally's catch tutorial, as Petalburg City's scene starts it (PetalburgCity_EventScript_WallyTutorial):
         * his Zigzagoon first in the party, a Ralts to catch. Wally's controller plays it by itself: two Tackles,
         * then he calls Zigzagoon back, comes out and throws a Poke Ball (the trainer's throw, the catch).
         * In memory only. */
        SavePlayerParty();
        LoadWallyZigzagoon();
        StartWallyTutorialBattle();
    }
    else if (strcmp(kind, "wild2") == 0)
    {
        CreateScriptedDoubleWildMon(a, b > 0 ? b : 30, ITEM_NONE, c, d > 0 ? d : 30, ITEM_NONE);
        RedoTestFoes(2);
        BattleSetup_StartScriptedDoubleWildBattle();
    }
    else if (strcmp(kind, "legendary") == 0)
    {
        CreateScriptedWildMon(a, b > 0 ? b : 50, ITEM_NONE);
        RedoTestFoes(1);
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
        TestLog("Battle env test: unknown battle kind '%s'", kind);
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
        SetMonMoveSlot(mon, s.firstMove, 0);
    }
    /* The Set battle style: in a single battle the Shift style asks whether to switch when
     * the opponent sends its next Pokemon, and A's "Yes" opens the party on the battler
     * already out ("already in battle!"), over and over. */
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    HealPlayerParty();
    TestLog("Battle env test: the party at level %d, healed, its first move %d", MAX_LEVEL, s.firstMove);
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
    if (s.leadOnly && !s.trimmed)
    {
        s.trimmed = true;
        for (u32 i = 1; i < PARTY_SIZE; i++)
            ZeroMonData(&gParties[B_TRAINER_PLAYER][i]);
        CalculatePlayerPartyCount();
        TestLog("Battle env test: the party is its first Pokemon alone");
    }
    SDL_AtomicSet(&s.fieldSeen, 1);
    if (gMain.inBattle)
        SDL_AtomicSet(&s.battleSeen, 1);
    /* PKMN_TEST_FIGHT: the field is back after the fought battle - how it ended */
    if (s.fightSeconds > 0 && !s.fightOver && SDL_AtomicGet(&s.menuSeen) != 0 && !gMain.inBattle)
    {
        s.fightOver = true;
        TestLog("Battle env test: battle over, outcome %d (%s), back on map %d.%d at %d,%d",
                gBattleOutcome, gBattleOutcome == B_OUTCOME_WON ? "won"
                              : gBattleOutcome == B_OUTCOME_LOST ? "lost" : "other",
                gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
        MegaLogAfter();
        if (s.sweep)
        {
            SweepNext();
            return;
        }
        if (s.fieldAfter > 0 && s.fieldKeys[0] != '\0')
        {
            s.fieldAfterPending = true;     /* PKMN_TEST_FIELD_AFTER: the field keys (below), instead of the end */
            return;
        }
        if (s.exitAfter)
        {
            fflush(stdout);
            fflush(stderr);
            _exit(gBattleOutcome == B_OUTCOME_WON ? 0 : 4);
        }
    }
    if (s.resetSeen && ++s.resetFieldFrames == 90)
    {
        TestLog("Battle env test: soft reset: back on the field");
        /* PKMN_TEST_SOFT_RESET=<n>: the same field keys again, from the field the save brought back */
        if (++s.resetsDone < s.softResets && s.fieldKeys[0] != '\0')
        {
            TestLog("Battle env test: soft reset %d of %d: the field keys again", s.resetsDone + 1, s.softResets);
            s.resetSeen = false;
            s.resetFieldFrames = s.resetWaitFrames = 0;
            StartFieldKeys(s.fieldEnd);
        }
        else if (s.exitAfter)
        {
            if (sLogFile != NULL)
                fclose(sLogFile);
            fflush(NULL);
            _exit(0);
        }
    }
    if (s.saveExitFrames > 0 && --s.saveExitFrames == 0)
    {
        TestLog("Battle env test: the save is written; exiting");
        fflush(NULL);
        _exit(0);
    }
    if (s.fieldAfterPending && !ArePlayerFieldControlsLocked() && !gPaletteFade.active && !ScriptContext_IsEnabled())
    {
        s.fieldAfterPending = false;    /* the field after the battle settled: the keys from now */
        StartFieldKeys(s.fieldAfter);
    }
    if (s.spec[0] == '\0' || s.started)
        return;
    if (ArePlayerFieldControlsLocked() || gPaletteFade.active || ScriptContext_IsEnabled())
    {
        s.fieldFrames = 0;
        return;
    }
    if (s.warp && !s.warped)
    {
        /* the field settled first, as one would open the debug menu */
        if (++s.fieldFrames < 30)
            return;
        s.warped = true;
        s.fieldFrames = 0;
        TestLog("Battle env test: warping from map %d.%d to map %d.%d warp %d (%d,%d)",
                gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                s.warpGroup, s.warpNum, s.warpId, s.warpX, s.warpY);
        if (s.warpX >= 0 && s.warpY >= 0)
            SetWarpDestination(s.warpGroup, s.warpNum, WARP_ID_NONE, s.warpX, s.warpY);
        else
            SetWarpDestinationToMapWarp(s.warpGroup, s.warpNum, s.warpId);
        DoWarp();
        ResetInitialPlayerAvatarState();
        return;
    }
    /* PKMN_TEST_FREEZE_FIELD: the battle that starts is the one asked for - while it waits, the field
     * stands still, so a visible wild Pokemon does not walk into the player and start another first
     * (KNOWN_ISSUES N33; the trainer battles below freeze it too). */
    if (s.freezeField)
        FreezeObjectEvents();
    if (++s.fieldFrames < s.delay)
        return;
    if (s.sweep && !s.sweepStarted)
    {
        s.sweepStarted = true;
        SweepPrepare();     /* the first holder (the later ones: SweepNext) */
    }
    s.started = true;
    StartBattle();
}

/*
 * The parade (sprite tests): at the action menu, both sides' first battlers take the next
 * species' look - the species set on the Pokemon and its picture reloaded as a form change
 * reloads it (HandleSpeciesGfxDataChange) - every PKMN_TEST_PARADE_FRAMES frames, so one battle
 * shows every species' front and back. Species without a picture of their own are skipped.
 */
static void SetParadeLook(enum BattlerId battler, u32 species)
{
    struct Pokemon *mon = GetBattlerMon(battler);
    u32 personality = GetMonData(mon, MON_DATA_PERSONALITY), i;
    bool32 shiny = s.paradeShiny;

    if (s.paradeGender != 0)
    {
        /* a personality of that gender: the Pokemon made again with it (its data is encrypted
         * with the personality, which cannot just be overwritten) */
        u8 want = s.paradeGender == 'f' ? MON_FEMALE : MON_MALE;
        u32 level = GetMonData(mon, MON_DATA_LEVEL);

        for (i = 0; i < 4096 && GetGenderFromSpeciesAndPersonality(species, personality) != want; i++)
            personality += 0x9E3779B1;
        CreateMonWithIVs(mon, species, level, personality, OTID_STRUCT_PLAYER_ID, MAX_PER_STAT_IVS);
    }
    else
        SetMonData(mon, MON_DATA_SPECIES, &species);
    SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
    HandleSpeciesGfxDataChange(battler, battler, SPECIES_GFX_CHANGE_FORM_CHANGE);
}

static void ParadeTick(void)
{
    u32 species;

    if (s.paradeDone || ++s.paradeWait < s.paradeFrames)
        return;
    s.paradeWait = 0;
    if (s.paradeMegas)
    {
        if (s.paradeHolder >= sHolderCount)
        {
            s.paradeDone = true;
            TestLog("Battle env test: parade done");
            if (s.exitAfter)
            {
                if (sLogFile != NULL)
                    fclose(sLogFile);
                fflush(NULL);
                _exit(0);
            }
            return;
        }
        species = sHolders[s.paradeHolder++].mega;
        SetParadeLook(GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), species);
        SetParadeLook(GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT), species);
        TestLog("Battle env test: parade species %u (Mega form %d)", species, s.paradeHolder - 1);
        RequestShot(s.paradeHolder - 1, s.paradeFrames / 2, "a Mega form");
        return;
    }
    for (species = s.paradeNext; species <= (u32)s.paradeLast && species < NUM_SPECIES; species++)
        if (IsSpeciesEnabled(species) && gSpeciesInfo[species].frontPic != NULL
         && gSpeciesInfo[species].frontPic != gSpeciesInfo[SPECIES_NONE].frontPic)
            break;
    if (species > (u32)s.paradeLast || species >= NUM_SPECIES)
    {
        s.paradeDone = true;
        TestLog("Battle env test: parade done");
        if (s.exitAfter)
        {
            fflush(NULL);
            _exit(0);
        }
        return;
    }
    s.paradeNext = species + 1;
    SetParadeLook(GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), species);
    SetParadeLook(GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT), species);
    TestLog("Battle env test: parade species %u", species);
}

void DioramaBattleEnvTest_OnActionMenu(void)
{
    if (!s.init)
        Init();
    if (s.stem[0] != '\0' || s.fightSeconds > 0)
        SDL_AtomicSet(&s.menuSeen, 1);
    if (s.paradeFrames > 0)
        ParadeTick();
}

/*
 * The keys a person would press, for a run with nobody at the keyboard (the owner's
 * graphics-card measurements): Start past the intro, then A every three seconds (the title
 * screen, Continue) until the field is up; then B every second and a half - a scripted scene
 * on entering the map, the battle's opening lines - until the action menu is up; nothing
 * after that. As --benchmark's (platform/benchmark.c) and the survey's (survey.py) presses.
 */
/* PKMN_TEST_KEYS "frame:KEY,...": KEY held for three frames from that many frames after the
 * action menu first came up (A B START SELECT UP DOWN LEFT RIGHT L R). */
static unsigned short KeysAt(const char *script, int frame)
{
    static const struct { const char *name; unsigned short bit; } names[] = {
        {"A", A_BUTTON}, {"B", B_BUTTON}, {"START", START_BUTTON}, {"SELECT", SELECT_BUTTON},
        {"UP", DPAD_UP}, {"DOWN", DPAD_DOWN}, {"LEFT", DPAD_LEFT}, {"RIGHT", DPAD_RIGHT},
        {"L", L_BUTTON}, {"R", R_BUTTON},
    };
    const char *p = script;
    unsigned short held = 0;
    int at, used;
    char name[16];

    while (*p != '\0' && sscanf(p, "%d:%15[A-Z]%n", &at, name, &used) == 2)
    {
        if (frame >= at && frame < at + AUTOPILOT_HOLD)
            for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
                if (strcmp(name, names[i].name) == 0)
                    held |= names[i].bit;
        p += used;
        if (*p == ',')
            p++;
    }
    return held;
}

static unsigned short ScriptKeys(void)
{
    return KeysAt(s.keys, s.keyFrames++);
}

unsigned short DioramaBattleEnvTest_FilterKeys(unsigned short keys, unsigned int nowMs)
{
    unsigned int t;

    if (!s.init)
        Init();
    MegaLogFrame();
    MegaLogField();
    StarterLogField();
    ShotTick();
    /* PKMN_TEST_SOFT_RESET: the game started again (its frame count went back: a soft reset, not a battle's end);
     * the field keys stop and, from the title, the autopilot goes as at the start (Start, A to CONTINUE) */
    if (s.softReset && !s.resetSeen && gMain.vblankCounter1 < s.lastVblank)
    {
        s.resetSeen = true;
        TestLog("Battle env test: soft reset: the game restarted (frame %u, %u before)", gMain.vblankCounter1,
                s.lastVblank);
        s.fieldKeysActive = false;
        s.autopilot = true;
        s.startMs = 0;
        s.startPressed = s.fieldPhase = false;
        s.pressLeft = s.gapLeft = 0;
        s.fightOver = true;
        s.fieldSeenSpecies = -1;    /* the field after it is logged, whatever it shows */
        s.fieldSeenCoins = s.fieldSeenMoney = -1;
        s.starterLogged = false;
        SDL_AtomicSet(&s.fieldSeen, 0);
        SDL_AtomicSet(&s.menuSeen, 0);
    }
    s.lastVblank = gMain.vblankCounter1;
    /* the field keys ended and no reset came: say so and stop */
    if (s.softReset && !s.resetSeen && !s.fieldKeysActive && s.fieldEnd > 0 && ++s.resetWaitFrames == 600)
    {
        TestLog("Battle env test: soft reset: the game did not restart");
        if (s.exitAfter)
        {
            if (sLogFile != NULL)
                fclose(sLogFile);
            fflush(NULL);
            _exit(6);
        }
    }
    /* PKMN_TEST_BATTLE=field: only the field keys (no autopilot), then the end */
    if (s.fieldKeysActive)
    {
        if (s.fieldKeyFrames >= s.fieldEnd)
        {
            TestLog("Battle env test: field keys done (%d frames)", s.fieldKeyFrames);
            s.fieldKeysActive = false;
            if (s.exitAfter && !s.softReset)
            {
                if (sLogFile != NULL)
                    fclose(sLogFile);
                fflush(NULL);
                _exit(0);
            }
            return keys;
        }
        for (int i = 0; i < s.fieldShotCount; i++)
            if (s.fieldShots[i] == s.fieldKeyFrames)
                RequestShot(50 + s.resetsDone * s.fieldShotCount + i, 1, "a field step");
        return keys | KeysAt(s.fieldKeys, s.fieldKeyFrames++);
    }
    if (s.startMs == 0)
        s.startMs = nowMs == 0 ? 1 : nowMs;
    t = nowMs - s.startMs;
    /* no picture in time (a battle drawn in 2D has none to take): say so and stop */
    if (s.timeoutSeconds > 0 && t > (unsigned int)s.timeoutSeconds * 1000u
     && SDL_AtomicGet(&s.captured) == 0)
    {
        TestLog("Battle env test: no picture after %d seconds (the battle %s); giving up",
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
    if (SDL_AtomicGet(&s.menuSeen) != 0 && s.keys[0] != '\0'
     && (s.fightSeconds <= 0 || s.keyFrames <= s.keysLast + AUTOPILOT_HOLD))
        return keys | ScriptKeys();
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
            TestLog("Battle env test: the battle did not end in %d seconds; giving up", s.fightSeconds);
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

/* PKMN_TEST_CAPTURE_EVERY/_COUNT: a series of captures <stem>-NN, every that many frames the 3D
 * battle draws, from PKMN_TEST_CAPTURE_DELAY frames after the action menu first came up (or
 * after the battle was first drawn, PKMN_TEST_CAPTURE_FROM=battle). */
static const char *SeriesStem(void)
{
    int frame = SDL_AtomicAdd(&s.renderFrames, 1), base, target;

    if (s.menuRenderFrame < 0 && SDL_AtomicGet(&s.menuSeen) != 0)
        s.menuRenderFrame = frame;
    if (s.seriesDone >= s.seriesCount)
        return NULL;
    if (s.seriesFromBattle)
        base = 0;
    else if (s.menuRenderFrame >= 0)
        base = s.menuRenderFrame;
    else
        return NULL;
    target = base + s.captureDelay + s.seriesDone * s.seriesEvery;
    if (frame < target)
        return NULL;
    SDL_snprintf(s.seriesStem, sizeof(s.seriesStem), "%s-%02d", s.stem, s.seriesDone);
    return s.seriesStem;
}

const char *DioramaBattleEnvTest_CaptureStem(void)
{
    if (SDL_AtomicCAS(&s.megaShotState, 1, 2))
        return s.megaShotPath;
    if (SDL_AtomicGet(&s.megaShotState) == 2)
        return NULL;
    if (s.stem[0] != '\0' && s.seriesEvery > 0 && s.seriesCount > 0)
        return SeriesStem();
    if (SDL_AtomicGet(&s.menuSeen) == 0 || SDL_AtomicGet(&s.captured) != 0 || s.stem[0] == '\0')
        return NULL;
    if (SDL_AtomicAdd(&s.menuFrames, 1) + 1 < s.captureDelay)
        return NULL;
    return s.stem;
}

void DioramaBattleEnvTest_CaptureWritten(void)
{
    if (SDL_AtomicCAS(&s.megaShotState, 2, 3))
    {
        TestLog("Battle env test: captured %s (%s)", s.megaShotPath, s.megaShotWhat);
        return;
    }
    if (s.seriesEvery > 0 && s.seriesCount > 0)
    {
        TestLog("Battle env test: captured %s", s.seriesStem);
        SDL_AtomicSet(&s.captured, 1);
        if (++s.seriesDone < s.seriesCount)
            return;
    }
    else
    {
        SDL_AtomicSet(&s.captured, 1);
        TestLog("Battle env test: captured %s", s.stem);
    }
    if (s.exitAfter && s.fightSeconds <= 0)
    {
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
}

#ifndef ENABLE_DIORAMA
/* The Classic 2D test build (platform/sdl2.c, each presented frame, the game parked): the captures the 3D battle takes
 * there (CaptureStem), of the GBA frame, while a battle runs. */
void DioramaBattleEnvTest_Frame2D(const unsigned short *pixels)
{
    static uint8_t rgba[DISPLAY_WIDTH * DISPLAY_HEIGHT * 4];
    const char *stem;
    char path[640];

    if (!s.init || (!gMain.inBattle && !s.fieldKeysActive) || (stem = DioramaBattleEnvTest_CaptureStem()) == NULL)
        return;
    for (unsigned p = 0; p < DISPLAY_WIDTH * DISPLAY_HEIGHT; p++)
    {
        unsigned short c = pixels[p];

        rgba[p * 4 + 0] = (uint8_t)((c & 31) * 255 / 31);
        rgba[p * 4 + 1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
        rgba[p * 4 + 2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
        rgba[p * 4 + 3] = 255;
    }
    SDL_snprintf(path, sizeof(path), "%s.png", stem);
    PngWrite_RGBA(path, rgba, DISPLAY_WIDTH, DISPLAY_HEIGHT, false);
    DioramaBattleEnvTest_CaptureWritten();
}
#endif

#endif
