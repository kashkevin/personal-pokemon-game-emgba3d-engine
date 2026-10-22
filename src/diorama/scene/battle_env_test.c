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
#include "battle_main.h"
#include "battle_partner.h"
#include "battle_setup.h"
#include "data.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "load_save.h"
#include "main.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "daycare.h"
#include "constants/party_menu.h"
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
#include "constants/script_commands.h"
#include "constants/event_objects.h"
#include "constants/trainer_types.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "constants/multiplayer_balance.h"
#include "constants/multiplayer_rival.h"
#include "platform/coin_shop.h"
#include "diorama/battle_env_test.h"
#include "tower_legends.h"
#include "battle_util.h"
#include "move.h"

extern const u8 *ScriptContext_DebugPosition(int *mode, int *status);  /* script.c (PORTABLE) */

/* script.c, ScriptContext_SetupScript (test builds): every field script started */
u32 gDioramaBattleEnvTestScriptStarts;

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
    unsigned short fightButton;   /* the fight's press in progress (A, or B / DOWN on a forced switch's party screen) */
    unsigned int partySteps;      /* presses made on a forced switch's party screen */
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
    /* PKMN_TEST_MORE_MOVES=<move>,<move>,<move>: with PKMN_TEST_STRONG, the party's other three moves; a plan's
     * battles then choose the most effective of the four against the foe (PlanPickMoves) */
    int moreMoves[MAX_MON_MOVES - 1];
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
    char fieldKeys[8192];     /* longer PKMN_TEST_FIELD_KEYS: refused (logged), never cut short */
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
    int towerFloor;     /* tower:<floor> - Battle Tower: Legends' solo floor (src/tower_legends.c) */
    bool towerWin;      /* PKMN_TEST_TOWER_WIN=1: the lead's HP refilled each turn and its most effective move chosen */
    bool towerLogged;   /* its opposing party logged at the first action menu */
    u32 towerMoney;     /* the money as it started (a Tower battle neither pays nor takes any) */
    /* PKMN_TEST_TRAIN_LOG=1: on the field, "train field: ..." whenever the lead's IVs, Hyper Training, Ability or the
     * Bag's caps and patches change (TrainLogField); what was last logged */
    bool trainLog, trainLogged;
    int trainSeen[13];
    /* PKMN_TEST_DAYCARE="spec;spec": the two Pokemon at the Day Care; PKMN_TEST_EGG_READY=1: an Egg of theirs waiting;
       PKMN_TEST_EGG_CYCLES=n: a party Egg with more cycles left has n; PKMN_TEST_BREED_LOG=1: "breed ..." lines
       (tools/battle_env/breed_check.py) */
    char daycare[256];
    bool eggReady, breedLog, breedLogged;
    int eggCycles;
    char breedSeen[PARTY_SIZE][192];
    int breedStateSeen[3];
    /* PKMN_TEST_BAG_LOG=item[,item...] (at most 4): on the field, "bag field: money M item I count C ..." whenever the
       money or one of those items' count in the Bag changes (tools/battle_env/shop_check.py: a Poke Mart's purchase and
       sale, saved and loaded); what was last logged */
    int bagItems[4], bagItemCount;
    bool bagLogged;
    u32 bagSeen[5];
} s;

/* PKMN_TEST_LOG=<file>: every line the driver logs is also appended to that file (a check on Windows reads it: the
 * game's own log may reach no console there). */
static FILE *sLogFile;
static void LoadPlan(const char *path);    /* PKMN_TEST_PLAN (below) */

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
    FACT(MP_INTRO_STARTER_PENDING), FACT(MOVE_FLASH_CANNON), FACT(TRAINER_FLAGS_START), FACT(ITEMS_COUNT),
    FACT(MOVE_THUNDERBOLT), FACT(MOVE_ICE_BEAM), FACT(MOVE_ENERGY_BALL),
    FACT(ITEM_POTION), FACT(MAP_OLDALE_TOWN_MART),
};
#undef FACT

static void LogFacts(void)
{
    for (u32 i = 0; i < sizeof(sFacts) / sizeof(sFacts[0]); i++)
        TestLog("Battle env test: fact %s %d", sFacts[i].name, sFacts[i].value);
    /* what the Mauville dealer asks for its first stone, in coins (the coin shop's own price) */
    TestLog("Battle env test: fact PRICE_ITEM_VENUSAURITE %u", CoinShop_StonePrice(ITEM_VENUSAURITE));
    /* what a Poke Mart asks for a Potion and pays for one (the game's own prices) */
    TestLog("Battle env test: fact PRICE_ITEM_POTION %u", GetItemPrice(ITEM_POTION));
    TestLog("Battle env test: fact SELL_PRICE_ITEM_POTION %u", GetItemSellPrice(ITEM_POTION));
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
    if (value != NULL && SDL_strlcpy(s.fieldKeys, value, sizeof(s.fieldKeys)) >= sizeof(s.fieldKeys))
    {
        /* a script cut short would drop its last keys without a word (breed_check.py's hatch walk did) */
        TestLog("Battle env test: cannot read PKMN_TEST_FIELD_KEYS: %u characters, at most %u",
                (unsigned)SDL_strlen(value), (unsigned)sizeof(s.fieldKeys) - 1);
        s.fieldKeys[0] = '\0';
    }
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
    s.towerWin = EnvInt("PKMN_TEST_TOWER_WIN", 0) != 0;
    s.firstMove = EnvInt("PKMN_TEST_FIRST_MOVE", 0);
    if (s.firstMove <= MOVE_NONE || s.firstMove >= MOVES_COUNT)
        s.firstMove = MOVE_FLAMETHROWER;
    value = SDL_getenv("PKMN_TEST_MORE_MOVES");
    for (u32 i = 0; value != NULL && *value != '\0' && i < ARRAY_COUNT(s.moreMoves); i++)
    {
        int move = atoi(value);

        s.moreMoves[i] = move > MOVE_NONE && move < MOVES_COUNT ? move : MOVE_NONE;
        value = strchr(value, ',');
        if (value != NULL)
            value++;
    }
    s.megaLog = EnvInt("PKMN_TEST_MEGA_LOG", 0) != 0;
    s.softResets = EnvInt("PKMN_TEST_SOFT_RESET", 0);
    s.softReset = s.softResets > 0;
    s.starterLog = EnvInt("PKMN_TEST_STARTER_LOG", 0) != 0;
    s.trainLog = EnvInt("PKMN_TEST_TRAIN_LOG", 0) != 0;
    value = SDL_getenv("PKMN_TEST_DAYCARE");
    if (value != NULL)
        snprintf(s.daycare, sizeof(s.daycare), "%s", value);
    s.eggReady = EnvInt("PKMN_TEST_EGG_READY", 0) != 0;
    s.eggCycles = EnvInt("PKMN_TEST_EGG_CYCLES", -1);
    s.breedLog = EnvInt("PKMN_TEST_BREED_LOG", 0) != 0;
    value = SDL_getenv("PKMN_TEST_BAG_LOG");
    for (s.bagItemCount = 0; value != NULL && *value != '\0' && s.bagItemCount < 4; )
    {
        s.bagItems[s.bagItemCount++] = atoi(value);
        value = strchr(value, ',');
        if (value != NULL)
            value++;
    }
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
    value = SDL_getenv("PKMN_TEST_PLAN");
    if (value != NULL && value[0] != '\0')
        LoadPlan(value);
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
 * n<nature>: a personality of that nature too; b<ball>: caught in that ball (breed_check.py's parents).
 */
static void CreateTestMon(struct Pokemon *mon, int species, int level, const char *flags)
{
    u32 personality = 0x13579BDF, i;
    int gender = -1, item = 0, move = 0, ivs = MAX_PER_STAT_IVS, nature = -1, ball = 0;
    bool shiny = false, nextLevelShort = false;

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
        else if (*flags == 'w')
            ivs = atoi(flags + 1);
        else if (*flags == 'e')
            nextLevelShort = true;
        else if (*flags == 'n')
            nature = atoi(flags + 1);
        else if (*flags == 'b')
            ball = atoi(flags + 1);
    }
    for (i = 0; i < 65536 && ((gender >= 0 && GetGenderFromSpeciesAndPersonality(species, personality) != gender)
                             || (nature >= 0 && GetNatureFromPersonality(personality) != (u32)nature)); i++)
        personality += 0x9E3779B1;
    CreateMonWithIVs(mon, species, level > 0 ? level : 30, personality, OTID_STRUCT_PLAYER_ID,
                     ivs >= 0 && ivs <= MAX_PER_STAT_IVS ? ivs : MAX_PER_STAT_IVS);
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
    if (ball > 0)
    {
        u8 caughtIn = ball;

        SetMonData(mon, MON_DATA_POKEBALL, &caughtIn);
    }
    if (nextLevelShort && level > 0 && level < MAX_LEVEL)
    {
        /* e: one experience point short of the next level (the Tower experience cap's runs) */
        u32 exp = gExperienceTables[gSpeciesInfo[species].growthRate][level + 1] - 1;

        SetMonData(mon, MON_DATA_EXP, &exp);
    }
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

/* PKMN_TEST_DAYCARE="species:level[:flags];species:level[:flags]": the two Pokemon left at the Day Care (Route 117) as
 * the Day Care lady stores them; PKMN_TEST_EGG_READY=1: an Egg of theirs made now, waiting with the Day Care man, as a
 * step check that passed would make it (daycare.c MPDaycare_TestProduceEgg). Test builds only. */
void MPDaycare_TestProduceEgg(void);

static void SetUpTestDaycare(void)
{
    const char *p = s.daycare;
    int slot = 0;

    if (s.daycare[0] == '\0')
        return;
    while (*p != '\0' && slot < DAYCARE_MON_COUNT)
    {
        struct Pokemon mon;
        int species = 0, level = 0, used = 0;
        const char *flags = NULL;

        if (sscanf(p, "%d:%d%n", &species, &level, &used) < 2)
            break;
        p += used;
        if (*p == ':')
            flags = ++p;
        CreateTestMon(&mon, species, level, flags);
        StorePokemonInDaycare(&mon, &gSaveBlock1Ptr->daycare.mons[slot++]);
        while (*p != '\0' && *p != ';')
            p++;
        if (*p == ';')
            p++;
    }
    TestLog("Battle env test: breed daycare: %d Pokemon at the Day Care", CountPokemonInDaycare(&gSaveBlock1Ptr->daycare));
    if (s.eggReady)
    {
        MPDaycare_TestProduceEgg();
        TestLog("Battle env test: breed daycare: an Egg waits %d", FlagGet(FLAG_PENDING_DAYCARE_EGG));
    }
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

/* PKMN_TEST_TRAIN_LOG off a battle: "train field: lead S ivs a,b,c,d,e,f trained M ability A num N caps C gold G
 * patches P" whenever one changes (M the Hyper Training bits, HP first; N the Ability slot, 2 the Hidden Ability) - a
 * Bottle Cap, a Gold Bottle Cap or an Ability Patch used in the Bag, refused, saved and loaded. */
static void TrainLogField(void)
{
    struct Pokemon *lead = &gParties[B_TRAINER_PLAYER][0];
    int now[13];
    bool changed;

    if (!s.trainLog || gMain.inBattle || SDL_AtomicGet(&s.fieldSeen) == 0 || gSaveBlock1Ptr == NULL)
        return;
    now[0] = gPartiesCount[B_TRAINER_PLAYER] > 0 ? (int)GetMonData(lead, MON_DATA_SPECIES) : 0;
    now[7] = 0;
    for (int stat = 0; stat < NUM_STATS; stat++)
    {
        now[1 + stat] = now[0] != 0 ? (int)GetMonData(lead, MON_DATA_HP_IV + stat) : 0;
        if (now[0] != 0 && GetMonData(lead, MON_DATA_HYPER_TRAINED_HP + stat))
            now[7] |= 1 << stat;
    }
    now[8] = now[0] != 0 ? (int)GetMonAbility(lead) : 0;
    now[9] = now[0] != 0 ? (int)GetMonData(lead, MON_DATA_ABILITY_NUM) : 0;
    now[10] = CountTotalItemQuantityInBag(ITEM_BOTTLE_CAP);
    now[11] = CountTotalItemQuantityInBag(ITEM_GOLD_BOTTLE_CAP);
    now[12] = CountTotalItemQuantityInBag(ITEM_ABILITY_PATCH);
    changed = !s.trainLogged;
    for (int i = 0; i < 13 && !changed; i++)
        changed = now[i] != s.trainSeen[i];
    if (!changed)
        return;
    s.trainLogged = true;
    memcpy(s.trainSeen, now, sizeof(now));
    TestLog("Battle env test: train field: lead %d ivs %d,%d,%d,%d,%d,%d trained %d ability %d num %d caps %d gold %d patches %d",
            now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7], now[8], now[9], now[10], now[11], now[12]);
}

/* PKMN_TEST_BAG_LOG off a battle: "bag field: money M item I count C [item I count C ...]" whenever the money or one of
 * the named items' count in the Bag changes - a Poke Mart purchase or sale, saved and loaded. */
static void BagLogField(void)
{
    u32 now[5];
    char line[160];
    int used;
    bool changed;

    if (s.bagItemCount == 0 || gMain.inBattle || SDL_AtomicGet(&s.fieldSeen) == 0 || gSaveBlock1Ptr == NULL)
        return;
    now[0] = GetMoney(&gSaveBlock1Ptr->money);
    for (int i = 0; i < s.bagItemCount; i++)
        now[1 + i] = CountTotalItemQuantityInBag(s.bagItems[i]);
    changed = !s.bagLogged;
    for (int i = 0; i <= s.bagItemCount && !changed; i++)
        changed = now[i] != s.bagSeen[i];
    if (!changed)
        return;
    s.bagLogged = true;
    memcpy(s.bagSeen, now, sizeof(now));
    used = snprintf(line, sizeof(line), "money %u", (unsigned)now[0]);
    for (int i = 0; i < s.bagItemCount && used < (int)sizeof(line); i++)
        used += snprintf(line + used, sizeof(line) - used, " item %d count %u", s.bagItems[i], (unsigned)now[1 + i]);
    TestLog("Battle env test: bag field: %s", line);
}

/* PKMN_TEST_BREED_LOG off a battle: "breed party N: species S egg E level L ivs a,b,c,d,e,f nature T num A ball B
 * moves m1,m2,m3,m4 cycles C" for each party slot whose line changed (the nature its personality's: what an Everstone
 * passes on; cycles: an Egg's cycles left), and "breed state: party N daycare D egg waiting W" when those change.
 * PKMN_TEST_EGG_CYCLES=n: an Egg in the party with more cycles left has n (in memory: it hatches after n more cycles
 * of steps, the next cycle's end with 0). */
static void BreedLogField(void)
{
    char line[192];
    int state[3];

    if (!s.breedLog || gMain.inBattle || SDL_AtomicGet(&s.fieldSeen) == 0 || gSaveBlock1Ptr == NULL)
        return;
    for (int i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        int species = i < gPartiesCount[B_TRAINER_PLAYER] ? (int)GetMonData(mon, MON_DATA_SPECIES) : 0;
        int egg = species != 0 && GetMonData(mon, MON_DATA_IS_EGG);
        u32 cycles = species != 0 ? GetMonData(mon, MON_DATA_FRIENDSHIP) : 0;

        if (egg && s.eggCycles >= 0 && cycles > (u32)s.eggCycles)
        {
            cycles = s.eggCycles;
            SetMonData(mon, MON_DATA_FRIENDSHIP, &cycles);
        }
        if (species == 0)
            snprintf(line, sizeof(line), "none");
        else
            snprintf(line, sizeof(line), "species %d egg %d level %d ivs %d,%d,%d,%d,%d,%d nature %d num %d ball %d "
                     "moves %d,%d,%d,%d cycles %u", species, egg, (int)GetMonData(mon, MON_DATA_LEVEL),
                     (int)GetMonData(mon, MON_DATA_HP_IV), (int)GetMonData(mon, MON_DATA_ATK_IV),
                     (int)GetMonData(mon, MON_DATA_DEF_IV), (int)GetMonData(mon, MON_DATA_SPEED_IV),
                     (int)GetMonData(mon, MON_DATA_SPATK_IV), (int)GetMonData(mon, MON_DATA_SPDEF_IV),
                     (int)GetNatureFromPersonality(GetMonData(mon, MON_DATA_PERSONALITY)),
                     (int)GetMonData(mon, MON_DATA_ABILITY_NUM), (int)GetMonData(mon, MON_DATA_POKEBALL),
                     (int)GetMonData(mon, MON_DATA_MOVE1), (int)GetMonData(mon, MON_DATA_MOVE2),
                     (int)GetMonData(mon, MON_DATA_MOVE3), (int)GetMonData(mon, MON_DATA_MOVE4), cycles);
        if (!s.breedLogged || strcmp(line, s.breedSeen[i]) != 0)
        {
            snprintf(s.breedSeen[i], sizeof(s.breedSeen[i]), "%s", line);
            if (species != 0 || s.breedLogged)
                TestLog("Battle env test: breed party %d: %s", i, line);
        }
    }
    state[0] = gPartiesCount[B_TRAINER_PLAYER];
    state[1] = CountPokemonInDaycare(&gSaveBlock1Ptr->daycare);
    state[2] = FlagGet(FLAG_PENDING_DAYCARE_EGG);
    if (!s.breedLogged || memcmp(state, s.breedStateSeen, sizeof(state)) != 0)
    {
        memcpy(s.breedStateSeen, state, sizeof(state));
        TestLog("Battle env test: breed state: party %d daycare %d egg waiting %d", state[0], state[1], state[2]);
    }
    s.breedLogged = true;
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

/* tower:<floor> - Battle Tower: Legends' solo floor through the game's own start (src/tower_legends.c: its script, the
 * floor's intro, the battle against the floor's trainer, the end: healed, held items back, no money moved, the result in
 * the save's Tower record), without a host (the test ticket is never sent). The opposing party is logged as the battle
 * built it (TowerLogParty) and the end once the field is back (TowerLogEnd). */
static void StartTowerFloor(int floor)
{
    s.towerFloor = floor;
    s.towerLogged = false;
    s.towerMoney = GetMoney(&gSaveBlock1Ptr->money);
    /* The Set battle style (in memory, as PKMN_TEST_STRONG's): the Shift style's "switch?" question after each opposing
     * Pokemon falls has no answer the fight's A presses can give. */
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    /* PKMN_TEST_BADGES=n and PKMN_TEST_CHAMPION=0/1: the badges and the Hall of Fame flag in memory, for the Tower
     * experience cap's runs (tools/battle_env/tower_check.py --caps) */
    if (getenv("PKMN_TEST_BADGES") != NULL)
    {
        int badges = EnvInt("PKMN_TEST_BADGES", 0);

        for (int i = 0; i < NUM_BADGES; i++)
        {
            if (i < badges)
                FlagSet(FLAG_BADGE01_GET + i);
            else
                FlagClear(FLAG_BADGE01_GET + i);
        }
    }
    if (getenv("PKMN_TEST_CHAMPION") != NULL)
    {
        if (EnvInt("PKMN_TEST_CHAMPION", 0) != 0)
            FlagSet(FLAG_SYS_GAME_CLEAR);
        else
            FlagClear(FLAG_SYS_GAME_CLEAR);
    }
    if (!TowerLegends_TestStartSolo(floor))
        TestLog("Battle env test: tower floor %d does not exist", floor);
    else
        TestLog("Battle env test: tower floor %d: the intro, then the battle (money %u, cap %u)", floor, s.towerMoney,
                TowerLegends_ExpCap());
}

static void TowerLogMon(const char *side, u32 slot, struct Pokemon *mon)
{
    TestLog("Battle env test: tower %s %u species %u level %u item %u ability %u moves %u %u %u %u hp %u/%u exp %u",
            side, slot, GetMonData(mon, MON_DATA_SPECIES), GetMonData(mon, MON_DATA_LEVEL),
            GetMonData(mon, MON_DATA_HELD_ITEM), GetMonAbility(mon), GetMonData(mon, MON_DATA_MOVE1),
            GetMonData(mon, MON_DATA_MOVE2), GetMonData(mon, MON_DATA_MOVE3), GetMonData(mon, MON_DATA_MOVE4),
            GetMonData(mon, MON_DATA_HP), GetMonData(mon, MON_DATA_MAX_HP), GetMonData(mon, MON_DATA_EXP));
}

static void TowerLogParty(void)
{
    const struct Trainer *trainer = GetTrainerStructFromId(TRAINER_BATTLE_PARAM.opponentA);
    u32 i;

    s.towerLogged = true;
    TestLog("Battle env test: tower battle floor %u mode %u opponent %u class %u pic %u party %u",
            TowerLegends_ActiveFloor(), TowerLegends_ActiveMode(), TRAINER_BATTLE_PARAM.opponentA,
            trainer->trainerClass, trainer->trainerPic, gPartiesCount[B_TRAINER_OPPONENT_A]);
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gParties[B_TRAINER_OPPONENT_A][i], MON_DATA_SPECIES) != SPECIES_NONE)
            TowerLogMon("foe", i, &gParties[B_TRAINER_OPPONENT_A][i]);
}

static void TowerLogEnd(void)
{
    const struct MPTowerRecord *record = &gSaveBlock3Ptr->tower;
    u32 i;

    TestLog("Battle env test: tower end floor %d pending %u ticket %08x floor %u record %u money %u (was %u) cap %u",
            s.towerFloor, TowerLegends_RecordValid() ? record->pendingOutcome : 0,
            TowerLegends_RecordValid() ? record->pendingTicket : 0, TowerLegends_RecordValid() ? record->pendingFloor : 0,
            TowerLegends_RecordValid(), GetMoney(&gSaveBlock1Ptr->money), s.towerMoney, TowerLegends_ExpCap());
    for (i = 0; i < PARTY_SIZE; i++)
        if (GetMonData(&gParties[B_TRAINER_PLAYER][i], MON_DATA_SPECIES) != SPECIES_NONE)
            TowerLogMon("own", i, &gParties[B_TRAINER_PLAYER][i]);
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
    SetUpTestDaycare();
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
        u8 status;
        int highest = 0, progress = 0;
        const char *record = getenv("PKMN_TEST_TOWER_RECORD");

        /* PKMN_TEST_TOWER_RECORD=H:P - the save's Battle Tower record as a climb that cleared floor H and goes on at P */
        if (record != NULL && sscanf(record, "%d:%d", &highest, &progress) == 2)
        {
            TowerLegends_TestSetProgress(highest, progress);
            TestLog("Battle env test: the Tower record: highest %d, progress %d (valid %d)", (int)TowerLegends_Highest(),
                    (int)TowerLegends_Progress(), (int)TowerLegends_RecordValid());
        }
        status = TrySavingData(SAVE_NORMAL);

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
    else if (strcmp(kind, "tower") == 0)
        StartTowerFloor(a);
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
        for (u32 j = 0; j < ARRAY_COUNT(s.moreMoves); j++)
            if (s.moreMoves[j] != MOVE_NONE)
                SetMonMoveSlot(mon, s.moreMoves[j], j + 1);
    }
    /* The Set battle style: in a single battle the Shift style asks whether to switch when
     * the opponent sends its next Pokemon, and A's "Yes" opens the party on the battler
     * already out ("already in battle!"), over and over. */
    gSaveBlock2Ptr->optionsBattleStyle = OPTIONS_BATTLE_STYLE_SET;
    HealPlayerParty();
    TestLog("Battle env test: the party at level %d, healed, its moves %d %d %d %d", MAX_LEVEL, s.firstMove,
            s.moreMoves[0], s.moreMoves[1], s.moreMoves[2]);
}

/*
 * PKMN_TEST_PLAN=<file>: the gameplay checks (tools/qa/gameplay_check.py). One target per line,
 *     <key> <kind> <group> <num> <x> <y> <dir> <arg>
 * kind talk or fight (an object event, fight a trainer; arg its local id), sign or hidden (a background event; arg
 * its index), warp or trigger (a warp or a coordinate trigger; arg its index). From wherever the player is, the plan
 * warps to x,y on that map, faces dir (U D L R) and presses A (warp, trigger: holds dir, a step onto it; a trigger logs
 * whether its variable holds the value it runs on), then answers what comes as a player would: B through the lines and
 * every question (NO; a fight's YES until its battle; an Easy Chat screen left by its own CANCEL), A through a battle
 * (the most effective of the party's moves on the first target), until the field is free again. Before each: a taken
 * item ball or hidden item put back (any other object as the save has it: one its story hides is logged as absent),
 * every other trainer of the map counted as beaten and the target trainer as not (the battle that starts is the one
 * asked for), the party healed.
 * Each target logs where it began and ended, whether a script ran, its battles (trainers, outcome), the money, the
 * trainer's beaten flag, the target's own flag and the bag's changes. PKMN_TEST_PLAN_FIRST=n starts at line n (0-based;
 * comments and blank lines not counted). A target that does not give the field back within PKMN_TEST_PLAN_FRAMES
 * (default 2400; 10800 more for each battle) with nothing moving on for PLAN_STALL frames (the player's place, the
 * screen, the script, the message box), or within four times that, is logged as a hang, pictured
 * (<PKMN_TEST_MEGA_SHOT_STEM>-99.png) and the process ends with code 7: the tool starts the game again at the next
 * target. A target whose map's own scene ran first (it began by itself; no battle, the same map) runs once more after
 * it. In memory only: nothing is saved.
 */
#define PLAN_MAX 4096
#define PLAN_SETTLE 30      /* free field frames that end a target */
#define PLAN_STALL 600      /* frames past the budget with nothing moving on that make it a hang */
enum { PLAN_TALK, PLAN_SIGN, PLAN_HIDDEN, PLAN_WARP, PLAN_TRIGGER };
enum { PLAN_OFF, PLAN_START, PLAN_GO, PLAN_ARRIVE, PLAN_ACT, PLAN_RUN, PLAN_DONE, PLAN_HANG, PLAN_FINISHED };
static struct PlanTarget
{
    char key[80];
    u8 kind, dir, group, num;
    bool8 fight;    /* "fight": a trainer, talked to - its questions answered YES until its battle begins */
    s16 x, y, arg;
} sPlanTargets[PLAN_MAX];
static struct
{
    int count, first, next, phase, frames, budget, settle, pressLeft, gapLeft, presses, hangFrames;
    int lastPhase, phaseFrames;
    int lastProgress;
    u32 progress, rng;
    int escapeChoice, escapeDowns, menuUp;
    u16 held;           /* the key of the escape's press being held (chosen once, as the press starts) */
    u32 scriptStarts;       /* gDioramaBattleEnvTestScriptStarts at the arrival */
    s16 warpX, warpY;
    bool warpPending;
    bool shots, byItself, again, ending;
    u32 lastVblank;
    bool script, inBattle, mapChanged;
    int battles, outcome, trainerA, trainerB;
    u16 trainerId, ownFlag;     /* the target trainer (0: none) and the target's own flag (0: none) */
    u32 money;
    u8 mapGroup, mapNum;
    u16 bagBefore[ITEMS_COUNT];
} sPlan;

static void LoadPlan(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[256];
    int index = 0;

    sPlan.first = EnvInt("PKMN_TEST_PLAN_FIRST", 0);
    sPlan.budget = EnvInt("PKMN_TEST_PLAN_FRAMES", 2400);
    sPlan.shots = EnvInt("PKMN_TEST_PLAN_SHOTS", 0) != 0;
    if (file == NULL)
    {
        TestLog("Battle env test: plan: cannot open %s", path);
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL && sPlan.count < PLAN_MAX)
    {
        struct PlanTarget *t = &sPlanTargets[sPlan.count];
        char kind[12], dir[4];
        int group, num, x, y, arg;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (sscanf(line, "%79s %11s %d %d %d %d %3s %d", t->key, kind, &group, &num, &x, &y, dir, &arg) != 8)
        {
            TestLog("Battle env test: plan: cannot read '%s'", line);
            continue;
        }
        if (index++ < sPlan.first)
            continue;
        t->fight = strcmp(kind, "fight") == 0;
        t->kind = strcmp(kind, "talk") == 0 || t->fight ? PLAN_TALK : strcmp(kind, "sign") == 0 ? PLAN_SIGN
                : strcmp(kind, "hidden") == 0 ? PLAN_HIDDEN : strcmp(kind, "trigger") == 0 ? PLAN_TRIGGER : PLAN_WARP;
        t->dir = dir[0] == 'U' ? DIR_NORTH : dir[0] == 'D' ? DIR_SOUTH : dir[0] == 'L' ? DIR_WEST : DIR_EAST;
        t->group = group;
        t->num = num;
        t->x = x;
        t->y = y;
        t->arg = arg;
        sPlan.count++;
    }
    fclose(file);
    sPlan.phase = sPlan.count > 0 ? PLAN_START : PLAN_OFF;
    TestLog("Battle env test: plan: %d targets from line %d of %s", sPlan.count, sPlan.first, path);
}

static bool PlanFieldFree(void)
{
    return gMain.callback2 == CB2_Overworld && !gMain.inBattle && !ArePlayerFieldControlsLocked()
        && !gPaletteFade.active && !ScriptContext_IsEnabled();
}

/* The trainers a script fights if it starts with trainerbattle (as a trainer object's always does). */
static bool PlanScriptTrainers(const u8 *script, u16 *a, u16 *b)
{
    const TrainerBattleParameter *params;

    if (script == NULL || script[0] != SCR_OP_TRAINERBATTLE)
        return false;
    params = (const TrainerBattleParameter *)(script + TRAINERBATTLE_OPCODE_OFFSET);
    *a = params->params.opponentA;
    *b = params->params.opponentB;
    return true;
}

void DioramaBattleEnvTest_OnAssert(const char *fmt, va_list va)
{
    char text[256];

    SDL_vsnprintf(text, sizeof(text), fmt, va);
    TestLog("Battle env test: assert %s", text);
    if (sPlan.phase != PLAN_OFF && sPlan.phase != PLAN_FINISHED && sPlan.next < sPlan.count)
        TestLog("Battle env test: plan target %d %s hang assert %s", sPlan.first + sPlan.next,
                sPlanTargets[sPlan.next].key, text);
    if (s.exitAfter)
    {
        /* the crash screen waits for START: a driven game ends here instead (a plan's hang: its next target goes on) */
        if (sLogFile != NULL)
            fclose(sLogFile);
        fflush(NULL);
        _exit(sPlan.phase != PLAN_OFF ? 7 : 8);
    }
}

bool DioramaBattleEnvTest_TakeWarpCoords(s16 *x, s16 *y)
{
    if (!sPlan.warpPending)
        return false;
    sPlan.warpPending = false;
    *x = sPlan.warpX;
    *y = sPlan.warpY;
    return true;
}

/* What a target's run counts, from nothing. */
static void PlanResetRun(void)
{
    sPlan.frames = sPlan.settle = sPlan.pressLeft = sPlan.gapLeft = sPlan.presses = 0;
    sPlan.script = sPlan.inBattle = sPlan.mapChanged = sPlan.byItself = false;
    sPlan.battles = 0;
    sPlan.outcome = sPlan.trainerA = sPlan.trainerB = -1;
    sPlan.hangFrames = sPlan.budget;
    sPlan.lastProgress = 0;
    sPlan.progress = 0;
    sPlan.rng = (u32)(sPlan.first + sPlan.next) * 2654435761u + 1;
    sPlan.ending = false;
    sPlan.lastVblank = gMain.vblankCounter1;
    sPlan.escapeChoice = sPlan.escapeDowns = sPlan.menuUp = 0;
    sPlan.held = 0;
}

/* Game thread, the field free: the target's map set up, then the warp to its place. */
static void PlanGo(void)
{
    const struct PlanTarget *t = &sPlanTargets[sPlan.next];
    const struct MapEvents *events = Overworld_GetMapHeaderByGroupAndId(t->group, t->num)->events;
    u16 a, b, targetA = 0, targetB = 0;

    sPlan.trainerId = sPlan.ownFlag = 0;
    for (u32 i = 0; events != NULL && i < events->objectEventCount; i++)
    {
        const struct ObjectEventTemplate *o = &events->objectEvents[i];
        bool target = t->kind == PLAN_TALK && o->localId == t->arg;

        if (o->kind != OBJ_KIND_NORMAL)
            continue;
        if (target && o->flagId != 0)
        {
            /* an item ball's flag is its own (taken): put back, as on a fresh save. Any other object's hide flag is
               the story's: left as the save has it (shown out of its story step, a rival or Steven runs a script
               for another time), a set one logged as absent */
            sPlan.ownFlag = o->flagId;
            if (o->graphicsId == OBJ_EVENT_GFX_ITEM_BALL)
                FlagClear(o->flagId);
        }
        if (!PlanScriptTrainers(o->script, &a, &b))
            continue;
        if (target)
        {
            targetA = a;
            targetB = b;
        }
        else if (o->trainerType != TRAINER_TYPE_NONE)
        {
            SetTrainerFlag(a);
        }
    }
    /* After the others: twins (Gina and Mia) are two objects of one double battle's trainer. */
    if (targetA != 0)
    {
        sPlan.trainerId = targetA;
        ClearTrainerFlag(targetA);
        if (targetB != 0 && targetB != 0xFFFF)
            ClearTrainerFlag(targetB);
    }
    if (t->kind == PLAN_HIDDEN && events != NULL && t->arg < events->bgEventCount)
    {
        sPlan.ownFlag = FLAG_HIDDEN_ITEMS_START + events->bgEvents[t->arg].bgUnion.hiddenItem.hiddenItemId;
        FlagClear(sPlan.ownFlag);
    }
    HealPlayerParty();
    VarSet(VAR_REPEL_STEP_COUNT, 250);  /* no wild Pokemon below the party's level 100: a step is the target's alone */
    sPlan.money = GetMoney(&gSaveBlock1Ptr->money);
    for (u32 i = 0; i < ITEMS_COUNT; i++)
        sPlan.bagBefore[i] = CountTotalItemQuantityInBag(i);
    PlanResetRun();
    TestLog("Battle env test: plan target %d %s begin", sPlan.first + sPlan.next, t->key);
    /* the place itself through DioramaBattleEnvTest_TakeWarpCoords: a warp's own coordinates are s8 (Route 123 is 140
       wide, Route 119 140 high; past 127 the game put the player in the map's middle) */
    sPlan.warpX = t->x;
    sPlan.warpY = t->y;
    sPlan.warpPending = true;
    SetWarpDestination(t->group, t->num, WARP_ID_NONE, 0, 0);
    DoWarp();
    ResetInitialPlayerAvatarState();
    sPlan.phase = PLAN_ARRIVE;
}

/* Game thread: arrived and the field free - the target object back at its place and still, the player facing it. */
static void PlanArrive(void)
{
    const struct PlanTarget *t = &sPlanTargets[sPlan.next];
    const struct MapEvents *events = gMapHeader.events;
    u8 player = gPlayerAvatar.objectEventId;

    if (t->kind == PLAN_TALK && events != NULL)
    {
        u8 objectId;
        u16 hideFlag = 0;
        const struct ObjectEventTemplate *own = NULL;

        for (u32 i = 0; i < events->objectEventCount; i++)
            if (events->objectEvents[i].localId == t->arg && events->objectEvents[i].kind == OBJ_KIND_NORMAL)
            {
                own = &events->objectEvents[i];
                hideFlag = own->flagId;
                TryMoveObjectEventToMapCoords(t->arg, t->num, t->group, own->x, own->y);  /* it adds MAP_OFFSET itself */
            }
        /* Beside the player, so in view: not there with its flag set means the game hides it now (a story flag, the
           map's script); not there because its map's script put it elsewhere for this story step (setobjectxyperm on
           the map's saved copy of its place: the Weather Institute's people while Team Aqua holds it) is "moved";
           not there with nothing hiding or moving it is a fault of its own ("missing"). */
        if (TryGetObjectEventIdByLocalIdAndMap(t->arg, t->num, t->group, &objectId))
        {
            const struct ObjectEventTemplate *saved = NULL;

            for (u32 i = 0; i < ARRAY_COUNT(gSaveBlock1Ptr->objectEventTemplates); i++)
                if (gSaveBlock1Ptr->objectEventTemplates[i].localId == t->arg)
                    saved = &gSaveBlock1Ptr->objectEventTemplates[i];
            if (hideFlag != 0 && FlagGet(hideFlag))
                TestLog("Battle env test: plan target %d %s absent flag %d", sPlan.first + sPlan.next, t->key, hideFlag);
            else if (own != NULL && saved != NULL && (saved->x != own->x || saved->y != own->y))
                TestLog("Battle env test: plan target %d %s moved to %d,%d from %d,%d", sPlan.first + sPlan.next,
                        t->key, saved->x, saved->y, own->x, own->y);
            else
                TestLog("Battle env test: plan target %d %s missing flag %d", sPlan.first + sPlan.next, t->key,
                        hideFlag);
        }
        FreezeObjectEventsExceptOne(player);
    }
    ObjectEventTurn(&gObjectEvents[player], t->dir);
    {
        /* the player's elevation from the tile it stands on: what A searches and a warp mat matches (the game keeps
           the last one where its tile is a multi-level one); logged when it was another */
        struct ObjectEvent *p = &gObjectEvents[player];
        u8 elevation = MapGridGetElevationAt(p->currentCoords.x, p->currentCoords.y);

        if (elevation != ELEVATION_MULTI_LEVEL && elevation != ELEVATION_TRANSITION && p->previousElevation != elevation)
        {
            TestLog("Battle env test: plan target %d %s elevation %d %d, its tile %d", sPlan.first + sPlan.next, t->key,
                    p->currentElevation, p->previousElevation, elevation);
            p->currentElevation = p->previousElevation = elevation;
        }
    }
    if (t->kind == PLAN_WARP && events != NULL && t->arg < events->warpCount)
    {
        /* a door its map's script keeps shut until the story opens it (a sealed cave, Mirage Tower on another day, a
           locked cabin): its tile solid, or not its layout's own tile now */
        s32 x = events->warps[t->arg].x, y = events->warps[t->arg].y;
        /* the map's own layout: a script may load another in its place (Route 111 without the Mirage Tower) */
        const struct MapLayout *layout = Overworld_GetMapHeaderByGroupAndId(t->group, t->num)->mapLayout;
        bool32 replaced = layout != NULL && x >= 0 && y >= 0 && x < layout->width && y < layout->height
                       && (layout->map[x + y * layout->width] & MAPGRID_METATILE_ID_MASK)
                          != MapGridGetMetatileIdAt(x + MAP_OFFSET, y + MAP_OFFSET);

        if (MapGridGetCollisionAt(x + MAP_OFFSET, y + MAP_OFFSET) != 0 || replaced)
            TestLog("Battle env test: plan target %d %s closed", sPlan.first + sPlan.next, t->key);
        /* someone on the player's own tile or on the warp itself in this state (a story's guard standing where the
           check puts the player down): logged, so a step that then fails is the story's to allow, not a fault */
        for (u32 i = 0; i < OBJECT_EVENTS_COUNT; i++)
        {
            const struct ObjectEvent *o = &gObjectEvents[i];
            const struct ObjectEvent *me = &gObjectEvents[player];

            if (!o->active || i == player || o->invisible)
                continue;
            if ((o->currentCoords.x == me->currentCoords.x && o->currentCoords.y == me->currentCoords.y)
             || (o->currentCoords.x == x + MAP_OFFSET && o->currentCoords.y == y + MAP_OFFSET))
            {
                TestLog("Battle env test: plan target %d %s guarded by object %d", sPlan.first + sPlan.next, t->key,
                        o->localId);
                break;
            }
        }
    }
    if (t->kind == PLAN_TRIGGER && events != NULL && t->arg < events->coordEventCount)
    {
        /* a coordinate trigger runs its script when its variable holds its value (a weather one: always) */
        const struct CoordEvent *c = &events->coordEvents[t->arg];

        TestLog("Battle env test: plan target %d %s trigger var %d is %d wants %d", sPlan.first + sPlan.next, t->key,
                c->trigger, c->trigger != 0 ? VarGet(c->trigger) : -1, c->index);
    }
    sPlan.scriptStarts = gDioramaBattleEnvTestScriptStarts;
    sPlan.mapGroup = gSaveBlock1Ptr->location.mapGroup;
    sPlan.mapNum = gSaveBlock1Ptr->location.mapNum;
    TestLog("Battle env test: plan target %d %s at map %d.%d %d,%d facing %d", sPlan.first + sPlan.next, t->key,
            sPlan.mapGroup, sPlan.mapNum, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, t->dir);
    sPlan.settle = 0;
    sPlan.phase = PLAN_ACT;
}

/* Game thread, the field free again: what the target did. */
static void PlanDone(void)
{
    const struct PlanTarget *t = &sPlanTargets[sPlan.next];
    char bag[200] = "";
    size_t used = 0;

    for (u32 i = 1; i < ITEMS_COUNT && used < sizeof(bag) - 24; i++)
    {
        int now = CountTotalItemQuantityInBag(i);

        if (now != sPlan.bagBefore[i])
            used += SDL_snprintf(bag + used, sizeof(bag) - used, "%s%u:%+d", used ? "," : "", i, now - sPlan.bagBefore[i]);
    }
    /* a trigger's weather and variable after it: a TRIGGER_RUN_IMMEDIATELY one runs inside the step (its weather is
       what shows), and a script with no effect on the screen runs to its end inside the step too
       (RunScriptImmediatelyUntilEffect: Ever Grande's arrival trigger sets a flag and its variable) */
    if (t->kind == PLAN_TRIGGER)
    {
        const struct MapEvents *events = gMapHeader.events;
        int var = events != NULL && t->arg < events->coordEventCount && events->coordEvents[t->arg].trigger != 0
                ? VarGet(events->coordEvents[t->arg].trigger) : -1;

        used += SDL_snprintf(bag + used, sizeof(bag) - used, "%s weather %d var %d", used ? "" : "-",
                             GetSavedWeather(), var);
    }
    TestLog("Battle env test: plan target %d %s end frames %d script %d battles %d outcome %d trainers %d %d "
            "by_itself %d map %d.%d %d,%d money %u %u beaten %d own %d bag %s", sPlan.first + sPlan.next, t->key,
            sPlan.frames, sPlan.script, sPlan.battles, sPlan.outcome, sPlan.trainerA, sPlan.trainerB, sPlan.byItself,
            gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->pos.x,
            gSaveBlock1Ptr->pos.y, sPlan.money, GetMoney(&gSaveBlock1Ptr->money),
            sPlan.trainerId ? (int)FlagGet(TRAINER_FLAGS_START + sPlan.trainerId) : -1,
            sPlan.ownFlag ? (int)FlagGet(sPlan.ownFlag) : -1, used ? bag : "-");
    UnfreezeObjectEvents();
    sPlan.again = false;
    if (++sPlan.next >= sPlan.count)
    {
        TestLog("Battle env test: plan done (%d targets)", sPlan.count);
        sPlan.phase = PLAN_FINISHED;
        if (s.exitAfter)
        {
            if (sLogFile != NULL)
                fclose(sLogFile);
            fflush(NULL);
            _exit(0);
        }
        return;
    }
    sPlan.settle = 0;
    sPlan.phase = PLAN_GO;
}

static void PlanFieldTick(void)
{
    switch (sPlan.phase)
    {
    case PLAN_START:
        /* the test party (PKMN_TEST_PARTY; at level 100 with PKMN_TEST_STRONG) and bag, once */
        SetUpTestParty();
        if (!s.bagDone)
            SetUpTestBag();
        if (s.strong)
            StrengthenParty();
        /* a player's own options, in memory: fast text and no move animations (the long scenes and battles shorter;
           the battle effects have checks of their own) */
        gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;
        gSaveBlock2Ptr->optionsBattleSceneOff = TRUE;
        sPlan.settle = 0;
        sPlan.phase = PLAN_GO;
        break;
    case PLAN_GO:
        if (!PlanFieldFree())
            sPlan.settle = 0;
        else if (++sPlan.settle >= 10)
            PlanGo();
        break;
    case PLAN_ARRIVE:
        if (!PlanFieldFree() || gSaveBlock1Ptr->location.mapGroup != sPlanTargets[sPlan.next].group
         || gSaveBlock1Ptr->location.mapNum != sPlanTargets[sPlan.next].num)
            sPlan.settle = 0;
        else if (++sPlan.settle >= 8)
            PlanArrive();
        break;
    case PLAN_DONE:
        if (!PlanFieldFree())
            break;
        if (sPlan.byItself && !sPlan.again && sPlan.battles == 0 && !sPlan.mapChanged)
        {
            /* a scene of the map's own ran first (its variable said it was due) and the target never had its turn:
               the target once more, the scene done - from where the player still stands (a scene that runs on each
               entry: the Abandoned Ship's hidden rooms' sparkle), else from a warp there again */
            const struct PlanTarget *t = &sPlanTargets[sPlan.next];
            bool stays = gSaveBlock1Ptr->pos.x == t->x && gSaveBlock1Ptr->pos.y == t->y;

            TestLog("Battle env test: plan target %d %s again (a scene ran first)%s", sPlan.first + sPlan.next, t->key,
                    stays ? "" : ", warped back");
            UnfreezeObjectEvents();
            sPlan.again = true;
            sPlan.settle = 0;
            if (stays)
            {
                PlanResetRun();
                sPlan.phase = PLAN_ARRIVE;
            }
            else
            {
                sPlan.phase = PLAN_GO;
            }
            break;
        }
        PlanDone();
        break;
    }
}

/* Render thread (the game thread parked at the frame's end), in a plan's battle: each of the player's battlers' move
   cursors on its most effective move against the first foe still standing, so the A that the move menu takes picks it
   (one move the foe resists can be out-healed for good: Champion Wallace's Water team, Milotic's Recover). */
static void PlanPickMoves(void)
{
    u32 foe = gBattleMons[B_POSITION_OPPONENT_LEFT].hp != 0 ? B_POSITION_OPPONENT_LEFT : B_POSITION_OPPONENT_RIGHT;

    if (foe >= gBattlersCount || gBattleMons[foe].hp == 0)
        return;
    for (u32 battler = B_POSITION_PLAYER_LEFT; battler < gBattlersCount; battler += 2)
    {
        s32 best = -1;
        u32 pick = 0;

        for (u32 i = 0; i < MAX_MON_MOVES; i++)
        {
            enum Move move = gBattleMons[battler].moves[i];
            enum Type type = GetBattleMoveType(move);
            enum Ability ability = gBattleMons[foe].ability;
            s32 effect;

            if (move == MOVE_NONE || gBattleMons[battler].pp[i] == 0)
                continue;
            effect = CalcPartyMonTypeEffectivenessMultiplier(move, gBattleMons[foe].species, ability);
            if ((type == TYPE_ELECTRIC && (ability == ABILITY_VOLT_ABSORB || ability == ABILITY_LIGHTNING_ROD
                                           || ability == ABILITY_MOTOR_DRIVE))
             || (type == TYPE_GRASS && ability == ABILITY_SAP_SIPPER)
             || (type == TYPE_FIRE && ability == ABILITY_FLASH_FIRE))
                effect = 0;     /* the foe's ability takes it in */
            if (effect > best)
            {
                best = effect;
                pick = i;
            }
        }
        gMoveSelectionCursor[battler] = pick;
    }
}

/* Render thread (the game thread parked at the frame's end): the keys a player would press for the target. */
static unsigned short PlanKeys(void)
{
    const struct PlanTarget *t = &sPlanTargets[sPlan.next];
    unsigned short button;

    if (sPlan.phase != sPlan.lastPhase)
    {
        sPlan.lastPhase = sPlan.phase;
        sPlan.phaseFrames = 0;
    }
    sPlan.phaseFrames++;
    /* the warp, the arrival or the end that never comes (a map that does not load, a field never free) */
    if ((sPlan.phase == PLAN_GO || sPlan.phase == PLAN_ARRIVE || sPlan.phase == PLAN_DONE)
     && sPlan.phaseFrames > sPlan.budget)
    {
        TestLog("Battle env test: plan target %d %s hang in phase %d frames %d script %d in battle %d map %d.%d %d,%d",
                sPlan.first + sPlan.next, t->key, sPlan.phase, sPlan.phaseFrames, ScriptContext_IsEnabled(),
                gMain.inBattle, gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
                gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
        RequestShot(99, 1, "a hang");
        sPlan.settle = 0;
        sPlan.phase = PLAN_HANG;
        return 0;
    }
    /* arrived, and something began before A: a trainer who saw the player, a map's own scene - answered as any */
    if (sPlan.phase == PLAN_ARRIVE && sPlan.phaseFrames > 30 && (ScriptContext_IsEnabled() || gMain.inBattle)
     && gSaveBlock1Ptr->location.mapGroup == t->group && gSaveBlock1Ptr->location.mapNum == t->num)
    {
        sPlan.mapGroup = t->group;
        sPlan.mapNum = t->num;
        sPlan.byItself = true;
        TestLog("Battle env test: plan target %d %s at map %d.%d %d,%d facing %d (it began by itself)",
                sPlan.first + sPlan.next, t->key, sPlan.mapGroup, sPlan.mapNum, gSaveBlock1Ptr->pos.x,
                gSaveBlock1Ptr->pos.y, t->dir);
        sPlan.settle = 0;
        sPlan.phase = PLAN_RUN;
        return 0;
    }
    if (sPlan.phase == PLAN_HANG)
    {
        /* after its picture (the render thread writes it some frames later; at most 240 frames) */
        if (++sPlan.settle >= 240 || (sPlan.settle >= 40 && (s.megaShotStem[0] == '\0'
                                                             || SDL_AtomicGet(&s.megaShotState) == 3)))
        {
            if (sLogFile != NULL)
                fclose(sLogFile);
            fflush(NULL);
            _exit(7);
        }
        return 0;
    }
    if (sPlan.phase == PLAN_ACT)
    {
        /* a moment for the turn to show, then A (a step onto a warp: its direction held) */
        if (++sPlan.settle < 6)
        {
            if (sPlan.settle == 3 && sPlan.shots)
                RequestShot((sPlan.first + sPlan.next) % 98, 1, "the target faced");    /* PKMN_TEST_PLAN_SHOTS */
            return 0;
        }
        if (t->kind == PLAN_WARP || t->kind == PLAN_TRIGGER)
        {
            /* a trigger: one step (a walk step is 16 frames; held longer, a second step passes a trigger that is not
               due - Route 116's into Rusturf Tunnel) */
            if (sPlan.settle < 6 + (t->kind == PLAN_TRIGGER ? 12 : 24))
                return t->dir == DIR_NORTH ? DPAD_UP : t->dir == DIR_SOUTH ? DPAD_DOWN
                     : t->dir == DIR_WEST ? DPAD_LEFT : DPAD_RIGHT;
        }
        else if (sPlan.settle < 6 + AUTOPILOT_HOLD)
        {
            return A_BUTTON;
        }
        sPlan.settle = 0;
        sPlan.phase = PLAN_RUN;
        return 0;
    }
    if (sPlan.phase != PLAN_RUN)
        return 0;
    /* The game's ending (the Champion beaten): the Hall of Fame room, its screen and the credits, which end in a reset
       to the title - the target's last act. Logged, the time it takes allowed, and at the reset the process ends: the
       tool goes on at the next target, which starts from the save again. */
    if (!sPlan.ending && gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(MAP_EVER_GRANDE_CITY_HALL_OF_FAME)
     && gSaveBlock1Ptr->location.mapNum == MAP_NUM(MAP_EVER_GRANDE_CITY_HALL_OF_FAME))
    {
        sPlan.ending = true;
        sPlan.hangFrames += 24000;
        TestLog("Battle env test: plan target %d %s ending (the Hall of Fame, the credits)", sPlan.first + sPlan.next,
                t->key);
    }
    if (sPlan.ending && gMain.vblankCounter1 < sPlan.lastVblank)
    {
        TestLog("Battle env test: plan target %d %s ending done (the game reset to its title)", sPlan.first + sPlan.next,
                t->key);
        if (sLogFile != NULL)
            fclose(sLogFile);
        fflush(NULL);
        _exit(7);
    }
    sPlan.lastVblank = gMain.vblankCounter1;
    sPlan.frames++;
    {
        /* what a scene that goes on changes now and then: the player's place and facing, the map, the screen, the
           script, the message box and its text, a battle's turns and HP (the Devon president's scene, Scott's
           welcome and Steven's six run past the budget) */
        const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];
        u32 progress = (player->currentCoords.x * 31u + player->currentCoords.y) * 31u + player->facingDirection;

        progress = progress * 31u + gSaveBlock1Ptr->location.mapGroup * 257u + gSaveBlock1Ptr->location.mapNum;
        progress = progress * 31u + (u32)(uintptr_t)gMain.callback2;
        progress = progress * 31u + ScriptContext_IsEnabled() * 2u + IsFieldMessageBoxHidden() * 4u + gMain.inBattle;
        progress = progress * 31u + (u32)(uintptr_t)DioramaBattleEnvTest_TextPosition();  /* a long speech printing */
        if (gMain.inBattle)     /* a long battle: its turns and every battler's HP */
        {
            progress = progress * 31u + gBattleTurnCounter;
            for (u32 i = 0; i < gBattlersCount && i < MAX_BATTLERS_COUNT; i++)
                progress = progress * 31u + gBattleMons[i].hp;
        }
        if (progress != sPlan.progress)
        {
            sPlan.progress = progress;
            sPlan.lastProgress = sPlan.frames;
        }
    }
    /* the budget gone and nothing moving on for a while, or four budgets gone */
    if (sPlan.frames > sPlan.hangFrames
     && (sPlan.frames - sPlan.lastProgress > PLAN_STALL || sPlan.frames > sPlan.hangFrames + 3 * sPlan.budget))
    {
        int mode, status;
        const u8 *at = ScriptContext_DebugPosition(&mode, &status);   /* where the field's script waits */

        TestLog("Battle env test: plan target %d %s hang frames %d script %d battles %d in battle %d map %d.%d %d,%d "
                "callback %p script at %p mode %d", sPlan.first + sPlan.next, t->key, sPlan.frames,
                ScriptContext_IsEnabled(), sPlan.battles, gMain.inBattle, gSaveBlock1Ptr->location.mapGroup,
                gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y,
                (void *)gMain.callback2, (const void *)at, mode);
        RequestShot(99, 1, "a hang");
        sPlan.settle = 0;
        sPlan.phase = PLAN_HANG;
        return 0;
    }
    if (gMain.inBattle)
    {
        if (!sPlan.inBattle)
        {
            sPlan.inBattle = true;
            sPlan.battles++;
            sPlan.hangFrames += 10800;
            sPlan.trainerA = TRAINER_BATTLE_PARAM.opponentA;
            sPlan.trainerB = TRAINER_BATTLE_PARAM.opponentB;
            TestLog("Battle env test: plan target %d %s battle %d type %08x trainers %d %d", sPlan.first + sPlan.next,
                    t->key, sPlan.battles, gBattleTypeFlags, sPlan.trainerA, sPlan.trainerB);
        }
        button = A_BUTTON;      /* the most effective move on the first target, every line */
        PlanPickMoves();
        /* a Pokemon fainted: the battle's party menu up (it starts on the fainted one) - DOWN while its cursor is on a
           fainted Pokemon, then A (and A on its SHIFT); a fixed key cycle locked into a loop with the "has no energy
           left to battle" message (Steven's six, Meteor Falls) */
        if (gMain.callback2 != BattleMainCB2 && gPartyMenu.menuType == PARTY_MENU_TYPE_IN_BATTLE
         && gPartyMenu.slotId >= 0 && gPartyMenu.slotId < PARTY_SIZE
         && GetMonData(&gParties[B_TRAINER_PLAYER][gPartyMenu.slotId], MON_DATA_HP) == 0)
            button = DPAD_DOWN;
    }
    else
    {
        if (sPlan.inBattle)
        {
            sPlan.inBattle = false;
            sPlan.outcome = gBattleOutcome;
            TestLog("Battle env test: plan target %d %s battle %d over outcome %d", sPlan.first + sPlan.next, t->key,
                    sPlan.battles, gBattleOutcome);
        }
        if (ScriptContext_IsEnabled())
            sPlan.script = true;
        /* or a script started since the arrival: a quick one starts and ends inside one frame (Ever Grande's
           arrival trigger sets a flag and a variable) */
        if (gDioramaBattleEnvTestScriptStarts != sPlan.scriptStarts)
            sPlan.script = true;
        if (!sPlan.mapChanged && (gSaveBlock1Ptr->location.mapGroup != sPlan.mapGroup
                                  || gSaveBlock1Ptr->location.mapNum != sPlan.mapNum))
        {
            sPlan.mapChanged = true;
            TestLog("Battle env test: plan target %d %s map now %d.%d", sPlan.first + sPlan.next, t->key,
                    gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
        }
        if (PlanFieldFree())
        {
            sPlan.pressLeft = sPlan.gapLeft = 0;
            if (++sPlan.settle >= PLAN_SETTLE && (sPlan.script || sPlan.battles || sPlan.mapChanged
                                                  || sPlan.frames > 3 * PLAN_SETTLE))
                sPlan.phase = PLAN_DONE;
            return 0;
        }
        sPlan.settle = 0;
        /* B through the lines and questions; a trainer's YES until its battle begins (the Winstrates ask first) */
        button = t->fight && sPlan.battles == 0 && sPlan.frames <= 1800 ? A_BUTTON : B_BUTTON;
        if (DioramaBattleEnvTest_NamingScreenUp())
        {
            /* a naming screen (a nickname offered for a Pokemon received, the question's YES taken after half a
               minute): the name kept - START puts the cursor on OK, then A */
            button = sPlan.presses % 2 == 0 ? START_BUTTON : A_BUTTON;
        }
        else if (DioramaBattleEnvTest_PokenavTutorialKey() != 0)
        {
            /* the PokéNav tutorial (Rustboro's scientist once the PokéNav is given): MATCH CALL, a call, then out */
            button = DioramaBattleEnvTest_PokenavTutorialKey();
        }
        else if (DioramaBattleEnvTest_ForcedStartMenuUp())
        {
            /* its forced START menu: POKéNAV, the fourth row (any other row, or B, asks again) */
            button = Menu_GetCursorPos() < 3 ? DPAD_DOWN : Menu_GetCursorPos() > 3 ? DPAD_UP : A_BUTTON;
        }
        else if (DioramaBattleEnvTest_EasyChatState() >= 0)
        {
            /* an Easy Chat screen (an interviewer, a phrase asked for): B until "Stop giving a comment?" is up, then
               YES above the NO it starts on - a key cycle can lock into a loop between its keyboard and its buttons */
            button = DioramaBattleEnvTest_EasyChatState() != 1 ? B_BUTTON
                   : Menu_GetCursorPos() == 0 ? A_BUTTON : DPAD_UP;
        }
        else if (sPlan.frames > 1800 && DioramaBattleEnvTest_ScriptMenuUp())
        {
            /* after half a minute, a script's menu: each choice in turn (DOWN to it, A), the next one the next time a
               menu is up - a menu that asks again until the right one is picked (Wallace's "Where's Rayquaza?"), a
               YES above a NO, an EXIT at a menu's end (the info desks). Chosen once per press, as the press starts (this
               runs every frame: the turn moved on in the hold and the gap too), and not in the menu's first frames (a
               YES/NO takes no input for its first 5: a press then is lost, its turn with it - the Fan Club
               interviewer's "Have you forgotten?" answered NO again and again) */
            if (sPlan.menuUp++ < 8)
                button = 0;
            else if (sPlan.pressLeft > 0 || sPlan.gapLeft > 0)
                button = sPlan.held;
            else if (Menu_GetCursorPos() < sPlan.escapeChoice && sPlan.escapeDowns < 8)
            {
                button = sPlan.held = DPAD_DOWN;
                sPlan.escapeDowns++;
            }
            else
            {
                button = sPlan.held = A_BUTTON;
                sPlan.escapeDowns = 0;
                sPlan.escapeChoice = (sPlan.escapeChoice + 1) % 8;
            }
        }
        else if (sPlan.frames > 1800)
        {
            /* after half a minute, otherwise keys drawn at random (seeded: the same run each time) from B, A, DOWN and
               UP, one per press: a choice that has no NO, a list's EXIT where B does nothing - a fixed cycle can lock
               into a loop */
            static const unsigned short sEscape[] = {B_BUTTON, B_BUTTON, B_BUTTON, A_BUTTON, A_BUTTON, DPAD_DOWN,
                DPAD_DOWN, DPAD_UP};

            if (sPlan.pressLeft > 0 || sPlan.gapLeft > 0)
                button = sPlan.held;
            else
            {
                sPlan.rng = sPlan.rng * 1103515245u + 12345u;
                button = sPlan.held = sEscape[(sPlan.rng >> 16) % ARRAY_COUNT(sEscape)];
            }
        }
        if (!DioramaBattleEnvTest_ScriptMenuUp())
            sPlan.menuUp = 0;
    }
    if (sPlan.pressLeft > 0)
    {
        sPlan.pressLeft--;
        return button;
    }
    if (sPlan.gapLeft > 0)
    {
        sPlan.gapLeft--;
        return 0;
    }
    sPlan.presses++;
    sPlan.pressLeft = AUTOPILOT_HOLD - 1;
    sPlan.gapLeft = gMain.inBattle ? AUTOPILOT_HOLD * 2 : 12;
    return button;
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
    if (sPlan.phase != PLAN_OFF)
    {
        PlanFieldTick();     /* PKMN_TEST_PLAN: the plan alone drives the field */
        return;
    }
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
        if (s.towerFloor > 0)
            TowerLogEnd();
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

/* What a move does against the opposing Pokemon as the battle has it (TowerWinTurn's choice): its power times the type
 * chart's multiplier with the Pokemon's Ability and held item (Levitate or an Air Balloon against Ground, Wonder Guard:
 * nothing; floor 88's Gengar stalled a run that chose Earthquake on the chart alone), nothing when its Ability takes the
 * move's type (Flash Fire, Well-Baked Body, Earth Eater). Reads the battle only (no flags updated). */
static u32 TowerWinScore(u32 player, u32 foe, enum Move move)
{
    struct DamageContext ctx = {0};
    enum Ability ability = gBattleMons[foe].ability;

    ctx.battlerAtk = player;
    ctx.battlerDef = foe;
    ctx.move = ctx.chosenMove = ctx.baseMove = move;
    ctx.moveType = GetMoveType(move);
    ctx.updateFlags = FALSE;
    ctx.abilities[player] = gBattleMons[player].ability;
    ctx.abilities[foe] = ability;
    ctx.holdEffects[player] = GetItemHoldEffect(gBattleMons[player].item);
    ctx.holdEffects[foe] = GetItemHoldEffect(gBattleMons[foe].item);
    if ((ctx.moveType == TYPE_FIRE && (ability == ABILITY_FLASH_FIRE || ability == ABILITY_WELL_BAKED_BODY))
     || (ctx.moveType == TYPE_GROUND && ability == ABILITY_EARTH_EATER))
        return 0;
    return GetMovePower(move) * CalcTypeEffectivenessMultiplier(&ctx) / UQ_4_12(1.0);
}

/* PKMN_TEST_TOWER_WIN (a test advantage, in memory only, for a Tower floor's won run; solo Tower battles only - a Duo
 * is a link battle that both games must compute alike): at each of the player's turns the lead's HP is full again, its
 * Attack, Defense, Speed, Sp. Atk, Sp. Def and accuracy stages are +6 (a set-up opponent such as Floor 100's Curse
 * Snorlax cannot knock it out between two of its turns; one that still faints, to a critical hit, is replaced through
 * the party screen: ForcedSwitchOnFainted) and the move cursor is on its move that does most against the opposing
 * Pokemon (TowerWinScore) - or on Protect while the opposing
 * Pokemon holds a Mega Stone it has not used, so it acts and Mega Evolves (DioramaBattleEnvTest_HoldOnForMega keeps
 * one that came in during the turn from fainting first). The battle itself - the AI's turns, damage, switches, Mega
 * Evolution, the end - is the game's own. */
static void TowerWinTurn(void)
{
    static const u16 sCoverage[MAX_MON_MOVES] = {MOVE_ICE_BEAM, MOVE_FLAMETHROWER, MOVE_SHADOW_BALL, MOVE_EARTHQUAKE};
    u32 player = GetBattlerAtPosition(B_POSITION_PLAYER_LEFT), foe = GetBattlerAtPosition(B_POSITION_OPPONENT_LEFT);
    u32 i, best = 0, bestScore = 0;

    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        gBattleMons[player].moves[i] = sCoverage[i];
        gBattleMons[player].pp[i] = 30;
    }
    gBattleMons[player].hp = gBattleMons[player].maxHP;
    SetMonData(&gParties[B_TRAINER_PLAYER][gBattlerPartyIndexes[player]], MON_DATA_HP, &gBattleMons[player].maxHP);
    for (i = STAT_ATK; i <= STAT_ACC; i++)
        gBattleMons[player].statStages[i] = MAX_STAT_STAGE;
    /* An opposing Mega Stone holder that has not Mega Evolved yet: Protect this turn, so it chooses a move and Mega
     * Evolves at the turn's start (an attack first could knock it out before it ever acts) */
    if (CanMegaEvolve(foe))
    {
        gBattleMons[player].moves[MAX_MON_MOVES - 1] = MOVE_PROTECT;
        gMoveSelectionCursor[player] = MAX_MON_MOVES - 1;
        return;
    }
    for (i = 0; i < MAX_MON_MOVES; i++)
    {
        enum Move move = gBattleMons[player].moves[i];
        u32 score;

        if (move == MOVE_NONE || GetMoveCategory(move) == DAMAGE_CATEGORY_STATUS || gBattleMons[player].pp[i] == 0)
            continue;
        score = TowerWinScore(player, foe, move);
        if (score > bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    gMoveSelectionCursor[player] = best;
}

/* PKMN_TEST_TOWER_WIN, from GetAdjustedDamage (a hit that would knock the battler out): an opposing Pokemon that can
 * still Mega Evolve (its Mega Stone unused, its trainer's Mega Evolution not spent) holds on with 1 HP, as Endure
 * does, so it chooses a move and Mega Evolves before it can faint - one the AI switched in just before the test
 * Pokemon's attack fainted before it ever acted (floor 58's Absol and floor 88's Chandelure in check-full-1). Solo
 * Tower battles only. */
bool DioramaBattleEnvTest_HoldOnForMega(unsigned battler)
{
    if (!s.init || !s.towerWin || TowerLegends_ActiveMode() != TOWER_MODE_SOLO || battler >= gBattlersCount
     || GetBattlerSide(battler) != B_SIDE_OPPONENT || !CanMegaEvolve(battler))
        return false;
    TestLog("Battle env test: tower win: battler %u species %u holds on to Mega Evolve", battler,
            gBattleMons[battler].species);
    return true;
}

void DioramaBattleEnvTest_OnActionMenu(void)
{
    if (!s.init)
        Init();
    if (s.towerFloor > 0 && !s.towerLogged && TowerLegends_IsBattleActive())
        TowerLogParty();
    if (s.towerWin && TowerLegends_ActiveMode() == TOWER_MODE_SOLO)
        TowerWinTurn();
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

/* Main thread, every turn of its loop, the game thread perhaps mid-frame: a plan whose game made no frame for a
   minute (a loop inside one frame: a Battle Pike room picking a trainer with no challenge) ends as a hang, so the tool
   goes on at the next target instead of waiting out its process timeout. Reads the plan without the game parked: the
   game thread is stuck elsewhere. */
#define PLAN_NO_FRAME_MS 60000
static SDL_atomic_t sLastFrameMs;

void DioramaBattleEnvTest_Watchdog(unsigned int nowMs)
{
    unsigned int last = (unsigned int)SDL_AtomicGet(&sLastFrameMs);
    int phase = sPlan.phase, next = sPlan.next;
    // Signed: a frame stamped after nowMs was read (the game thread runs on; the stamp is odd, ms | 1) is no wait.
    // Unsigned, that difference wrapped to about 4,294,967 s and ended healthy games at once (gameplay CI run 3).
    int waited = (int)(nowMs - last);

    if (last == 0 || phase == PLAN_OFF || phase == PLAN_HANG || phase == PLAN_FINISHED || next >= sPlan.count
     || waited < PLAN_NO_FRAME_MS)
        return;
    TestLog("Battle env test: plan target %d %s hang the game made no frame for %d s", sPlan.first + next,
            sPlanTargets[next].key, waited / 1000);
    if (sLogFile != NULL)
        fclose(sLogFile);
    fflush(NULL);
    _exit(7);
}

/* The party screen of a battle's forced switch (the player's Pokemon fainted) with its cursor on a Pokemon that cannot
 * battle: the fight's A there only says so, so the autopilot presses B (that message, or nothing: the screen cannot be
 * left) and DOWN instead, then A on the next one that can (its SHIFT). */
static bool ForcedSwitchOnFainted(void)
{
    s8 slot = gPartyMenu.slotId;

    return gMain.inBattle && gMain.callback2 != BattleMainCB2
        && gPartyMenu.exitCallback == CB2_SetUpReshowBattleScreenAfterMenu
        && (gPartyMenu.action == PARTY_ACTION_SEND_OUT || gPartyMenu.action == PARTY_ACTION_CHOOSE_FAINTED_MON)
        && slot >= 0 && slot < PARTY_SIZE
        && (GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_SPECIES) == SPECIES_NONE
         || GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_HP) == 0);
}

unsigned short DioramaBattleEnvTest_FilterKeys(unsigned short keys, unsigned int nowMs)
{
    unsigned int t;

    SDL_AtomicSet(&sLastFrameMs, (int)(nowMs | 1));
    if (!s.init)
        Init();
    MegaLogFrame();
    MegaLogField();
    StarterLogField();
    TrainLogField();
    BreedLogField();
    BagLogField();
    ShotTick();
    /* PKMN_TEST_PLAN: once the autopilot brought the field up, the plan's keys alone */
    if (sPlan.phase != PLAN_OFF && SDL_AtomicGet(&s.fieldSeen) != 0)
        return keys | PlanKeys();
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
            return keys | s.fightButton;
        }
        if (s.gapLeft > 0)
        {
            s.gapLeft--;
            return keys;
        }
        s.fightButton = A_BUTTON;
        if (ForcedSwitchOnFainted())
            s.fightButton = s.partySteps++ % 2 == 0 ? B_BUTTON : DPAD_DOWN;
        s.pressLeft = AUTOPILOT_HOLD - 1;
        s.gapLeft = AUTOPILOT_HOLD * 2;
        return keys | s.fightButton;
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
