// The pre-game menu's line protocol (include/platform/shell_menu.h): fields split at TAB, \t \n \\ escapes, no field lost or invented.
#include <string.h>

#include "minitest.h"
#include "platform/shell_menu.h"

MT_TEST(a_line_splits_at_tabs_and_unescapes_each_field)
{
    char line[] = "row\tid-1\tA\\tB\\nC\\\\D\tdetail\ts";
    char *fields[8];

    MT_EQ(ShellMenu_Split(line, fields, 8), 5);
    MT_CHECK(strcmp(fields[0], "row") == 0);
    MT_CHECK(strcmp(fields[1], "id-1") == 0);
    MT_CHECK(strcmp(fields[2], "A\tB\nC\\D") == 0);
    MT_CHECK(strcmp(fields[3], "detail") == 0);
    MT_CHECK(strcmp(fields[4], "s") == 0);
}

MT_TEST(an_empty_field_is_kept_and_extra_fields_are_not_split)
{
    char line[] = "field\tname\t\tl5";
    char *fields[3];

    MT_EQ(ShellMenu_Split(line, fields, 3), 3);
    MT_CHECK(strcmp(fields[1], "name") == 0);
    MT_CHECK(fields[2][0] == '\0');
    {
        char many[] = "a\tb\tc\td\te";
        char *two[2];

        MT_EQ(ShellMenu_Split(many, two, 2), 2);
        MT_CHECK(strcmp(two[0], "a") == 0);
    }
}

MT_TEST(a_trailing_backslash_is_kept_and_unescape_never_grows_the_text)
{
    char text[] = "abc\\";
    int n = ShellMenu_Unescape(text);

    MT_EQ(n, 4);
    MT_CHECK(strcmp(text, "abc\\") == 0);
    {
        char escaped[] = "\\\\\\t\\n\\x";
        MT_EQ(ShellMenu_Unescape(escaped), 4);
        MT_CHECK(strcmp(escaped, "\\\t\nx") == 0);
    }
}

int main(void)
{
    MT_RUN(a_line_splits_at_tabs_and_unescapes_each_field);
    MT_RUN(an_empty_field_is_kept_and_extra_fields_are_not_split);
    MT_RUN(a_trailing_backslash_is_kept_and_unescape_never_grows_the_text);
    return MT_REPORT();
}
