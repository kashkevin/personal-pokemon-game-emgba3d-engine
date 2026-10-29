#ifndef GUARD_NATIVE_UI_MESSAGE_PAGE_H
#define GUARD_NATIVE_UI_MESSAGE_PAGE_H

#include <stdbool.h>
#include <stdint.h>

/* The page of an engine message (engine bytes, 0xFF-terminated) its window shows when `printed` bytes have been printed
 * (< 0: all), as the game's printer lays it out: a \p (0xFB) clears the box, a \l (0xFA) scrolls it a line, the line
 * above staying on top; a break that is the last byte printed waits for its button, unless `flip` (the printer has
 * left the wait). The page goes to `page` (0xFF-terminated, at most size - 1 bytes, a \l in it as a line break);
 * returns the offset where the box's text begins (KNOWN_ISSUES MS-167). */
int UiW_MessagePageFlip(const uint8_t *text, int printed, bool flip, uint8_t *page, int size);
/* ... without flip */
int UiW_MessagePageText(const uint8_t *text, int printed, uint8_t *page, int size);

#endif // GUARD_NATIVE_UI_MESSAGE_PAGE_H
