#ifndef GUARD_BERRY_CRUSH_H
#define GUARD_BERRY_CRUSH_H

#include "main.h"

void StartBerryCrush(MainCallback exitCallback);
#ifdef PORTABLE
void BerryCrush_StartComputerPartners(void); // KNOWN_ISSUES MS-141 (PC game only)
#endif
#line 7 // gba-line

#endif // GUARD_BERRY_CRUSH_H
