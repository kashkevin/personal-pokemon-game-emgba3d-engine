#ifndef GUARD_PLATFORM_SHELL_MENU_H
#define GUARD_PLATFORM_SHELL_MENU_H

/*
 * The pre-game menu (Update 30): the EmeraldTogether main menu, shown in the game's own window before any game
 * starts. `pokeemerald-mp --shell` runs only this - no ROM, no save, no game state - and talks to the application
 * (EmeraldTogether.exe) over stdin/stdout in a line protocol, so every choice that needs the application's checks
 * (the installed game, worlds, characters, the network) stays in the application, which describes each screen and
 * receives what the player chose. When a choice starts a game the application closes this process and starts the game.
 *
 * Protocol: one command or event a line, fields separated by TAB; text escapes are \\ \t \n. Commands to the menu:
 *   screen <id> <token>   begin a screen (the token comes back with every event)
 *   title|sub|note|hint <text>
 *   row <id> <label> <detail> <flags>       d disabled, s start here, w warning, a accent, 2 two lines
 *   field <id> <label> <value> <flags>      n digits only, lNN longest value, m long value (pasted code), s start here
 *   button <id> <label> <flags>             d disabled, a the main action, s start here
 *   status <info|ok|warn|error|busy> <text>
 *   end                   show the screen          quit    close the window and exit
 * Events from the menu: ready, event <token> select <id> [<field> <value>]..., event <token> back [<field> <value>]...,
 * event <token> closed. With PKMN_SHELL_TEST=1: inject key|text <..>, shot <png path>, state.
 */
#include <stdbool.h>

#define SHELL_PROTOCOL 1

/* Runs the menu to the end: returns the process exit code (0 normal, 2 no display, 3 the application went away). */
int ShellMenu_Main(int argc, char **argv);

/* Pure pieces, exposed so tools/pc_port/tests can check them without a window. */
int ShellMenu_Unescape(char *text);                           /* in place; returns the new length */
int ShellMenu_Split(char *line, char **fields, int maxFields); /* splits at TAB (unescaping each); returns the count */

#endif
