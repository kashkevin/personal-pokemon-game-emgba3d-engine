#ifdef ENABLE_DIORAMA

#include <SDL2/SDL.h>

#include "diorama/tree_sprites.h"

/* Ids from 0x200 are the secondary tileset's, the same number meaning a
 * different drawing on every map. */
#define SECONDARY_ID 0x200

bool DioramaTreeSprites_Enabled(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_TREES");

        sEnabled = value != NULL && SDL_strcasecmp(value, "3ds") == 0;
    }
    return sEnabled;
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

#endif // ENABLE_DIORAMA
