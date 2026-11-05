#if defined(DIORAMA_BATTLE_ENV_TEST) && defined(ENABLE_DIORAMA)
/*
 * The geometry probe's game side (diorama/geometry_probe.h): the tour's places, the warps there and the wait for
 * the field; a place a warp cannot show is entered through its facility instead (its keys: DioramaGeoProbe_Keys).
 * The render side (geometry_probe.c) takes the cells and the pictures and says when it is done.
 * Project-owned, test builds only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL2/SDL.h>

#include "global.h"
#include "battle_main.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "field_weather.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "pokemon.h"
#include "rtc.h"
#include "script.h"
#include "constants/battle_pike.h"
#include "constants/flags.h"
#include "constants/map_groups.h"
#include "constants/vars.h"
#include "constants/weather.h"
#include "diorama/battle_env_test.h"
#include "diorama/geometry_probe.h"
#include "geometry_probe_shared.h"
#ifdef ENABLE_NATIVE_UI
#include "native_ui/ui_engine.h"
#endif

#define GEO_MAX_PLACES 4096
#define GEO_ARRIVE_FRAMES 1800      /* game frames a place may take to give the field back */
#define GEO_RENDER_FRAMES 36000     /* game frames the render side may take (a slow software renderer) */
#define GEO_ENTER_FRAMES 36000      /* game frames a facility's way in may take (its talk, menus, rooms, a battle) */
#define GEO_MAX_STEPS 8
#define GEO_PRESS_HOLD 3            /* a press: held this many frames, then released for GEO_PRESS_GAP */
#define GEO_PRESS_GAP 9

/* What the player does on the field of one map on the way into a facility's room. */
struct GeoStep
{
    int group, num;
    char action[8];                 /* A (talk), U / D / L / R (walk), pike (the Battle Pike's room of three paths) */
};

struct GeoPlace
{
    char key[96];
    int group, num, x, y;
    /* entered through its facility ("enter" in the tour line): where the player starts, facing the attendant */
    bool enter;
    int startGroup, startNum, startX, startY;
    u8 startDir;
    int stepCount;
    struct GeoStep steps[GEO_MAX_STEPS];
};

static struct GeoPlace *sPlaces;
static int sPlaceCount, sNext, sLast, sFrames, sGapFrames;
static bool sInit, sActive, sWarpPending;
static s16 sWarpX, sWarpY;
static int sHour = 12;
static bool sKeepWeather;
struct GeoProbeShared gGeoProbe;

/* The way in (game thread in the field's frames; its keys on the render thread while the game thread is parked: one
 * after the other, never at once) */
static struct
{
    int frames;                     /* game frames since the start's field was free */
    int pressLeft, gapLeft;         /* the press in progress */
    u16 held;
    int lastGroup, lastNum;         /* the map last seen (each change logged) */
    int pikeDoor;                   /* the Battle Pike's door chosen in this visit of its room of three paths (-1 none) */
    int pikeLeg;                    /* how far along the way to it */
    u8 partySkip;                   /* party slots whose selection menu has no ENTER */
} sEnter;

static bool ParseEntry(struct GeoPlace *p, const char *rest)
{
    char dir = 0, steps[256];
    char *save = NULL;

    if (sscanf(rest, " enter %d.%d:%d,%d:%c %255s", &p->startGroup, &p->startNum, &p->startX, &p->startY, &dir,
               steps) != 6)
        return false;
    p->startDir = dir == 'U' ? DIR_NORTH : dir == 'D' ? DIR_SOUTH : dir == 'L' ? DIR_WEST : DIR_EAST;
    for (char *token = SDL_strtokr(steps, ",", &save); token != NULL && p->stepCount < GEO_MAX_STEPS;
         token = SDL_strtokr(NULL, ",", &save))
    {
        struct GeoStep *step = &p->steps[p->stepCount];

        if (sscanf(token, "%d.%d=%7s", &step->group, &step->num, step->action) == 3)
            p->stepCount++;
    }
    p->enter = true;
    return true;
}

static void Load(void)
{
    const char *path = SDL_getenv("PKMN_TEST_GEO_TOUR");
    const char *value;
    char line[512];
    FILE *file;

    sInit = true;
    if (path == NULL || path[0] == '\0')
        return;
    file = fopen(path, "r");
    if (file == NULL)
    {
        SDL_Log("Geometry probe: cannot read %s", path);
        return;
    }
    sPlaces = calloc(GEO_MAX_PLACES, sizeof(*sPlaces));
    while (sPlaces != NULL && sPlaceCount < GEO_MAX_PLACES && fgets(line, sizeof(line), file) != NULL)
    {
        struct GeoPlace *p = &sPlaces[sPlaceCount];
        int used = 0;

        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (sscanf(line, "%95s %d %d %d %d%n", p->key, &p->group, &p->num, &p->x, &p->y, &used) == 5)
        {
            ParseEntry(p, line + used);
            sPlaceCount++;
        }
    }
    fclose(file);
    if ((value = SDL_getenv("PKMN_TEST_GEO_HOUR")) != NULL && value[0] != '\0')
        sHour = atoi(value);
    sKeepWeather = (value = SDL_getenv("PKMN_TEST_GEO_WEATHER")) != NULL && strcmp(value, "keep") == 0;
    value = SDL_getenv("PKMN_TEST_GEO_FIRST");
    sNext = value != NULL ? atoi(value) : 0;
    value = SDL_getenv("PKMN_TEST_GEO_LAST");  /* the place to stop before (one process of several) */
    sLast = value != NULL && value[0] != '\0' ? atoi(value) : sPlaceCount;
    if (sLast > sPlaceCount)
        sLast = sPlaceCount;
    sActive = sPlaceCount > 0;
    SDL_AtomicSet(&gGeoProbe.phase, GEO_IDLE);
    SDL_Log("Geometry probe: %d places from %s, starting at %d", sPlaceCount, path, sNext);
}

static bool FieldFree(void)
{
    return gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !gPaletteFade.active
        && !ScriptContext_IsEnabled();
}

static bool OnMap(int group, int num)
{
    return gSaveBlock1Ptr->location.mapGroup == group && gSaveBlock1Ptr->location.mapNum == num;
}

static void Finish(void)
{
    SDL_Log("Geometry probe: tour done (to place %d of %d)", sLast, sPlaceCount);
    SDL_AtomicSet(&gGeoProbe.phase, GEO_FINISHED);
    fflush(NULL);
    _exit(0);
}

/* On the place, the field free: the cells and the pictures are the render side's now. */
static void Arrive(void)
{
    if (!sKeepWeather)
        SetCurrentAndNextWeatherNoDelay(WEATHER_NONE);
    /* the people stand still: the same picture from every camera (not the player: a surfer's blob is theirs,
       and the next warp moves them) */
    FreezeObjectEventsExceptOne(gPlayerAvatar.objectEventId);
    gGeoProbe.arrivedGroup = gSaveBlock1Ptr->location.mapGroup;
    gGeoProbe.arrivedNum = gSaveBlock1Ptr->location.mapNum;
    gGeoProbe.arrivedX = gSaveBlock1Ptr->pos.x;
    gGeoProbe.arrivedY = gSaveBlock1Ptr->pos.y;
    gGeoProbe.failure[0] = '\0';
    if (gGeoProbe.arrivedGroup != gGeoProbe.group || gGeoProbe.arrivedNum != gGeoProbe.num)
        SDL_strlcpy(gGeoProbe.failure, "a script took the player elsewhere", sizeof(gGeoProbe.failure));
    sFrames = 0;
    SDL_AtomicSet(&gGeoProbe.phase, GEO_ARRIVED);
}

/* The place was never reached: no cells and no pictures, the reason in its result line. */
static void NotReached(const char *why)
{
    gGeoProbe.arrivedGroup = gSaveBlock1Ptr->location.mapGroup;
    gGeoProbe.arrivedNum = gSaveBlock1Ptr->location.mapNum;
    gGeoProbe.arrivedX = gSaveBlock1Ptr->pos.x;
    gGeoProbe.arrivedY = gSaveBlock1Ptr->pos.y;
    SDL_snprintf(gGeoProbe.failure, sizeof(gGeoProbe.failure), "%s (on map %d.%d at %d,%d)", why,
                 gGeoProbe.arrivedGroup, gGeoProbe.arrivedNum, gGeoProbe.arrivedX, gGeoProbe.arrivedY);
    SDL_Log("Geometry probe: %s: %s", gGeoProbe.key, gGeoProbe.failure);
    gGeoProbe.noScene = 1;
    sFrames = 0;
    SDL_AtomicSet(&gGeoProbe.phase, GEO_ARRIVED);
}

static void LogMapChange(void)
{
    if (OnMap(sEnter.lastGroup, sEnter.lastNum))
        return;
    sEnter.lastGroup = gSaveBlock1Ptr->location.mapGroup;
    sEnter.lastNum = gSaveBlock1Ptr->location.mapNum;
    sEnter.pikeDoor = -1;       /* a room of three paths visited again (after a room) chooses again */
    sEnter.pikeLeg = 0;
    SDL_Log("Geometry probe: %s: on map %d.%d at %d,%d (frame %d of the way in)", gGeoProbe.key, sEnter.lastGroup,
            sEnter.lastNum, gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, sEnter.frames);
}

bool DioramaGeoProbe_FieldTick(void)
{
    int phase;

    if (!sInit)
        Load();
    if (!sActive)
        return false;
    phase = SDL_AtomicGet(&gGeoProbe.phase);
    switch (phase)
    {
    case GEO_IDLE:
        /* the field free: a place whose own script keeps it (a cutscene on arrival) ends the process, which the
           tool starts again at the next place - a warp in the middle of a script's message box broke the game heap
           (two aborts in the first tour: the text printers' blocks freed at the next map's load) */
        if (++sGapFrames < 10 || (!FieldFree() && sGapFrames < GEO_ARRIVE_FRAMES))
            return true;
        if (sNext >= sLast)
            Finish();
        if (!FieldFree())
        {
            SDL_Log("Geometry probe: the field was not free after place %d; stopping here", sNext - 1);
            fflush(NULL);
            _exit(5);
        }
        sGapFrames = 0;
        UnfreezeObjectEvents();
        {
            const struct GeoPlace *p = &sPlaces[sNext];

            SDL_strlcpy(gGeoProbe.key, p->key, sizeof(gGeoProbe.key));
            gGeoProbe.group = p->group;
            gGeoProbe.num = p->num;
            gGeoProbe.x = p->x;
            gGeoProbe.y = p->y;
            gGeoProbe.index = sNext;
            gGeoProbe.entered = p->enter;
            gGeoProbe.noScene = 0;
            gGeoProbe.failure[0] = '\0';
            SDL_Log("Geometry probe: place %d %s", sNext, p->key);  /* where a run that stopped got to */
            /* noon, the cave lit as with Flash: the shapes, not the hour (in memory only) */
            RtcCalcLocalTimeOffset(0, sHour, 0, 0);
            FlagSet(FLAG_SYS_USE_FLASH);
            if (p->enter)
            {
                /* first to the facility's start: in front of its attendant, with the party its challenge takes
                   (PKMN_TEST_GEO_PARTY: a new character of the short introduction has none) */
                SDL_Log("Geometry probe: %s: entering through its facility, from map %d.%d at %d,%d", p->key,
                        p->startGroup, p->startNum, p->startX, p->startY);
                DioramaBattleEnvTest_SetUpParty(SDL_getenv("PKMN_TEST_GEO_PARTY"));
                memset(&sEnter, 0, sizeof(sEnter));
                sEnter.lastGroup = sEnter.lastNum = -1;
                sEnter.pikeDoor = -1;
                sWarpX = (s16)p->startX;
                sWarpY = (s16)p->startY;
                SetWarpDestination(p->startGroup, p->startNum, WARP_ID_NONE, 0, 0);
            }
            else
            {
                /* the Battle Pike's room picks its trainer from the challenge's battle number when entered, which the
                 * game sets before it lets the player in (battle_pike.c PrepareOneTrainer loops for ever on none) */
                if ((p->num | (p->group << 8)) == MAP_BATTLE_FRONTIER_BATTLE_PIKE_ROOM_NORMAL
                 && gSaveBlock2Ptr->frontier.curChallengeBattleNum == 0)
                    gSaveBlock2Ptr->frontier.curChallengeBattleNum = 1;
                sWarpX = (s16)p->x;
                sWarpY = (s16)p->y;
                SetWarpDestination(p->group, p->num, WARP_ID_NONE, 0, 0);
            }
            sWarpPending = true;
            DoWarp();
            ResetInitialPlayerAvatarState();
        }
        sFrames = 0;
        SDL_AtomicSet(&gGeoProbe.phase, GEO_WARPING);
        return true;
    case GEO_WARPING:
        if (++sFrames > GEO_ARRIVE_FRAMES)
        {
            gGeoProbe.arrivedGroup = gSaveBlock1Ptr->location.mapGroup;
            gGeoProbe.arrivedNum = gSaveBlock1Ptr->location.mapNum;
            SDL_strlcpy(gGeoProbe.failure, "the field was not given back", sizeof(gGeoProbe.failure));
            if (sPlaces[sNext].enter)
                NotReached("the facility's start did not give the field back");
            else
                SDL_AtomicSet(&gGeoProbe.phase, GEO_ARRIVED);
            return true;
        }
        if (!FieldFree())
            return true;
        if (sPlaces[sNext].enter)
        {
            const struct GeoPlace *p = &sPlaces[sNext];

            if (!OnMap(p->startGroup, p->startNum))
            {
                NotReached("the warp to the facility's start went elsewhere");
                return true;
            }
            /* facing the attendant: the way in from here is the keys' (DioramaGeoProbe_Keys) */
            ObjectEventTurn(&gObjectEvents[gPlayerAvatar.objectEventId], p->startDir);
            LogMapChange();
            SDL_AtomicSet(&gGeoProbe.phase, GEO_ENTERING);
            return true;
        }
        Arrive();
        return true;
    case GEO_ENTERING:
        LogMapChange();
        if (OnMap(gGeoProbe.group, gGeoProbe.num) && FieldFree())
        {
            SDL_Log("Geometry probe: %s: entered through its facility at %d,%d after %d frames", gGeoProbe.key,
                    gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y, sEnter.frames);
            Arrive();
            return true;
        }
        if (sEnter.frames > GEO_ENTER_FRAMES)
            NotReached("the facility did not take the player there in time");
        return true;
    case GEO_ARRIVED:
        /* the render side never got there (the field drawn in 2D, which syncs no 3D scene): on with the tour, the
           place without a line in results.tsv (the check reports it not inspected) */
        if (++sFrames > GEO_RENDER_FRAMES)
        {
            SDL_Log("Geometry probe: %s: the render side did not finish", gGeoProbe.key);
            SDL_AtomicSet(&gGeoProbe.timedOut, 1);
            SDL_AtomicSet(&gGeoProbe.phase, GEO_DONE);
        }
        return true;
    case GEO_DONE:
        sNext++;
        SDL_AtomicSet(&gGeoProbe.phase, GEO_IDLE);
        return true;
    default:
        return true;
    }
}

bool DioramaGeoProbe_TakeWarpCoords(int16_t *x, int16_t *y)
{
    if (!sWarpPending)
        return false;
    sWarpPending = false;
    *x = sWarpX;
    *y = sWarpY;
    return true;
}

/* --- the way in: the keys a player would press ---------------------------------------------------------------- */

bool DioramaGeoProbe_Entering(void)
{
    return sActive && SDL_AtomicGet(&gGeoProbe.phase) == GEO_ENTERING;
}

static bool PartySlotChosen(int slot)
{
    for (u32 i = 0; i < MAX_FRONTIER_PARTY_SIZE; i++)
        if (gSelectedOrderFromParty[i] == slot + 1)
            return true;
    return false;
}

#ifdef ENABLE_NATIVE_UI
/* The frontier's party screen (ChoosePartyForBattleFrontier: as many Pokemon as the facility asks, then CONFIRM):
 * each Pokemon from the first that is not chosen yet - A, and ENTER, the first row of its menu - until the screen
 * moves the cursor to CONFIRM by itself, which A takes. A Pokemon whose menu has no ENTER (not able to enter) is left. */
static u16 PartyButton(const struct NativePartyMenu *party)
{
    switch (party->phase)
    {
    case NATIVE_PARTY_BROWSE:
        if (!party->ready)
            return 0;
        if (party->cursor < PARTY_SIZE)
            return PartySlotChosen(party->cursor) || (sEnter.partySkip & (1u << party->cursor)) ? DPAD_DOWN : A_BUTTON;
        return party->cursor == PARTY_SIZE ? A_BUTTON : DPAD_UP;   /* CONFIRM; from CANCEL back up to it */
    case NATIVE_PARTY_MENU:
        if (!party->ready)
            return 0;
        /* ENTER, SUMMARY, CANCEL for one that can enter and is not chosen */
        if (party->numActions == 3 && party->menuCursor == 0 && party->slotId < PARTY_SIZE
         && !PartySlotChosen(party->slotId))
            return A_BUTTON;
        if (party->slotId < PARTY_SIZE)
            sEnter.partySkip |= (u8)(1u << party->slotId);
        return B_BUTTON;
    case NATIVE_PARTY_MESSAGE:
    case NATIVE_PARTY_YES_NO:
        return party->ready ? A_BUTTON : 0;
    default:
        return 0;
    }
}
#endif

/* The Battle Pike's room of three paths: the door its hint points to - a player asks the woman there which room is
 * behind which door (battle_pike.c SetHintedRoom: one door's room is drawn and hinted at, the others are drawn when
 * walked into) - unless that room is the wild Pokemon's, whose room is another map: then another door, whose room
 * cannot be the wild Pokemon's (GetNextRoomType leaves out the hinted room's kind). Its doors are the warp triggers
 * at (2,3), (6,3) and (10,3); the player comes in at (6,10). The way: up the middle to row 6, along it to the door's
 * column, up into the door (rows 5 to 9 are open floor; the woman stands on row 5). */
static u16 PikeDoorButton(void)
{
    s16 x, y;
    int doorX, wayX, wayY;

    if (VarGet(VAR_TEMP_5) == 0)
        return 0;   /* the room's hint is set by its first frame's script */
    if (sEnter.pikeDoor < 0)
    {
        int hinted = gSaveBlock2Ptr->frontier.pikeHintedRoomIndex;
        int type = gSaveBlock2Ptr->frontier.pikeHintedRoomType;

        if (type == PIKE_ROOM_WILD_MONS)
            sEnter.pikeDoor = hinted == PIKE_ROOM_CENTER ? PIKE_ROOM_LEFT : PIKE_ROOM_CENTER;
        else
            sEnter.pikeDoor = hinted <= PIKE_ROOM_RIGHT ? hinted : PIKE_ROOM_CENTER;
        sEnter.pikeLeg = 0;
        SDL_Log("Geometry probe: %s: the Battle Pike's hint: room type %d behind door %d; door %d taken", gGeoProbe.key,
                type, hinted, sEnter.pikeDoor);
    }
    PlayerGetDestCoords(&x, &y);
    x -= MAP_OFFSET;
    y -= MAP_OFFSET;
    doorX = 2 + 4 * sEnter.pikeDoor;
    for (;;)
    {
        wayX = sEnter.pikeLeg == 0 ? 6 : doorX;
        wayY = sEnter.pikeLeg <= 1 ? 6 : 3;
        if (x != wayX || y != wayY || sEnter.pikeLeg >= 2)
            break;
        sEnter.pikeLeg++;
    }
    if (x < wayX)
        return DPAD_RIGHT;
    if (x > wayX)
        return DPAD_LEFT;
    return y > wayY ? DPAD_UP : y < wayY ? DPAD_DOWN : 0;
}

/* What the player does now, or 0. Directions are held (a walk), every other button pressed and let go. */
static u16 EnterButton(const struct GeoPlace *p, bool *walk)
{
    *walk = false;
#ifdef ENABLE_NATIVE_UI
    {
        struct NativePartyMenu party;

        if (!gMain.inBattle && PartyMenu_NativeUiState(&party) && party.chooseHalf)
            return PartyButton(&party);
    }
#endif
    if (gMain.inBattle)
    {
        /* A through a battle (its first move every turn, every line); a Pokemon fainted: the battle's party menu up
           on it - DOWN to the next one (as tools/qa/gameplay_check.py's plan does) */
        if (gMain.callback2 != BattleMainCB2 && gPartyMenu.menuType == PARTY_MENU_TYPE_IN_BATTLE
         && gPartyMenu.slotId >= 0 && gPartyMenu.slotId < PARTY_SIZE
         && GetMonData(&gParties[B_TRAINER_PLAYER][gPartyMenu.slotId], MON_DATA_HP) == 0)
            return DPAD_DOWN;
        return A_BUTTON;
    }
    if (gPaletteFade.active)
        return 0;
    /* a line, a question (its first choice: CHALLENGE, LV. 50, YES), the save: what the way in asks */
    if (gMain.callback2 != CB2_Overworld || ScriptContext_IsEnabled() || !IsFieldMessageBoxHidden()
     || ArePlayerFieldControlsLocked())
        return A_BUTTON;
    for (int i = 0; i < p->stepCount; i++)
    {
        const struct GeoStep *step = &p->steps[i];

        if (!OnMap(step->group, step->num))
            continue;
        *walk = true;
        if (strcmp(step->action, "U") == 0)
            return DPAD_UP;
        if (strcmp(step->action, "D") == 0)
            return DPAD_DOWN;
        if (strcmp(step->action, "L") == 0)
            return DPAD_LEFT;
        if (strcmp(step->action, "R") == 0)
            return DPAD_RIGHT;
        if (strcmp(step->action, "pike") == 0)
            return PikeDoorButton();
        *walk = false;
        return A_BUTTON;        /* A: talk to whoever the player faces */
    }
    return 0;
}

uint16_t DioramaGeoProbe_Keys(void)
{
    const struct GeoPlace *p;
    bool walk;
    u16 button;

    if (!DioramaGeoProbe_Entering())
        return 0;
    p = &sPlaces[sNext];
    sEnter.frames++;
    /* there, the field free: nothing more (an A then would talk to whoever stands in front) */
    if (OnMap(p->group, p->num) && FieldFree())
    {
        sEnter.pressLeft = sEnter.gapLeft = 0;
        return 0;
    }
    if (sEnter.pressLeft > 0)
    {
        sEnter.pressLeft--;
        return sEnter.held;
    }
    if (sEnter.gapLeft > 0)
    {
        sEnter.gapLeft--;
        return 0;
    }
    button = EnterButton(p, &walk);
    if (button == 0 || walk)
        return button;
    sEnter.held = button;
    sEnter.pressLeft = GEO_PRESS_HOLD - 1;
    sEnter.gapLeft = GEO_PRESS_GAP;
    return button;
}
#endif
