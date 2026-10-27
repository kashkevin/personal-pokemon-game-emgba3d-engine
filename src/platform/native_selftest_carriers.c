// Native PC build: the Pokemon carrying the player on Surf, Dive and Fly (the owner's request of 2026-10-27, item 4;
// docs/gameplay-visual-updates-20261027.md section 4 in the app repo). Project-owned. src/field_carrier.c draws the
// Pokemon that used the move in place of the generic surf blob, underwater silhouette and fly bird. Checked here before
// the game starts, without a display:
//  - every enabled species and form that can learn Surf, Dive or Fly has an overworld picture to be drawn with (its
//    own, or its base form's); the ones without are listed (the generic blob or bird is drawn for them);
//  - the seat on each (the rider's lift and sideways shift, both trainers) is within the riding seats' bounds;
//  - which Pokemon carries: the move's user (a party slot, or the PC boxes), else the first that knows the move, and the
//    NPCs' own fliers by the map;
//  - the sprites: the surf sprite made after the player's (it reads the player's place of this frame), its species'
//    palette shiny or not, a silhouette palette under the water, its place under the rider's seat, its animations and
//    drawing order, and its palette freed once the game destroys it; the flyer's place and drawing order;
//  - the underwater rider's picture and palette, and a save keeping the original's underwater look.
// With an output path, a contact sheet: per species the carrier with the rider on its back facing south, north, west and
// east, under the water (west) and in the air (west). Nothing here writes a save file; then the game exits.
//   --selftest-carriers [SHEET.ppm]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "event_object_movement.h"
#include "field_carrier.h"
#include "field_effect_helpers.h"
#include "field_move.h"
#include "field_player_avatar.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_ride.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "sprite.h"
#include "constants/characters.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/maps.h"
#include "constants/moves.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

#define CELL_W 64
#define CELL_H 96
#define RIDER_BOTTOM 64   // the row the rider's picture ends on in a cell
#define CELLS 6           // surfing south, north, west, east; under the water; in the air
#define PER_ROW 2         // species side by side
static const u8 sFacings[4] = {DIR_SOUTH, DIR_NORTH, DIR_WEST, DIR_EAST};

static u32 Rgb(u16 color)
{
    u32 r = (color & 0x1F) << 3, g = ((color >> 5) & 0x1F) << 3, b = ((color >> 10) & 0x1F) << 3;
    return (r << 16) | (g << 8) | b;
}

// One frame of a picture drawn into the sheet, its bottom row at `bottom`, its centre column at `centre`
static void Blit(u8 *sheet, u32 sheetWidth, u32 sheetHeight, const struct ObjectEventGraphicsInfo *info, u32 frame,
                 bool32 hFlip, const u16 *palette, s32 centre, s32 bottom)
{
    const u8 *tiles = info->images[0].relativeFrames ? (const u8 *)info->images[0].data + frame * info->images[0].size
                                                     : info->images[frame].data;
    s32 x, y, left = centre - info->width / 2;

    for (y = 0; y < info->height; y++)
    {
        for (x = 0; x < info->width; x++)
        {
            s32 sx = hFlip ? info->width - 1 - x : x;
            u32 tile = (y / 8) * (info->width / 8) + sx / 8;
            u8 packed = tiles[tile * TILE_SIZE_4BPP + (y & 7) * 4 + (sx & 7) / 2];
            u8 index = (sx & 1) ? packed >> 4 : packed & 0xF;
            s32 px = left + x, py = bottom - (info->height - 1) + y;
            u32 rgb;

            if (index == 0 || px < 0 || py < 0 || (u32)px >= sheetWidth || (u32)py >= sheetHeight)
                continue;
            rgb = Rgb(palette[index]);
            sheet[(py * sheetWidth + px) * 3 + 0] = rgb >> 16;
            sheet[(py * sheetWidth + px) * 3 + 1] = rgb >> 8;
            sheet[(py * sheetWidth + px) * 3 + 2] = rgb;
        }
    }
}

// The first frame an object shows for an animation (as the game shows it)
static void AnimFrame(const struct ObjectEventGraphicsInfo *info, u32 anim, u32 *frame, bool32 *hFlip)
{
    const union AnimCmd *cmd = info->anims[anim];

    *frame = cmd->frame.imageValue;
    *hFlip = cmd->frame.hFlip;
}

static u32 FaceAnimOf(u32 facing)
{
    return facing == DIR_SOUTH ? ANIM_STD_FACE_SOUTH : facing == DIR_NORTH ? ANIM_STD_FACE_NORTH
         : facing == DIR_WEST ? ANIM_STD_FACE_WEST : ANIM_STD_FACE_EAST;
}

static void Name(u16 species, char *out, u32 size)
{
    const u8 *n = GetSpeciesName(species);
    u32 k = 0;

    for (; *n != EOS && k < size - 1; n++)
        out[k++] = (*n >= CHAR_A && *n <= CHAR_Z) ? 'A' + (*n - CHAR_A) : (*n >= CHAR_a && *n <= CHAR_z) ? 'a' + (*n - CHAR_a)
                 : (*n >= CHAR_0 && *n <= CHAR_9) ? '0' + (*n - CHAR_0) : '-';
    out[k] = '\0';
}

static bool32 BattleOnly(u16 species)
{
    const struct SpeciesInfo *info = &gSpeciesInfo[species];

    return info->isMegaEvolution || info->isPrimalReversion || info->isUltraBurst || info->isGigantamax || info->isTeraForm
        || info->isTotem;
}

static bool32 Bluish(const u16 *palette)
{
    u32 i;

    for (i = 1; i < 16; i++)
    {
        u32 r = palette[i] & 0x1F, g = (palette[i] >> 5) & 0x1F, b = (palette[i] >> 10) & 0x1F;

        if (b < r || b < g)
            return FALSE;
    }
    return TRUE;
}

static void MakeMon(struct Pokemon *mon, u16 species, u32 personality, bool32 shiny, enum Move move1, enum Move move2)
{
    CreateMon(mon, species, 40, personality, OTID_STRUCT_PLAYER_ID);
    SetMonData(mon, MON_DATA_IS_SHINY, &shiny);
    SetMonMoveSlot(mon, move1, 0);
    SetMonMoveSlot(mon, move2 != MOVE_NONE ? move2 : MOVE_TACKLE, 1);
}

static u16 PictureSpecies(u16 graphicsId)
{
    return graphicsId & OBJ_EVENT_MON_SPECIES_MASK;
}

static void Setup(void)
{
    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock1Ptr, 0, sizeof(*gSaveBlock1Ptr));
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    ResetPokemonStorageSystem();
    ZeroPlayerPartyMons();
    ResetSpriteData();
    FreeAllSpritePalettes();
    MultiplayerRide_Init();
    memset(gObjectEvents, 0, sizeof(gObjectEvents));
    memset(&gPlayerAvatar, 0, sizeof(gPlayerAvatar));
    gPlayerAvatar.objectEventId = 0;
    gPlayerAvatar.gender = MALE;
    gObjectEvents[0].active = TRUE;
    gObjectEvents[0].isPlayer = TRUE;
    gObjectEvents[0].movementDirection = DIR_SOUTH;
    gObjectEvents[0].graphicsId = GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_SURFING);
}

int NativeSelfTest_Carriers(const char *sheetPath)
{
    static u16 roster[NUM_SPECIES];
    u32 count = 0, own = 0, baseForm = 0, battleOnly = 0, missing = 0, i, f, gender;
    u32 surfers = 0, divers = 0, fliers = 0;
    u16 riderGfx[2];
    u16 riderPalette[2][16], riderSilhouette[16];
    u8 *sheet = NULL;
    u32 sheetWidth = CELL_W * CELLS * PER_ROW, sheetHeight = 0;
    u32 minLift = 255, maxLift = 0, maxFront = 0, raised = 0, seatsOk = 0, drawn = 0;
    char name[POKEMON_NAME_LENGTH * 2 + 1];

    Setup();
    printf("selftest-carriers: the Pokemon carrying the player on Surf, Dive and Fly (field_carrier.c)\n");
    riderGfx[0] = GetPlayerAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_SURFING, MALE);
    riderGfx[1] = GetPlayerAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_SURFING, FEMALE);
    for (gender = 0; gender < 2; gender++)
    {
        const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(riderGfx[gender]);
        u8 slot = LoadObjectEventPalette(info->paletteTag);

        memcpy(riderPalette[gender], &gPlttBufferUnfaded[OBJ_PLTT_OFFSET + slot * 16], sizeof(riderPalette[gender]));
    }
    FieldCarrier_Silhouette(riderPalette[0], riderSilhouette);
    FreeAllSpritePalettes();

    // ---- the roster: every enabled species and form that can learn one of the three
    printf("species without an overworld picture of their own or their base form's (the generic blob or bird is drawn):\n");
    for (u32 s = 1; s < NUM_SPECIES; s++)
    {
        bool32 surf, dive, fly;
        u16 graphicsId;

        if (!IsSpeciesEnabled(s))
            continue;
        surf = CanLearnTeachableMove(s, MOVE_SURF);
        dive = CanLearnTeachableMove(s, MOVE_DIVE);
        fly = CanLearnTeachableMove(s, MOVE_FLY);
        if (!surf && !dive && !fly)
            continue;
        if (BattleOnly(s))
        {
            battleOnly++;   // never in the field: back in its usual form after the battle
            continue;
        }
        surfers += surf;
        divers += dive;
        fliers += fly;
        if (!FieldCarrier_GetGraphicsId(s, FALSE, FALSE, &graphicsId))
        {
            Name(s, name, sizeof(name));
            printf("  %4u %-12s%s%s%s\n", s, name, surf ? " Surf" : "", dive ? " Dive" : "", fly ? " Fly" : "");
            missing++;
            continue;
        }
        if (PictureSpecies(graphicsId) == s)
            own++;
        else
            baseForm++;
        roster[count++] = s;
    }
    printf("can learn them (battle-only forms left out: %u): %u species and forms; Surf %u, Dive %u, Fly %u\n", battleOnly,
           count + missing, surfers, divers, fliers);
    printf("drawn as themselves: %u with their own picture, %u with their base form's; without a picture: %u\n", own,
           baseForm, missing);
    Expect("every species and form that can learn Surf, Dive or Fly is drawn as itself (its own or its base form's "
           "overworld picture)", missing == 0);
    Expect("Lapras, Wailord, Gyarados, Milotic and Sharpedo surf as themselves",
           FieldCarrier_GetGraphicsId(SPECIES_LAPRAS, FALSE, FALSE, &(u16){0})
        && FieldCarrier_GetGraphicsId(SPECIES_WAILORD, FALSE, FALSE, &(u16){0})
        && FieldCarrier_GetGraphicsId(SPECIES_GYARADOS, FALSE, FALSE, &(u16){0})
        && FieldCarrier_GetGraphicsId(SPECIES_MILOTIC, FALSE, FALSE, &(u16){0})
        && FieldCarrier_GetGraphicsId(SPECIES_SHARPEDO, FALSE, FALSE, &(u16){0}));
    {
        u16 normal, shiny, east, west;

        FieldCarrier_GetGraphicsId(SPECIES_GYARADOS, FALSE, FALSE, &normal);
        FieldCarrier_GetGraphicsId(SPECIES_GYARADOS, TRUE, FALSE, &shiny);
        Expect("a shiny carrier keeps its shininess (its picture's id carries it, so its shiny palette is loaded)",
               normal != shiny && (shiny & OBJ_EVENT_MON_SHINY));
        FieldCarrier_GetGraphicsId(SPECIES_GASTRODON_EAST, FALSE, FALSE, &east);
        FieldCarrier_GetGraphicsId(SPECIES_GASTRODON_WEST, FALSE, FALSE, &west);
        Expect("a form is drawn as that form (Gastrodon East Sea and West Sea: two pictures)", east != west
            && PictureSpecies(east) == SPECIES_GASTRODON_EAST && PictureSpecies(west) == SPECIES_GASTRODON_WEST);
    }

    // ---- the seats: as the riding seats, for both trainers' surfing poses
    if (sheetPath != NULL)
    {
        sheetHeight = CELL_H * ((count + PER_ROW - 1) / PER_ROW);
        sheet = malloc(sheetWidth * sheetHeight * 3);
        for (i = 0; i < sheetWidth * sheetHeight; i++)
        {
            u32 cell = (i % sheetWidth) / CELL_W % CELLS, y = (i / sheetWidth) % CELL_H;
            u32 rgb = cell == 4 ? 0x183058 : cell == 5 ? 0xC8D8E8 : y > RIDER_BOTTOM - 6 ? 0x3878C0 : 0x88C8F0;

            sheet[i * 3] = rgb >> 16;
            sheet[i * 3 + 1] = rgb >> 8;
            sheet[i * 3 + 2] = rgb;
        }
    }
    for (i = 0; i < count; i++)
    {
        u16 graphicsId;
        const struct ObjectEventGraphicsInfo *mount;
        const u16 *palette;
        u16 silhouette[16];
        u8 lift[2][4];
        s8 shift[2][4];
        bool32 ok = TRUE;

        FieldCarrier_GetGraphicsId(roster[i], FALSE, FALSE, &graphicsId);
        mount = GetObjectEventGraphicsInfo(graphicsId);
        palette = gSpeciesInfo[PictureSpecies(graphicsId)].overworldPalette;
        for (gender = 0; gender < 2; gender++)
        {
            for (f = 0; f < 4; f++)
                FieldCarrier_GetSeat(graphicsId, riderGfx[gender], sFacings[f], &lift[gender][f], &shift[gender][f]);
            for (f = 0; f < 4; f++)
            {
                if (lift[gender][f] > (sFacings[f] == DIR_SOUTH ? 63 : 30) || shift[gender][f] > mount->width / 2
                 || shift[gender][f] < -(s32)mount->width / 2)
                    ok = FALSE;
                if (sFacings[f] == DIR_SOUTH)
                {
                    maxFront = max(maxFront, lift[gender][f]);
                    raised += gender == 0 && lift[0][f] > lift[0][2];   // [2]: facing west
                    continue;
                }
                minLift = min(minLift, lift[gender][f]);
                maxLift = max(maxLift, lift[gender][f]);
            }
        }
        seatsOk += ok;
        if (sheet == NULL || mount->images == NULL || palette == NULL || mount->compressed)
            continue;
        drawn++;
        FieldCarrier_Silhouette(palette, silhouette);
        {
            const struct ObjectEventGraphicsInfo *rider = GetObjectEventGraphicsInfo(riderGfx[0]);
            s32 originX = (i % PER_ROW) * CELL_W * CELLS, originY = (i / PER_ROW) * CELL_H + RIDER_BOTTOM;
            u32 frame, riderFrame;
            bool32 flip, riderFlip;

            for (f = 0; f < 4; f++)
            {
                s32 centre = originX + f * CELL_W + CELL_W / 2;

                AnimFrame(mount, FaceAnimOf(sFacings[f]), &frame, &flip);
                AnimFrame(rider, FaceAnimOf(sFacings[f]), &riderFrame, &riderFlip);
                // facing the camera its head is drawn over the rider's legs, otherwise the rider is in front
                if (sFacings[f] != DIR_SOUTH)
                    Blit(sheet, sheetWidth, sheetHeight, mount, frame, flip, palette, centre - shift[0][f], originY + lift[0][f]);
                Blit(sheet, sheetWidth, sheetHeight, rider, riderFrame, riderFlip, riderPalette[0], centre, originY);
                if (sFacings[f] == DIR_SOUTH)
                    Blit(sheet, sheetWidth, sheetHeight, mount, frame, flip, palette, centre - shift[0][f], originY + lift[0][f]);
            }
            // under the water, facing west: both as the silhouette
            AnimFrame(mount, ANIM_STD_FACE_WEST, &frame, &flip);
            AnimFrame(rider, ANIM_STD_FACE_WEST, &riderFrame, &riderFlip);
            Blit(sheet, sheetWidth, sheetHeight, mount, frame, flip, silhouette, originX + 4 * CELL_W + CELL_W / 2 - shift[0][2],
                 originY + lift[0][2]);
            Blit(sheet, sheetWidth, sheetHeight, rider, riderFrame, riderFlip, riderSilhouette, originX + 4 * CELL_W + CELL_W / 2,
                 originY);
            // in the air, facing west with a wing beat (its fast walking loop's first picture): the rider's centre at the
            // flyer's plus (shift, half its height - lift - half the rider's), as FieldCarrier_ShiftFlySprite draws it
            AnimFrame(mount, ANIM_STD_GO_FAST_WEST, &frame, &flip);
            {
                s32 birdCentreY = (i / PER_ROW) * CELL_H + CELL_H / 2, birdCentreX = originX + 5 * CELL_W + CELL_W / 2;
                s32 riderCentreY = birdCentreY + mount->height / 2 - lift[0][2] - rider->height / 2;

                Blit(sheet, sheetWidth, sheetHeight, mount, frame, flip, palette, birdCentreX, birdCentreY + mount->height / 2 - 1);
                Blit(sheet, sheetWidth, sheetHeight, rider, riderFrame, riderFlip, riderPalette[0], birdCentreX + shift[0][2],
                     riderCentreY + rider->height / 2 - 1);
            }
        }
    }
    printf("seats: lift %u to %u px from the side; facing the camera up to %u px (%u seats lower than the side's, so the "
           "rider's head shows over a tall front picture)\n", minLift, maxLift, maxFront, raised);
    Expect("every carrier's seat is within bounds (from the side lift 0-30 px as the riding seats, facing the camera up to "
           "63 px; shift inside the picture)", seatsOk == count);

    // ---- which Pokemon carries
    Setup();
    MakeMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_PIKACHU, 0x100, FALSE, MOVE_THUNDERBOLT, MOVE_NONE);
    MakeMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_LAPRAS, 0x2001, TRUE, MOVE_ICE_BEAM, MOVE_SURF);
    MakeMon(&gParties[B_TRAINER_PLAYER][2], SPECIES_WAILORD, 0x3002, FALSE, MOVE_SURF, MOVE_DIVE);
    MakeMon(&gParties[B_TRAINER_PLAYER][3], SPECIES_PIDGEOT, 0x4003, FALSE, MOVE_FLY, MOVE_NONE);
    CalculatePlayerPartyCount();
    {
        u16 species;
        bool8 shiny, female;

        Expect("nobody chose yet: the first party Pokemon knowing Surf carries (Lapras, shiny)",
               FieldCarrier_GetWater(&species, &shiny, &female) && species == SPECIES_LAPRAS && shiny);
        FieldCarrier_SetWater(2);
        Expect("Surf used by the third (Wailord): Wailord carries", FieldCarrier_GetWater(&species, &shiny, &female)
            && species == SPECIES_WAILORD && !shiny);
        gPlayerAvatar.flags |= PLAYER_AVATAR_FLAG_UNDERWATER;
        Expect("and under the water after its Dive, still Wailord", FieldCarrier_GetWater(&species, &shiny, &female)
            && species == SPECIES_WAILORD);
        gPlayerAvatar.flags = 0;
        ZeroMonData(&gParties[B_TRAINER_PLAYER][2]);
        CalculatePlayerPartyCount();
        Expect("Wailord gone from the save (released): the first that knows Surf again (Lapras)",
               FieldCarrier_GetWater(&species, &shiny, &female) && species == SPECIES_LAPRAS);
        // the HMs from the PC boxes: Surf known only by a boxed Sharpedo
        ZeroMonData(&gParties[B_TRAINER_PLAYER][1]);
        CalculatePlayerPartyCount();
        {
            struct Pokemon boxed;

            MakeMon(&boxed, SPECIES_SHARPEDO, 0x5004, FALSE, MOVE_SURF, MOVE_CRUNCH);
            SetBoxMonAt(0, 7, &boxed.box);
        }
        Expect("Surf known only in the PC boxes: the boxed Sharpedo carries", FieldCarrier_GetWater(&species, &shiny, &female)
            && species == SPECIES_SHARPEDO);
        Expect("FieldMove_TakeBoxUser finds it", FieldMove_TakeBoxUser(MOVE_SURF));
        FieldCarrier_SetWater(FIELD_MOVE_BOX_USER);
        Expect("Surf used from the boxes (FIELD_MOVE_BOX_USER): Sharpedo", FieldCarrier_GetWater(&species, &shiny, &female)
            && species == SPECIES_SHARPEDO);
    }

    // ---- the sprites
    Setup();
    MakeMon(&gParties[B_TRAINER_PLAYER][0], SPECIES_LAPRAS, 0x2001, FALSE, MOVE_SURF, MOVE_DIVE);
    MakeMon(&gParties[B_TRAINER_PLAYER][1], SPECIES_PIDGEOT, 0x4003, FALSE, MOVE_FLY, MOVE_NONE);
    CalculatePlayerPartyCount();
    {
        u16 riderId = GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_SURFING), lapras;
        const struct ObjectEventGraphicsInfo *rider = GetObjectEventGraphicsInfo(riderId);
        u8 playerSprite = CreateObjectGraphicsSprite(riderId, SpriteCallbackDummy, 120, 64, 0);
        struct Sprite *ps = &gSprites[playerSprite], *cs;
        u8 carrier, lift;
        s8 shift;
        u16 tag;

        gObjectEvents[0].spriteId = playerSprite;
        FieldCarrier_GetGraphicsId(SPECIES_LAPRAS, FALSE, FALSE, &lapras);
        FieldCarrier_SetWater(0);
        carrier = FieldCarrier_CreateSurfSprite(120, 72, 150, FALSE);
        Expect("the surf sprite is made", carrier < MAX_SPRITES);
        cs = &gSprites[carrier];
        Expect("in a slot after the player's (its callback reads the player's place of this frame)", carrier > playerSprite);
        Expect("it is the carrier (UpdateSurfBlobFieldEffect hands it to field_carrier.c)", FieldCarrier_IsSurfSprite(cs)
            && cs->callback == UpdateSurfBlobFieldEffect);
        Expect("drawn with Lapras' own picture", cs->images == GetObjectEventGraphicsInfo(lapras)->images);
        tag = GetSpritePaletteTagByPaletteNum(cs->oam.paletteNum);
        Expect("in Lapras' own palette (its dynamic tag)", tag == lapras);
        FieldCarrier_GetSeat(lapras, riderId, DIR_SOUTH, &lift, &shift);
        Expect("made where it will carry the rider: its bottom the seat's lift below the surfer's, centred (south)",
               cs->x == 120 - shift && cs->y - cs->centerToCornerVecY == 72 - 8 + rider->height / 2 + lift);
        // carrying the player: its place from the player's sprite, its animations and drawing order
        ps->x = 100;
        ps->y = 50;
        ps->subpriority = 120;
        gObjectEvents[0].movementDirection = DIR_WEST;
        StartSpriteAnim(ps, ANIM_STD_GO_WEST);   // standing: its walking animation paused (FaceDirection)
        ps->animPaused = TRUE;
        FieldCarrier_UpdateSurfSprite(&gObjectEvents[0], ps, cs, TRUE);
        FieldCarrier_GetSeat(lapras, riderId, DIR_WEST, &lift, &shift);
        Expect("facing west it sits under the rider's seat (x - shift, bottom + lift)",
               cs->x == 100 - shift && cs->y - cs->centerToCornerVecY == 50 - ps->centerToCornerVecY + lift);
        Expect("standing still it treads the water (its walking loop, west)", cs->animNum == ANIM_STD_GO_WEST);
        Expect("and the rider is drawn in front of it (one more subpriority)", cs->subpriority == 121);
        StartSpriteAnim(ps, ANIM_STD_GO_FAST_WEST);   // a step on the water (the walking animation playing)
        ps->animPaused = FALSE;
        FieldCarrier_UpdateSurfSprite(&gObjectEvents[0], ps, cs, TRUE);
        Expect("the rider moving: it paddles (its fast loop)", cs->animNum == ANIM_STD_GO_FAST_WEST);
        gObjectEvents[0].movementDirection = DIR_SOUTH;
        StartSpriteAnim(ps, ANIM_STD_GO_SOUTH);
        ps->animPaused = TRUE;
        FieldCarrier_UpdateSurfSprite(&gObjectEvents[0], ps, cs, TRUE);
        Expect("facing the camera its head is drawn over the rider's legs (one less subpriority)", cs->subpriority == 119
            && cs->animNum == ANIM_STD_GO_SOUTH);
        {
            s16 x = cs->x, y = cs->y;

            ps->x += 16;
            StartSpriteAnim(ps, ANIM_STD_GO_SOUTH);
            ps->animPaused = FALSE;
            FieldCarrier_UpdateSurfSprite(&gObjectEvents[0], ps, cs, FALSE);
            Expect("the rider jumping off (not carried): it stays where it is, treading", cs->x == x && cs->y == y
                && cs->animNum == ANIM_STD_GO_SOUTH);
        }
        DestroySprite(cs);
        Expect("destroyed by the game: no longer the carrier", !FieldCarrier_IsSurfSprite(cs));
        Expect("its palette still loaded until the next tick", IndexOfSpritePaletteTag(tag) < 16);
        FieldCarrier_Tick();
        Expect("the tick frees its palette (nothing else uses it)", IndexOfSpritePaletteTag(tag) == 0xFF);

        // shiny
        {
            bool32 shiny = TRUE;
            u16 shinyLapras;

            SetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_IS_SHINY, &shiny);
            FieldCarrier_SetWater(0);
            FieldCarrier_GetGraphicsId(SPECIES_LAPRAS, TRUE, FALSE, &shinyLapras);
            carrier = FieldCarrier_CreateSurfSprite(120, 72, 150, FALSE);
            Expect("a shiny Lapras is drawn in its shiny palette", carrier < MAX_SPRITES
                && GetSpritePaletteTagByPaletteNum(gSprites[carrier].oam.paletteNum) == shinyLapras
                && memcmp(&gPlttBufferUnfaded[OBJ_PLTT_ID(gSprites[carrier].oam.paletteNum)],
                          gSpeciesInfo[SPECIES_LAPRAS].overworldShinyPalette, PLTT_SIZE_4BPP) == 0);
            DestroySprite(&gSprites[carrier]);
            FieldCarrier_Tick();
        }

        // under the water
        gPlayerAvatar.flags = PLAYER_AVATAR_FLAG_UNDERWATER;
        carrier = FieldCarrier_CreateSurfSprite(120, 72, 150, TRUE);
        Expect("under the water the carrier is made", carrier < MAX_SPRITES);
        Expect("in a silhouette palette of its own", GetSpritePaletteTagByPaletteNum(gSprites[carrier].oam.paletteNum)
            == FIELD_CARRIER_PAL_TAG_UNDERWATER_CARRIER);
        Expect("its colours the original silhouette's blues",
               Bluish(&gPlttBufferUnfaded[OBJ_PLTT_ID(gSprites[carrier].oam.paletteNum)]));
        Expect("its species palette not kept loaded for it", IndexOfSpritePaletteTag(lapras) == 0xFF
            && IndexOfSpritePaletteTag(lapras | OBJ_EVENT_MON_SHINY) == 0xFF);
        {
            const struct ObjectEventGraphicsInfo *underwater = FieldCarrier_ResolveGraphics(FIELD_CARRIER_GFX_UNDERWATER_RIDER);
            struct SpritePalette pal;

            Expect("the rider under the water: their surfing pose", underwater != NULL && underwater->images == rider->images
                && underwater->width == rider->width && underwater->height == rider->height);
            Expect("in the rider silhouette's palette", underwater != NULL
                && underwater->paletteTag == FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER
                && FieldCarrier_GetSpritePalette(FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER, &pal) && Bluish(pal.data));
            Expect("which the object palette loader finds", LoadObjectEventPalette(FIELD_CARRIER_PAL_TAG_UNDERWATER_RIDER) < 16);
            Expect("every other graphics id is not the carrier's", FieldCarrier_ResolveGraphics(riderId) == NULL
                && !FieldCarrier_IsUnderwaterAvatarGraphicsId(riderId)
                && FieldCarrier_IsUnderwaterAvatarGraphicsId(FIELD_CARRIER_GFX_UNDERWATER_RIDER));
        }
        DestroySprite(&gSprites[carrier]);
        FieldCarrier_Tick();
        Expect("its silhouette palette freed with it", IndexOfSpritePaletteTag(FIELD_CARRIER_PAL_TAG_UNDERWATER_CARRIER) == 0xFF);
        gPlayerAvatar.flags = 0;

        // a save under the water keeps the original's look
        gObjectEvents[0].graphicsId = FIELD_CARRIER_GFX_UNDERWATER_RIDER;
        gObjectEvents[0].localId = LOCALID_PLAYER;
        SaveObjectEvents();
        {
            u16 saved = gSaveBlock1Ptr->objectEvents[0].graphicsId;

            saved = (saved >> 8) | (saved << 8);
            Expect("a save under the water on a carrier keeps the original's underwater look",
                   saved == GetPlayerAvatarGraphicsIdByStateId(PLAYER_AVATAR_STATE_UNDERWATER));
        }
        Expect("the game's own object keeps the carrier's look", gObjectEvents[0].graphicsId == FIELD_CARRIER_GFX_UNDERWATER_RIDER);

        // the flyer
        FieldCarrier_SetFlyer(1);
        {
            u16 pidgeot;
            u8 bird = FieldCarrier_CreateFlySprite(FIELD_CARRIER_PLAYER, 0xff, 0xb4, 1);
            struct Sprite *b = &gSprites[bird];

            FieldCarrier_GetGraphicsId(SPECIES_PIDGEOT, FALSE, FALSE, &pidgeot);
            Expect("Fly used by Pidgeot: the flyer is Pidgeot", bird < MAX_SPRITES
                && b->images == GetObjectEventGraphicsInfo(pidgeot)->images);
            Expect("beating its wings, facing west (its fast loop)", bird < MAX_SPRITES && b->animNum == ANIM_STD_GO_FAST_WEST);
            Expect("in the first free slot, as the original's bird", bird < MAX_SPRITES && bird < playerSprite + 3);
            b->x2 = 0;
            b->y2 = 0;
            FieldCarrier_ShiftFlySprite(b, NULL);
            FieldCarrier_GetSeat(pidgeot, riderId, DIR_WEST, &lift, &shift);
            Expect("alone it flies over the map (priority 1, subpriority 1) along the original's path shifted for its seat",
                   b->oam.priority == 1 && b->subpriority == 1 && b->x2 == -shift
                && b->y2 == -8 - (GetObjectEventGraphicsInfo(pidgeot)->height / 2 - lift - rider->height / 2));
            ps->oam.priority = 2;
            ps->subpriority = 90;
            b->x2 = 0;
            b->y2 = 0;
            FieldCarrier_ShiftFlySprite(b, ps);
            Expect("carrying the rider, drawn behind them (their priority, one more subpriority)",
                   b->oam.priority == 2 && b->subpriority == 91);
            DestroySprite(b);
            FieldCarrier_Tick();
        }
        // the NPCs' fliers
        {
            static const struct { u16 map, species; const char *what; } sNpc[] = {
                {MAP_LILYCOVE_CITY, SPECIES_TROPIUS, "the rival leaving Lilycove: Tropius (in each of their teams there)"},
                {MAP_ROUTE120, SPECIES_SKARMORY, "Steven leaving Route 120: Skarmory"},
                {MAP_ROUTE128, SPECIES_SKARMORY, "Steven on Route 128: Skarmory"},
            };

            for (i = 0; i < ARRAY_COUNT(sNpc); i++)
            {
                u16 gfx;
                u8 bird;

                gSaveBlock1Ptr->location.mapGroup = sNpc[i].map >> 8;
                gSaveBlock1Ptr->location.mapNum = sNpc[i].map & 0xFF;
                FieldCarrier_GetGraphicsId(sNpc[i].species, FALSE, FALSE, &gfx);
                bird = FieldCarrier_CreateFlySprite(FIELD_CARRIER_NPC, 0x78, 0, 1);
                Expect(sNpc[i].what, bird < MAX_SPRITES && gSprites[bird].images == GetObjectEventGraphicsInfo(gfx)->images);
                if (bird < MAX_SPRITES)
                {
                    DestroySprite(&gSprites[bird]);
                    FieldCarrier_Tick();
                }
            }
            gSaveBlock1Ptr->location.mapGroup = MAP_LITTLEROOT_TOWN >> 8;
            gSaveBlock1Ptr->location.mapNum = MAP_LITTLEROOT_TOWN & 0xFF;
            Expect("an NPC fly-out on any other map keeps the original's bird",
                   FieldCarrier_CreateFlySprite(FIELD_CARRIER_NPC, 0x78, 0, 1) == MAX_SPRITES);
        }
        // nobody knows Fly: the original's bird
        ZeroPlayerPartyMons();
        CalculatePlayerPartyCount();
        Expect("nobody in the party or the boxes knows Fly: the original's bird",
               FieldCarrier_CreateFlySprite(FIELD_CARRIER_PLAYER, 0xff, 0xb4, 1) == MAX_SPRITES);
        Expect("nobody knows Surf or Dive: the original's blob", FieldCarrier_CreateSurfSprite(120, 72, 150, FALSE) == MAX_SPRITES);
    }

    if (sheet != NULL)
    {
        FILE *out = fopen(sheetPath, "wb");

        if (out != NULL)
        {
            fprintf(out, "P6\n%u %u\n255\n", sheetWidth, sheetHeight);
            fwrite(sheet, 1, sheetWidth * sheetHeight * 3, out);
            fclose(out);
            printf("contact sheet: %s (%u x %u, %u species; per species: surfing S N W E, under the water, in the air)\n",
                   sheetPath, sheetWidth, sheetHeight, drawn);
        }
        free(sheet);
    }
    if (sheetPath != NULL)   // the contact sheet's rows: two species a row, in this order
    {
        printf("contact sheet order:");
        for (i = 0; i < count; i++)
            printf(" %u", roster[i]);
        printf("\n");
    }
    printf("selftest-carriers: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
