/* The page of a game message its window shows (src/native_ui/ui_message_page.c; KNOWN_ISSUES MS-167). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "native_ui/ui_message_page.h"

/* engine bytes from ASCII: letters as themselves (the test only compares), '\n' 0xFE, '|' a \l (0xFA), '^' a \p (0xFB) */
static void Engine(uint8_t *out, const char *ascii)
{
    size_t i;

    for (i = 0; ascii[i] != '\0'; i++)
        out[i] = ascii[i] == '\n' ? 0xFE : ascii[i] == '|' ? 0xFA : ascii[i] == '^' ? 0xFB : (uint8_t)ascii[i];
    out[i] = 0xFF;
}

/* the page as ASCII ('\n' for a line break) */
static const char *Page(const char *ascii, int printed, int flip)
{
    static uint8_t text[2048], page[256];
    static char out[256];
    size_t i;

    Engine(text, ascii);
    UiW_MessagePageFlip(text, printed, flip != 0, page, (int)sizeof(page));
    for (i = 0; page[i] != 0xFF; i++)
        out[i] = page[i] == 0xFE ? '\n' : (char)page[i];
    out[i] = '\0';
    return out;
}

int main(void)
{
    static const char twoPages[] = "Line one\nline two^Page two\nends here";
    static const char scrolled[] = "First\nsecond|third|fourth";
    static char longText[1200];
    int n = 0;

    /* a \p clears the box; the first page until then */
    assert(strcmp(Page(twoPages, -1, 0), "Page two\nends here") == 0);
    assert(strcmp(Page(twoPages, 8, 0), "Line one") == 0);
    /* the \p printed and waiting: the old page stays up; after A (flip) the new one, blank until its first letter */
    assert(strcmp(Page(twoPages, 18, 0), "Line one\nline two") == 0);
    assert(strcmp(Page(twoPages, 18, 1), "") == 0);
    assert(strcmp(Page(twoPages, 19, 0), "P") == 0);
    /* a \l scrolls a line: the line before it stays on top (it cleared the box before: the line above vanished) */
    assert(strcmp(Page(scrolled, 16, 0), "second\nthi") == 0);
    assert(strcmp(Page(scrolled, -1, 0), "third\nfourth") == 0);
    /* a \l printed and waiting has not scrolled yet; after A it has, the bottom line blank */
    assert(strcmp(Page(scrolled, 13, 0), "First\nsecond") == 0);
    assert(strcmp(Page(scrolled, 13, 1), "second\n") == 0);
    /* a message far longer than 256 bytes: its last page whole (a window's copy holds UI_MESSAGE_COPY bytes) */
    for (int i = 0; i < 14; i++)
        n += snprintf(longText + n, sizeof(longText) - (size_t)n, "Page %02d of the long message\nwith a second line^", i);
    n += snprintf(longText + n, sizeof(longText) - (size_t)n, "The last page\nis all here");
    assert(n > 600);
    assert(strcmp(Page(longText, -1, 0), "The last page\nis all here") == 0);
    printf("test_message_page: ok\n");
    return 0;
}
