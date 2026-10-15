#ifdef ENABLE_DIORAMA

/*
 * The 3D battle's view of the game (include/diorama/battle_scene.h), read at
 * the frame boundary while the game thread is parked (sdl2.c VDraw). The
 * stage's ground kinds and the battlers' shadows follow
 * ZallaxDev/pokeemerald-3Ds-dualscreen's 3ds_port/src/voxel/voxel_battle.c,
 * the scenery fade its voxel_world.c (VoxelWorld_ScreenFade); MIT, see
 * src/diorama/README.md. Nothing here writes game memory.
 */

#include <string.h>

#include "global.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_main.h"
#include "battle_setup.h"
#include "data.h"
#include "bg.h"
#include "event_object_movement.h"
#include "field_weather.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "palette.h"
#include "sprite.h"
#include "diorama/battle_scene.h"
#include "constants/map_types.h"

uint8_t DioramaBattle_GroundKind(uint8_t behavior)
{
    if (MetatileBehavior_IsTallGrass(behavior) || MetatileBehavior_IsLongGrass(behavior))
        return DIORAMA_GROUND_GRASS;
    if (MetatileBehavior_IsSandOrDeepSand(behavior))
        return DIORAMA_GROUND_SAND;
    if (MetatileBehavior_IsSurfableWaterOrUnderwater(behavior)
     || MetatileBehavior_IsDeepOrOceanWater(behavior))
        return DIORAMA_GROUND_WATER;
    return DIORAMA_GROUND_PLAIN;
}

static float FadeChannel(u16 color, unsigned shift)
{
    return (float)((color >> shift) & 31);
}

/*
 * The fade that best explains the shown colours from the unfaded ones,
 * shown = keep * unfaded + (1 - keep) * colour, by least squares over all
 * of them (the 3DS port's FadeFit).
 */
static void FadeFit(const u16 *shown, const u16 *unfaded, unsigned count,
                    float *amount, float rgb[3])
{
    float n = (float)count, cov = 0.0f, var = 0.0f, keep;
    float su[3] = {0}, sf[3] = {0}, suu[3] = {0}, suf[3] = {0};
    unsigned i, c;

    for (i = 0; i < count; ++i)
        for (c = 0; c < 3; ++c)
        {
            float u = FadeChannel(unfaded[i], c * 5), f = FadeChannel(shown[i], c * 5);

            su[c] += u;
            sf[c] += f;
            suu[c] += u * u;
            suf[c] += u * f;
        }
    for (c = 0; c < 3; ++c)
    {
        cov += n * suf[c] - su[c] * sf[c];
        var += n * suu[c] - su[c] * su[c];
    }
    /* Colours all alike tell nothing of the slope: take them as the target. */
    keep = var > 1.0f ? cov / var : 0.0f;
    if (keep < 0.0f) keep = 0.0f;
    if (keep > 1.0f) keep = 1.0f;
    *amount = 1.0f - keep;
    for (c = 0; c < 3; ++c)
    {
        float offset = (sf[c] - keep * su[c]) / n;
        float value = *amount > 0.01f ? offset / *amount : 0.0f;

        if (value < 0.0f) value = 0.0f;
        if (value > 31.0f) value = 31.0f;
        rgb[c] = value / 31.0f;
    }
}

/*
 * The world stands in for the scenery, and fades as its three palettes do
 * (BG 2-4, LoadBattleEnvironmentGfx), whatever does it: a palette fade, or a
 * move blending the background towards a colour without one. The battlers'
 * palettes fade on their own.
 */
static void SceneryFade(struct DioramaBattleFrame *frame)
{
    const u16 *shown = (const u16 *)PLTT + BG_PLTT_ID(2);
    const u16 *unfaded = gPlttBufferUnfaded + BG_PLTT_ID(2);

    frame->fadeAmount = 0.0f;
    frame->fadeRgb[0] = frame->fadeRgb[1] = frame->fadeRgb[2] = 0.0f;
    if (memcmp(shown, unfaded, 3 * 16 * sizeof(u16)) == 0)
        return;
    FadeFit(shown, unfaded, 3 * 16, &frame->fadeAmount, frame->fadeRgb);
    /* Below one step of the game's own 16 it is rounding, not a fade. */
    if (frame->fadeAmount < 1.0f / 32.0f)
        frame->fadeAmount = 0.0f;
}

/*
 * The GBA scenery has a base under each side, which the world has not: a
 * soft shadow on the ground under each battler that the game gives none of
 * its own (the opponents' Gen 4 style shadow sprites, B_ENEMY_MON_SHADOW_STYLE,
 * are drawn by the game over the world as they are).
 */
static void Shadows(struct DioramaBattleFrame *frame)
{
    unsigned b;

    frame->shadowCount = 0;
    if (gBattleSpritesDataPtr == NULL || gBattleSpritesDataPtr->healthBoxesData == NULL)
        return;
    for (b = 0; b < gBattlersCount && b < MAX_BATTLERS_COUNT
                && frame->shadowCount < DIORAMA_BATTLE_MAX_SHADOWS; ++b)
    {
        unsigned id = gBattlerSpriteIds[b], shadow;
        const struct Sprite *sprite;
        bool enemy;
        struct DioramaBattleShadow *out;

        if ((gAbsentBattlerFlags & (1u << b)) || id >= MAX_SPRITES)
            continue;
        sprite = &gSprites[id];
        if (!sprite->inUse || sprite->invisible)
            continue;
        enemy = GetBattlerPositionSide(b) != B_SIDE_PLAYER;
        shadow = gBattleSpritesDataPtr->healthBoxesData[b].shadowSpriteIdPrimary;
        if (enemy && shadow < MAX_SPRITES && gSprites[shadow].inUse && !gSprites[shadow].invisible)
            continue;
        out = &frame->shadows[frame->shadowCount++];
        out->x = sprite->x + sprite->x2;
        out->y = GetBattlerSpriteCoord(b, BATTLER_COORD_Y) + 30.0f;
        out->rx = enemy ? 24.0f : 30.0f;
        out->ry = enemy ? 5.0f : 6.0f;
    }
}

/*
 * Each battler's sprite and healthbox as drawn this frame (battle_environment.h): the box
 * its 64 x 64 picture covers, offsets and animation included, the point it stands on (the
 * contact shadows' centre), and its healthbox's box.
 */
static void Battlers(struct DioramaBattleFrame *frame)
{
    unsigned b;

    if (gBattleSpritesDataPtr == NULL)
        return;
    for (b = 0; b < gBattlersCount && b < MAX_BATTLERS_COUNT && b < DIORAMA_BATTLE_MAX_BATTLERS; ++b)
    {
        struct DioramaBattleBattlerSprite *out = &frame->battlers[b];
        unsigned id = gBattlerSpriteIds[b], box = gHealthboxSpriteIds[b];
        const struct Sprite *sprite;
        float x, y;

        out->battler = b;
        out->position = GetBattlerPosition(b);
        out->opponent = GetBattlerPositionSide(b) != B_SIDE_PLAYER;
        if ((gAbsentBattlerFlags & (1u << b)) == 0 && id < MAX_SPRITES)
        {
            sprite = &gSprites[id];
            if (sprite->inUse && !sprite->invisible)
            {
                x = sprite->x + sprite->x2 + sprite->centerToCornerVecX;
                y = sprite->y + sprite->y2 + sprite->centerToCornerVecY;
                out->present = true;
                out->x0 = x;
                out->y0 = y;
                out->x1 = x + 64.0f;
                out->y1 = y + 64.0f;
                out->footX = sprite->x + sprite->x2;
                out->footY = GetBattlerSpriteCoord(b, BATTLER_COORD_Y) + 30.0f;
            }
        }
        if (box < MAX_SPRITES && gSprites[box].inUse && !gSprites[box].invisible)
        {
            const struct Sprite *h = &gSprites[box];

            out->healthbox = true;
            out->hx0 = h->x + h->x2 + h->centerToCornerVecX;
            out->hy0 = h->y + h->y2 + h->centerToCornerVecY;
            /* the healthbox is a 64 x 32 sprite and a second half beside it */
            out->hx1 = out->hx0 + 128.0f;
            out->hy1 = out->hy0 + 32.0f;
        }
    }
}

/*
 * The backdrop the battle chose (battle_bg.c GetBattleEnvironmentOverride, without its test
 * runner case): the Frontier's for a Frontier battle, a Gym Leader's or the Champion's by the
 * trainer's class, else the map's battle scene (a Gym, an Elite Four room, a hideout), else the
 * environment under the player. A link battle keeps the map's own: in this game it is fought
 * where the players stand, not in the Frontier's hall.
 */
static uint8_t BackdropEnvironment(void)
{
    enum MapBattleScene scene = GetCurrentMapBattleScene();

    if (gBattleTypeFlags & BATTLE_TYPE_FRONTIER)
        return BATTLE_ENVIRONMENT_FRONTIER;
    /* Only one of the game's own trainers has a class to read: a link battle's
     * opponent is another player (TRAINER_LINK_OPPONENT), as is a secret
     * base's, and reading a class for those ids stops the game at the engine's
     * assertion (data.h SanitizeTrainerId) on this drawing thread, so every
     * native multiplayer battle froze as it began. A co-op Gym battle's
     * opponent is the real Leader and keeps the Leader's backdrop. */
    if ((gBattleTypeFlags & BATTLE_TYPE_TRAINER) && TRAINER_BATTLE_PARAM.opponentA < TRAINERS_COUNT)
    {
        u32 trainerClass = GetTrainerClassFromId(TRAINER_BATTLE_PARAM.opponentA);

        if (trainerClass == TRAINER_CLASS_LEADER)
            return BATTLE_ENVIRONMENT_LEADER;
        if (trainerClass == TRAINER_CLASS_CHAMPION)
            return BATTLE_ENVIRONMENT_CHAMPION;
    }
    switch (scene)
    {
    case MAP_BATTLE_SCENE_GYM:      return BATTLE_ENVIRONMENT_GYM;
    case MAP_BATTLE_SCENE_MAGMA:    return BATTLE_ENVIRONMENT_MAGMA;
    case MAP_BATTLE_SCENE_AQUA:     return BATTLE_ENVIRONMENT_AQUA;
    case MAP_BATTLE_SCENE_SIDNEY:   return BATTLE_ENVIRONMENT_SIDNEY;
    case MAP_BATTLE_SCENE_PHOEBE:   return BATTLE_ENVIRONMENT_PHOEBE;
    case MAP_BATTLE_SCENE_GLACIA:   return BATTLE_ENVIRONMENT_GLACIA;
    case MAP_BATTLE_SCENE_DRAKE:    return BATTLE_ENVIRONMENT_DRAKE;
    case MAP_BATTLE_SCENE_FRONTIER: return BATTLE_ENVIRONMENT_FRONTIER;
    default:                        return gBattleEnvironment;
    }
}

/* BG3's scroll from rest, GBA pixels, either way round its 256-pixel turn:
 * the scenery rests at 0 (or 256, the same picture). */
static float SceneryShift(u16 scroll)
{
    return (float)((int)((scroll + 128) & 255) - 128);
}

extern const struct BgTemplate gBattleBgTemplates[];

/*
 * The battle's own screen, its BG3 set up as the scenery's (battle_bg.c): not
 * a screen over the battle - the bag, the party, a summary, the Pokedex page
 * of a catch - whose BG3, if it has one, is its own (the 3DS port knows the
 * battle screen by its VBlank callback the same way).
 */
static bool IsBattleScreen(void)
{
    u16 control = REG_BG3CNT;

    return gMain.callback2 == BattleMainCB2
        && ((control >> 2) & 3) == gBattleBgTemplates[3].charBaseIndex
        && ((control >> 8) & 0x1F) == gBattleBgTemplates[3].mapBaseIndex;
}

/* ── The map's sprites in OAM (sprite.c BuildOamBuffer and LoadOam) ── */

static uint32_t sMapOamPending[4];
uint32_t gDioramaMapOam[4];

/* Placed on the map, or the weather (the 3D view draws its own): the 3DS
 * port's split of the field's sprites from a transition's (its sScreenOam,
 * IsVoxelWeatherSprite), by the weather's sprite sheets here. */
static bool IsMapSprite(const struct Sprite *sprite)
{
    if (sprite->coordOffsetEnabled)
        return TRUE;
    return sprite->template != NULL
        && sprite->template->tileTag >= GFXTAG_CLOUD && sprite->template->tileTag <= GFXTAG_RAIN;
}

void DioramaSprites_BeginOam(void)
{
    memset(sMapOamPending, 0, sizeof(sMapOamPending));
}

void DioramaSprites_MarkOam(const struct Sprite *sprite, uint8_t first, uint8_t end)
{
    unsigned i;

    if (!IsMapSprite(sprite))
        return;
    for (i = first; i < end && i < 128; ++i)
        sMapOamPending[i >> 5] |= 1u << (i & 31);
}

void DioramaSprites_LatchOam(void)
{
    memcpy(gDioramaMapOam, sMapOamPending, sizeof(gDioramaMapOam));
}

/* Set once this battle has a weather of its own (gBattleWeather): from then on
 * its world shows that, and none when it ends. */
static bool sBattleWeatherSeen;

/* DioramaBattleFrame.weather (read only: the battle's weather is the game's). */
static uint8_t BattleWorldWeather(void)
{
    uint8_t place = GetCurrentWeather();

    if (gBattleWeather != B_WEATHER_NONE)
        sBattleWeatherSeen = true;
    if (gBattleWeather & B_WEATHER_RAIN)
        return (gBattleWeather & B_WEATHER_RAIN_PRIMAL) ? WEATHER_DOWNPOUR : WEATHER_RAIN;
    if (gBattleWeather & B_WEATHER_SANDSTORM)
        return WEATHER_SANDSTORM;
    if (gBattleWeather & B_WEATHER_ICY_ANY)
        return WEATHER_SNOW;
    if (gBattleWeather & B_WEATHER_FOG)
        return WEATHER_FOG_HORIZONTAL;
    /* A room's fog (the Lavaridge Gym's steam) stays out of its battles: the
     * battle's low camera sees the whole room through it, a white veil over every
     * wall, and the GBA's battles show none there (gl_compositor.c
     * DioramaGL_DrawBattleWorld leaves its light out too). */
    if (gMapHeader.mapType == MAP_TYPE_INDOOR
     && (place == WEATHER_FOG_HORIZONTAL || place == WEATHER_FOG_DIAGONAL))
        return WEATHER_NONE;
    switch (place)
    {
    /* Until the battle takes the place's weather (its opening lines), the
     * place's own; a fog the battle does not take (B_OVERWORLD_FOG) stays. */
    case WEATHER_RAIN:
    case WEATHER_RAIN_THUNDERSTORM:
    case WEATHER_DOWNPOUR:
    case WEATHER_SANDSTORM:
    case WEATHER_SNOW:
        return sBattleWeatherSeen ? WEATHER_NONE : place;
    case WEATHER_FOG_HORIZONTAL:
    case WEATHER_FOG_DIAGONAL:
    case WEATHER_VOLCANIC_ASH:
    case WEATHER_UNDERWATER:
    case WEATHER_UNDERWATER_BUBBLES:
        return place;
    default:
        return WEATHER_NONE;
    }
}

void DioramaBattle_Capture(struct DioramaBattleFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    frame->inBattle = gMain.inBattle;
    if (gSaveBlock1Ptr != NULL)
    {
        frame->mapGroup = gSaveBlock1Ptr->location.mapGroup;
        frame->mapNum = gSaveBlock1Ptr->location.mapNum;
    }
    /* The layout the game draws, as the 3D view's snapshot takes it
     * (scene_snapshot.c CurrentLayoutId): after setmaplayoutindex (Route
     * 111 without the Mirage Tower, Shoal Cave at high tide, Sootopolis
     * during the legends' battle, Route 130 with Mirage Island) it is the
     * save's id, not gMapHeader.mapLayoutId, and DioramaGL_GetBattleWorld
     * compares the two - a mismatch left the battle without its world. */
    frame->mapLayoutId = gSaveBlock1Ptr != NULL ? gSaveBlock1Ptr->mapLayoutId : gMapHeader.mapLayoutId;
    if (gPlayerAvatar.objectEventId < OBJECT_EVENTS_COUNT)
    {
        const struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

        frame->playerGridX = player->currentCoords.x;
        frame->playerGridY = player->currentCoords.y;
        frame->playerMapX = player->currentCoords.x - MAP_OFFSET;
        frame->playerMapY = player->currentCoords.y - MAP_OFFSET;
        frame->playerFacing = player->facingDirection;
    }
    frame->surfing = (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_SURFING) != 0;
    if (!frame->inBattle)
    {
        sBattleWeatherSeen = false;
        frame->transition = DioramaBattle_TransitionActive();
        if (frame->transition && (REG_BG1CNT & BGCNT_MOSAIC))
        {
            frame->fieldMosaicH = REG_MOSAIC & 0xF;
            frame->fieldMosaicV = (REG_MOSAIC >> 4) & 0xF;
        }
        return;
    }
    frame->battleTypeFlags = gBattleTypeFlags;
    frame->link = (gBattleTypeFlags & BATTLE_TYPE_LINK) != 0;
    frame->wild = !(gBattleTypeFlags & (BATTLE_TYPE_TRAINER | BATTLE_TYPE_LINK | BATTLE_TYPE_RECORDED_LINK
                                        | BATTLE_TYPE_FRONTIER | BATTLE_TYPE_EREADER_TRAINER));
    frame->doubles = (gBattleTypeFlags & BATTLE_TYPE_DOUBLE) != 0;
    frame->sceneryShown = DioramaBattle_SceneryShown() && IsBattleScreen();
    frame->introSliding = DioramaBattle_IntroSliding();
    frame->bg3Enabled = (REG_DISPCNT & DISPCNT_BG3_ON) != 0;
    frame->forcedBlank = (REG_DISPCNT & DISPCNT_FORCED_BLANK) != 0;
    frame->sceneryShiftX = SceneryShift(REG_BG3HOFS);
    frame->sceneryShiftY = SceneryShift(REG_BG3VOFS);
    SceneryFade(frame);
    Shadows(frame);
    Battlers(frame);
    frame->battleEnvironment = gBattleEnvironment;
    frame->backdropEnvironment = BackdropEnvironment();
    frame->mapType = gMapHeader.mapType;
    frame->weather = BattleWorldWeather();
}

#endif
