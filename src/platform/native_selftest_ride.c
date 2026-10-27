// Native PC build: the riding seats (the owner's request of 2026-10-27, docs/gameplay-visual-updates-20261027.md section 2
// in the app repo). Project-owned. For every rideable species (multiplayer_ride.c's roster, base forms) and both trainer
// pictures, the seat the game computes from the mount's picture (MultiplayerRide_GetSeat), checked for sanity, and with an
// output path a contact sheet: per species one row, the old seat (7 px up, centred, the rider always drawn in front) and the
// new one (its height, its sideways place, the mount in front when facing the camera), facing south, north, west, east.
//   --selftest-ride [SHEET.ppm]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "load_save.h"
#include "malloc.h"
#include "multiplayer_ride.h"
#include "palette.h"
#include "pokemon.h"
#include "sprite.h"
#include "constants/characters.h"
#include "constants/event_objects.h"

static int sChecks, sFailures;

static void Expect(const char *what, bool32 ok)
{
    sChecks++;
    if (!ok)
        sFailures++;
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
}

#define CELL 64
#define GROUND 58     // the row the pictures stand on in a cell
static const u8 sFacings[4] = {DIR_SOUTH, DIR_NORTH, DIR_WEST, DIR_EAST};

static u32 Rgb(u16 color)
{
    u32 r = (color & 0x1F) << 3, g = ((color >> 5) & 0x1F) << 3, b = ((color >> 10) & 0x1F) << 3;
    return (r << 16) | (g << 8) | b;
}

// One frame of a picture drawn into the sheet, its bottom row at `bottom`, its left column at `left` (hFlip for east).
static void Blit(u8 *sheet, u32 sheetWidth, const struct ObjectEventGraphicsInfo *info, u32 frame, bool32 hFlip,
                 const u16 *palette, s32 left, s32 bottom)
{
    const u8 *tiles = info->images[0].relativeFrames ? (const u8 *)info->images[0].data + frame * info->images[0].size
                                                     : info->images[frame].data;
    s32 x, y;

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

            if (index == 0 || px < 0 || py < 0 || (u32)px >= sheetWidth)
                continue;
            rgb = Rgb(palette[index]);
            sheet[(py * sheetWidth + px) * 3 + 0] = rgb >> 16;
            sheet[(py * sheetWidth + px) * 3 + 1] = rgb >> 8;
            sheet[(py * sheetWidth + px) * 3 + 2] = rgb;
        }
    }
}

// The picture an object shows standing still facing that way, from its own animation table (as the game shows it)
static void FaceFrame(const struct ObjectEventGraphicsInfo *info, u32 facing, u32 *frame, bool32 *hFlip)
{
    u32 anim = facing == DIR_SOUTH ? ANIM_STD_FACE_SOUTH : facing == DIR_NORTH ? ANIM_STD_FACE_NORTH
             : facing == DIR_WEST ? ANIM_STD_FACE_WEST : ANIM_STD_FACE_EAST;
    const union AnimCmd *cmd = info->anims[anim];

    *frame = cmd->frame.imageValue;
    *hFlip = cmd->frame.hFlip;
}

int NativeSelfTest_Ride(const char *sheetPath)
{
    u16 species[128];
    u32 count = 0, i, f, gender;
    u16 riderGfx[2] = {GetPlayerAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_SURFING, MALE),
                       GetPlayerAvatarGraphicsIdByStateIdAndGender(PLAYER_AVATAR_STATE_SURFING, FEMALE)};
    u16 riderPalette[2][16];
    u8 *sheet = NULL;
    u32 sheetWidth = CELL * 8, sheetHeight;
    u32 minLift = 255, maxLift = 0, readable = 0;

    InitHeap(gHeap, HEAP_SIZE);
    SetSaveBlocksPointers(0);
    memset(gSaveBlock2Ptr, 0, sizeof(*gSaveBlock2Ptr));
    ResetSpriteData();
    FreeAllSpritePalettes();
    MultiplayerRide_Init();
    printf("selftest-ride: the riding seats (multiplayer_ride.c MultiplayerRide_GetSeat)\n");
    for (gender = 0; gender < 2; gender++)
    {
        const struct ObjectEventGraphicsInfo *info = GetObjectEventGraphicsInfo(riderGfx[gender]);
        u8 slot = LoadObjectEventPalette(info->paletteTag);

        memcpy(riderPalette[gender], &gPlttBufferUnfaded[OBJ_PLTT_OFFSET + slot * 16], sizeof(riderPalette[gender]));
    }
    for (u32 s = 1; s < NUM_SPECIES && count < ARRAY_COUNT(species); s++)
        if (IsSpeciesEnabled(s) && GetFormSpeciesId(s, 0) == s && MultiplayerRide_IsSpeciesRideable(s))
            species[count++] = s;
    Expect("the ride roster has its 67 species", count == 67);
    sheetHeight = CELL * count;
    if (sheetPath != NULL)
    {
        sheet = malloc(sheetWidth * sheetHeight * 3);
        for (i = 0; i < sheetWidth * sheetHeight; i++)   // a grey ground below the line, lighter above
        {
            u32 y = (i / sheetWidth) % CELL, shade = y > GROUND ? 0x90 : 0xC8;
            sheet[i * 3] = sheet[i * 3 + 1] = sheet[i * 3 + 2] = shade;
        }
    }
    printf("species                       lift (S N W E)   shift W E   [May: lift shift W]\n");
    for (i = 0; i < count; i++)
    {
        u16 mountGfx = GetGraphicsIdForMon(species[i], FALSE, FALSE);
        const struct ObjectEventGraphicsInfo *mount = GetObjectEventGraphicsInfo(mountGfx);
        const u16 *mountPalette = gSpeciesInfo[species[i]].overworldPalette;
        u8 lift[2][4];
        s8 shift[2][4];
        char name[POKEMON_NAME_LENGTH * 2 + 1];

        for (gender = 0; gender < 2; gender++)
            for (f = 0; f < 4; f++)
                MultiplayerRide_GetSeat(mountGfx, riderGfx[gender], sFacings[f], &lift[gender][f], &shift[gender][f]);
        {
            const u8 *n = GetSpeciesName(species[i]);
            u32 k = 0;
            for (; *n != EOS && k < sizeof(name) - 1; n++)
                name[k++] = (*n >= CHAR_A && *n <= CHAR_Z) ? 'A' + (*n - CHAR_A) : (*n >= CHAR_a && *n <= CHAR_z) ? 'a' + (*n - CHAR_a) : '?';
            name[k] = '\0';
        }
        printf("%-12s gfx %5u %2ux%2u   %2u %2u %2u %2u        %+3d %+3d   [%2u %+3d]\n", name, mountGfx,
               mount ? mount->width : 0, mount ? mount->height : 0,
               lift[0][0], lift[0][1], lift[0][2], lift[0][3], shift[0][2], shift[0][3], lift[1][2], shift[1][2]);
        if (mount != NULL && mount->images != NULL && !mount->compressed)
            readable++;
        minLift = min(minLift, lift[0][0]);
        maxLift = max(maxLift, lift[0][0]);
        if (sheet != NULL && mount != NULL && mount->images != NULL && mountPalette != NULL)
        {
            const struct ObjectEventGraphicsInfo *rider = GetObjectEventGraphicsInfo(riderGfx[0]);

            for (f = 0; f < 4; f++)
            {
                u32 facing = sFacings[f], frame, riderFrame;
                bool32 flip, riderFlip;
                s32 cellY = i * CELL + GROUND;
                s32 oldX = f * CELL + CELL / 2, newX = (4 + f) * CELL + CELL / 2;

                FaceFrame(mount, facing, &frame, &flip);
                FaceFrame(rider, facing, &riderFrame, &riderFlip);
                // the old seat: 7 px up (the old lift of a 32x32 mount), centred, the rider over the mount
                Blit(sheet, sheetWidth, mount, frame, flip, mountPalette, oldX - mount->width / 2, cellY);
                Blit(sheet, sheetWidth, rider, riderFrame, riderFlip, riderPalette[0], oldX - rider->width / 2, cellY - 7);
                // the new seat: facing the camera the mount is drawn over the rider
                if (facing == DIR_SOUTH)
                {
                    Blit(sheet, sheetWidth, rider, riderFrame, riderFlip, riderPalette[0], newX - rider->width / 2 + shift[0][f], cellY - lift[0][f]);
                    Blit(sheet, sheetWidth, mount, frame, flip, mountPalette, newX - mount->width / 2, cellY);
                }
                else
                {
                    Blit(sheet, sheetWidth, mount, frame, flip, mountPalette, newX - mount->width / 2, cellY);
                    Blit(sheet, sheetWidth, rider, riderFrame, riderFlip, riderPalette[0], newX - rider->width / 2 + shift[0][f], cellY - lift[0][f]);
                }
            }
        }
    }
    printf("lift: %u to %u px\n", minLift, maxLift);
    Expect("every roster species' picture can be read (an uncompressed overworld picture)", readable == count);
    Expect("every seat is between 4 and 30 px up (no rider on the ground, none floating above the picture)",
           minLift >= 4 && maxLift <= 30);
    if (sheet != NULL)
    {
        FILE *out = fopen(sheetPath, "wb");

        if (out != NULL)
        {
            fprintf(out, "P6\n%u %u\n255\n", sheetWidth, sheetHeight);
            fwrite(sheet, 1, sheetWidth * sheetHeight * 3, out);
            fclose(out);
            printf("contact sheet: %s (%u x %u; left: the old seat S N W E, right: the new one)\n", sheetPath, sheetWidth, sheetHeight);
        }
        free(sheet);
    }
    printf("selftest-ride: %d checks, %d failed\n", sChecks, sFailures);
    return sFailures == 0 ? 0 : 1;
}
