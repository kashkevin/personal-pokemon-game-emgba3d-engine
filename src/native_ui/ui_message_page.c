#ifdef ENABLE_NATIVE_UI
/*
 * A game message's page as its window shows it (see native_ui/ui_message_page.h): pure, so the host tests build it
 * alone (tests/diorama/test_message_page.c). Project-owned.
 */
#include "native_ui/ui_message_page.h"

// The page of a message the box shows when `printed` characters have been printed (-1: all), as the game's printer
// lays it out. A \p (0xFB) clears the box: the page starts after the last one that has a character after it. With `flip`
// a \p that is the last character printed already starts the next page (blank until its first character): the printer
// has left its wait, the player pressed A, and the old page must not stay up until the first letter comes (a slow text
// speed made that a visible pause: "nothing happened"). A \l (0xFA) scrolls the box a line: the line before it stays on
// top and the text after it comes under it (the printer's RENDER_STATE_SCROLL), so the page starts at that line, the
// \l a line break (KNOWN_ISSUES MS-167: it cleared the box like a \p, and the line above vanished); one that is the last
// character printed waits for its button and has not scrolled yet, unless `flip`. Returns where the box's text begins.
// Used by the draw and by the dialogue trace.
int UiW_MessagePageFlip(const uint8_t *text, int printed, bool flip, uint8_t *page, int size)
{
    int len = 0, total = 0, pageStart = 0, scroll = -1;

    while (text[total] != 0xFF && (printed < 0 || total < printed))
        total++;
    for (int i = 0; i < total; i++)
        if (text[i] == 0xFB && (i + 1 < total || flip))
            pageStart = i + 1;
    for (int i = pageStart; i < total; i++)
        if (text[i] == 0xFA && (i + 1 < total || flip))
            scroll = i;
    if (scroll >= 0)
    {
        // the line the scroll keeps: back from the \l to the line break before it
        int from = scroll;

        while (from > pageStart && text[from - 1] != 0xFE && text[from - 1] != 0xFA)
            from--;
        pageStart = from;
    }
    for (int i = pageStart; i < total && text[i] != 0xFB && len < size - 2; i++)
    {
        if (text[i] == 0xFA)
        {
            if (i + 1 >= total && !flip)
                break;
            page[len++] = 0xFE;
            continue;
        }
        page[len++] = text[i];
    }
    page[len] = 0xFF;
    return pageStart;
}

int UiW_MessagePageText(const uint8_t *text, int printed, uint8_t *page, int size)
{
    return UiW_MessagePageFlip(text, printed, false, page, size);
}
#endif // ENABLE_NATIVE_UI
