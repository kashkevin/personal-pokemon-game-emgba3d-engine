#ifndef GUARD_PLATFORM_PNG_WRITE_H
#define GUARD_PLATFORM_PNG_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Writes RGBA pixels (alpha dropped) as an 8-bit RGB PNG; flipRows for the
 * bottom-up rows glReadPixels gives. */
bool PngWrite_RGBA(const char *path, const uint8_t *rgba, int width, int height, bool flipRows);
/* The PNG's zlib stream (for the tests): returns its size, *out malloc'd. */
size_t PngWrite_Encode(const uint8_t *rgba, int width, int height, bool flipRows, uint8_t **out);

#endif
