@ The Game Corner's TM counter (the PC game; the owner's TM catalogue of 2026-10-31, docs/owner-decisions.md 26-29): the
@ clerk asks first - TM PRIZES (the five as before, at their prices) or the TM CATALOGUE, every TM of the game in four
@ kinds (data/scripts/mp_tm_catalogue_lists.inc, tools/mp/gen_tm_catalogue.py), each opening the coin shop
@ (src/platform/coin_shop.c), and back to the kinds after a purchase. A TM the player has shows SOLD OUT (TMs are never
@ used up on PC). Reached from MauvilleCity_GameCorner_EventScript_ChooseTMPrizeMessage.
MauvilleCity_GameCorner_EventScript_TmCounterWhich::
	message MauvilleCity_GameCorner_Text_TmCounterWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 3, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_TmPrizes, MauvilleCity_GameCorner_Text_TmCatalogue, gText_Exit
	switch VAR_RESULT
	case 0, MauvilleCity_GameCorner_EventScript_TmPrizes
	case 1, MauvilleCity_GameCorner_EventScript_TmCatalogue
	goto MauvilleCity_GameCorner_EventScript_TmCounterLeave

MauvilleCity_GameCorner_EventScript_TmPrizes::
	message MauvilleCity_GameCorner_Text_WhichPrize
	waitmessage
	setvar VAR_TEMP_1, 0
	showcoinsbox 0, 0
	goto MauvilleCity_GameCorner_EventScript_ChooseTMPrize

MauvilleCity_GameCorner_EventScript_TmCatalogue::
	message MauvilleCity_GameCorner_Text_TmCatalogueWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 5, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_TmPhysical, MauvilleCity_GameCorner_Text_TmSpecial, MauvilleCity_GameCorner_Text_TmStatus, MauvilleCity_GameCorner_Text_TmUtility, gText_Exit
	switch VAR_RESULT
	case 0, MauvilleCity_GameCorner_EventScript_TmPhysical
	case 1, MauvilleCity_GameCorner_EventScript_TmSpecial
	case 2, MauvilleCity_GameCorner_EventScript_TmStatus
	case 3, MauvilleCity_GameCorner_EventScript_TmUtility
	goto MauvilleCity_GameCorner_EventScript_TmCounterLeave

MauvilleCity_GameCorner_EventScript_TmPhysical::
	pokemart MauvilleCity_GameCorner_Pokemart_TmPhysical
	goto MauvilleCity_GameCorner_EventScript_TmCatalogue

MauvilleCity_GameCorner_EventScript_TmSpecial::
	pokemart MauvilleCity_GameCorner_Pokemart_TmSpecial
	goto MauvilleCity_GameCorner_EventScript_TmCatalogue

MauvilleCity_GameCorner_EventScript_TmStatus::
	pokemart MauvilleCity_GameCorner_Pokemart_TmStatus
	goto MauvilleCity_GameCorner_EventScript_TmCatalogue

MauvilleCity_GameCorner_EventScript_TmUtility::
	pokemart MauvilleCity_GameCorner_Pokemart_TmUtility
	goto MauvilleCity_GameCorner_EventScript_TmCatalogue

MauvilleCity_GameCorner_EventScript_TmCounterLeave::
	msgbox gText_PleaseComeAgain, MSGBOX_DEFAULT
	release
	end

MauvilleCity_GameCorner_Text_TmCounterWhich:
	.string "TM PRIZES, or the TM CATALOGUE:\n"
	.string "every TM there is?$"

MauvilleCity_GameCorner_Text_TmPrizes:
	.string "TM PRIZES$"

MauvilleCity_GameCorner_Text_TmCatalogue:
	.string "TM CATALOGUE$"

MauvilleCity_GameCorner_Text_TmCatalogueWhich:
	.string "Which kind of TM?$"

MauvilleCity_GameCorner_Text_TmPhysical:
	.string "PHYSICAL ATTACKS$"

MauvilleCity_GameCorner_Text_TmSpecial:
	.string "SPECIAL ATTACKS$"

MauvilleCity_GameCorner_Text_TmStatus:
	.string "STATUS & SETUP$"

MauvilleCity_GameCorner_Text_TmUtility:
	.string "DEFENSE & UTILITY$"

	.include "data/scripts/mp_tm_catalogue_lists.inc"
