// The pre-game menu's line protocol, the part with no window (include/platform/shell_menu.h). Project-owned.
#include <string.h>

#include "platform/shell_menu.h"

int ShellMenu_Unescape(char *text)
{
    char *out = text;

    for (const char *in = text; *in != '\0'; in++)
    {
        if (*in == '\\' && in[1] != '\0')
        {
            in++;
            *out++ = *in == 't' ? '\t' : *in == 'n' ? '\n' : *in;
        }
        else
            *out++ = *in;
    }
    *out = '\0';
    return (int)(out - text);
}

int ShellMenu_Split(char *line, char **fields, int maxFields)
{
    int count = 0;
    char *cursor = line;

    while (count < maxFields)
    {
        char *tab = strchr(cursor, '\t');

        fields[count++] = cursor;
        if (tab == NULL)
            break;
        *tab = '\0';
        cursor = tab + 1;
    }
    for (int i = 0; i < count; i++)
        ShellMenu_Unescape(fields[i]);
    return count;
}

