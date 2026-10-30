#ifdef ENABLE_DIORAMA

#include <SDL2/SDL.h>

#include "diorama/tree_sprites.h"

/* Ids from 0x200 are the secondary tileset's, the same number meaning a
 * different drawing on every map. */
#define SECONDARY_ID 0x200

/* Set by the main thread (sdl2.c: F8, pokeemerald.cfg, PKMN_DIORAMA_TREES),
 * read by the game thread's snapshot and the renderer. */
static SDL_atomic_t sEnabled;

bool DioramaTreeSprites_Enabled(void)
{
    return SDL_AtomicGet(&sEnabled) != 0;
}

void DioramaTreeSprites_SetEnabled(bool enabled)
{
    SDL_AtomicSet(&sEnabled, enabled ? 1 : 0);
}

int DioramaTreeSprites_Part(bool dewford, uint16_t metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's small tree: the trunk on the sand (23A), and inside the
         * wood (243), where the next crown overlaps it. */
        if (dewford && (metatileId == 0x23A || metatileId == 0x243))
            return DIORAMA_TREE_PART_SMALL;
        return DIORAMA_TREE_PART_NONE;
    }
    switch (metatileId)
    {
    case 0x1D4: case 0x1D6: return 0; /* upper left, including forest edge */
    case 0x1D5: case 0x1D7: return 1;
    case 0x1DC: case 0x1DE: case 0x1E4: case 0x1E6: return 2;
    case 0x1DD: case 0x1DF: case 0x1E5: case 0x1E7: return 3;
    /* The lower row again, beside open ground (Petalburg Woods, Rustboro,
     * Route 116, the Safari Zone: about 200 cells). Not in the 3DS project's
     * list, so those trees lost their crowns' lower half and showed a band of
     * flat trunk art instead (KNOWN_ISSUES V23). */
    case 0x1F2: return 2;
    case 0x1F3: return 3;
    /* A large tree's lower row under a small tree's canopy top. */
    case 0x1EC: return 2;
    case 0x1ED: return 3;
    /* Small trees: the edge of a wood, and inside it where the next crown
     * overlaps the trunk. 1F4-1F5 also carry a neighbour's leaves. */
    case 0x016: case 0x017: case 0x0C6: case 0x0C7:
    case 0x1F4: case 0x1F5: return DIORAMA_TREE_PART_SMALL;
    default: return DIORAMA_TREE_PART_NONE;
    }
}

uint16_t DioramaTreeSprites_GroundMetatile(bool dewford, uint16_t metatileId)
{
    if (metatileId >= SECONDARY_ID)
    {
        /* Dewford's crown top over the sand. */
        if (dewford && metatileId == 0x239)
            return 0x124;
        return metatileId;
    }
    switch (metatileId)
    {
    case 0x1C6: case 0x1C7: return 0x00D; /* tall grass without canopy */
    case 0x1CE: case 0x1CF: return 0x001; /* ordinary grass */
    /* A small tree's canopy top, over whatever it stood in front of. The
     * fence feet under 040 have no metatile of their own: grass. */
    case 0x00E: case 0x00F: case 0x040: return 0x001;
    case 0x01D: return 0x002; /* ledge edge */
    case 0x025: return 0x00D; /* tall grass */
    case 0x02D: return 0x0A1; /* reflective water */
    case 0x035: case 0x193: return 0x170; /* calm water */
    case 0x0CE: return 0x091; /* rock wall, sand base */
    default: return metatileId;
    }
}

uint16_t DioramaTreeSprites_TrunkGround(bool dewford)
{
    return dewford ? 0x124 : 0x001; /* sand, grass */
}

int DioramaTreeSprites_AshPart(uint16_t metatileId)
{
    /* The 2x2 tree on the ash (Route 113), then on the volcanic sand (Route
     * 114, Fallarbor Town): its quarters inside a wood, its upper row beside
     * open ground (0x204-0x207) and its lower row at a wood's foot
     * (0x20C-0x20F), as the metatiles lie beside and over one another on the
     * three maps. */
    switch (metatileId)
    {
    case 0x26E: case 0x205: case 0x26C: case 0x207: return 0;
    case 0x26D: case 0x204: case 0x26B: case 0x206: return 1;
    case 0x276: case 0x20D: case 0x274: case 0x20F: return 2;
    case 0x275: case 0x20C: case 0x273: case 0x20E: return 3;
    default: return DIORAMA_TREE_PART_NONE;
    }
}

uint16_t DioramaTreeSprites_AshGround(uint16_t metatileId)
{
    switch (metatileId)
    {
    case 0x26C: case 0x207: case 0x26B: case 0x206: case 0x274: case 0x20F: case 0x273: case 0x20E:
        return 0x279; /* volcanic sand */
    default:
        return 0x218; /* ash */
    }
}

#endif // ENABLE_DIORAMA
