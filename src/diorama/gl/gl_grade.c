#ifdef ENABLE_DIORAMA

#include <SDL2/SDL.h>

#include "diorama/gl_grade.h"

#define GRADE_SATURATION 1.30f /* the 3DS project's VOXEL_GRADE_SATURATION */

float DioramaGLGrade_Saturation(void)
{
    static int sEnabled = -1;

    if (sEnabled < 0)
    {
        const char *value = SDL_getenv("PKMN_DIORAMA_GRADE");

        sEnabled = value == NULL || value[0] != '0';
    }
    return sEnabled ? GRADE_SATURATION : 1.0f;
}

#endif
