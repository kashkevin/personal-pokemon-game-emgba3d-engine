#ifndef GUARD_DODRIO_BERRY_PICKING_H
#define GUARD_DODRIO_BERRY_PICKING_H

#include "main.h"

void StartDodrioBerryPicking(u16 partyId, MainCallback exitCallback);
void IsDodrioInParty(void);
void ShowDodrioBerryPickingRecords(void);
#ifdef PORTABLE
void DodrioBerryPicking_StartComputerPartners(void); // KNOWN_ISSUES MS-141 (PC game only)
#endif
#line 9 // gba-line

#endif // GUARD_DODRIO_BERRY_PICKING_H
