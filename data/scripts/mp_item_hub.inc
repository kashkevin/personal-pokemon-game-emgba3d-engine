@ The Game Corner's item hub (the PC game; the owner's request of 2026-10-31 and the approved prices, docs/owner-decisions.md
@ 30-31): the prize counter's ITEMS - every item a player can use here, for COINS, in the owner's ten kinds. Eight are
@ the hub's own lists (data/scripts/mp_item_hub_lists.inc, tools/mp/gen_item_hub.py: the RARE GOODS at their prices and
@ the items found elsewhere at the owner's); MEGA STONES opens the Mega Stone dealer's list (with a MEGA RING, as at the
@ dealer); TMs the TM CATALOGUE's kinds. Each opens the coin shop (src/platform/coin_shop.c) and comes back to the kinds
@ after a purchase. Reached from the clerk's second choice (MauvilleCity_GameCorner_EventScript_RareGoods).
MauvilleCity_GameCorner_EventScript_ItemHub::
	message MauvilleCity_GameCorner_Text_ItemHubWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 6, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_HubHeld, MauvilleCity_GameCorner_Text_HubBattle, MauvilleCity_GameCorner_Text_HubMega, MauvilleCity_GameCorner_Text_HubEvolution, MauvilleCity_GameCorner_Text_HubBalls, MauvilleCity_GameCorner_Text_HubHealing, MauvilleCity_GameCorner_Text_HubBerries, MauvilleCity_GameCorner_Text_HubTraining, MauvilleCity_GameCorner_Text_HubTms, MauvilleCity_GameCorner_Text_HubOther, gText_Exit
	switch VAR_RESULT
	case 0, MauvilleCity_GameCorner_EventScript_HubHeld
	case 1, MauvilleCity_GameCorner_EventScript_HubBattle
	case 2, MauvilleCity_GameCorner_EventScript_HubMega
	case 3, MauvilleCity_GameCorner_EventScript_HubEvolution
	case 4, MauvilleCity_GameCorner_EventScript_HubBalls
	case 5, MauvilleCity_GameCorner_EventScript_HubHealing
	case 6, MauvilleCity_GameCorner_EventScript_HubBerries
	case 7, MauvilleCity_GameCorner_EventScript_HubTraining
	case 8, MauvilleCity_GameCorner_EventScript_HubTms
	case 9, MauvilleCity_GameCorner_EventScript_HubOther
	goto MauvilleCity_GameCorner_EventScript_RareGoodsLeave

MauvilleCity_GameCorner_EventScript_HubHeld::
	pokemart MauvilleCity_GameCorner_Pokemart_HubHeld
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubBattle::
	pokemart MauvilleCity_GameCorner_Pokemart_HubBattle
	goto MauvilleCity_GameCorner_EventScript_ItemHub

@ The Mega Stone dealer's stones at the dealer's prices; like the dealer, only for a player with a MEGA RING
MauvilleCity_GameCorner_EventScript_HubMega::
	checkitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_HubMegaNoRing
	pokemart MauvilleCity_Mart_Pokemart_MegaStones
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubMegaNoRing::
	msgbox MauvilleCity_GameCorner_Text_HubMegaNoRing, MSGBOX_DEFAULT
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubEvolution::
	pokemart MauvilleCity_GameCorner_Pokemart_HubEvolution
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubBalls::
	pokemart MauvilleCity_GameCorner_Pokemart_HubBalls
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubHealing::
	pokemart MauvilleCity_GameCorner_Pokemart_HubHealing
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubBerries::
	pokemart MauvilleCity_GameCorner_Pokemart_HubBerries
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubTraining::
	pokemart MauvilleCity_GameCorner_Pokemart_HubTraining
	goto MauvilleCity_GameCorner_EventScript_ItemHub

@ The TM CATALOGUE's four kinds (data/scripts/mp_tm_catalogue.inc), back to the hub's kinds at EXIT
MauvilleCity_GameCorner_EventScript_HubTms::
	message MauvilleCity_GameCorner_Text_TmCatalogueWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 5, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_TmPhysical, MauvilleCity_GameCorner_Text_TmSpecial, MauvilleCity_GameCorner_Text_TmStatus, MauvilleCity_GameCorner_Text_TmUtility, gText_Exit
	switch VAR_RESULT
	case 0, MauvilleCity_GameCorner_EventScript_HubTmPhysical
	case 1, MauvilleCity_GameCorner_EventScript_HubTmSpecial
	case 2, MauvilleCity_GameCorner_EventScript_HubTmStatus
	case 3, MauvilleCity_GameCorner_EventScript_HubTmUtility
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_EventScript_HubTmPhysical::
	pokemart MauvilleCity_GameCorner_Pokemart_TmPhysical
	goto MauvilleCity_GameCorner_EventScript_HubTms

MauvilleCity_GameCorner_EventScript_HubTmSpecial::
	pokemart MauvilleCity_GameCorner_Pokemart_TmSpecial
	goto MauvilleCity_GameCorner_EventScript_HubTms

MauvilleCity_GameCorner_EventScript_HubTmStatus::
	pokemart MauvilleCity_GameCorner_Pokemart_TmStatus
	goto MauvilleCity_GameCorner_EventScript_HubTms

MauvilleCity_GameCorner_EventScript_HubTmUtility::
	pokemart MauvilleCity_GameCorner_Pokemart_TmUtility
	goto MauvilleCity_GameCorner_EventScript_HubTms

MauvilleCity_GameCorner_EventScript_HubOther::
	pokemart MauvilleCity_GameCorner_Pokemart_HubOther
	goto MauvilleCity_GameCorner_EventScript_ItemHub

MauvilleCity_GameCorner_Text_ItemHubWhich:
	.string "ITEMS for COINS: the RARE GOODS and\n"
	.string "much more! Which kind?$"

MauvilleCity_GameCorner_Text_HubHeld:
	.string "HELD ITEMS$"

MauvilleCity_GameCorner_Text_HubBattle:
	.string "BATTLE ITEMS$"

MauvilleCity_GameCorner_Text_HubMega:
	.string "MEGA STONES$"

MauvilleCity_GameCorner_Text_HubEvolution:
	.string "EVOLUTION & FORMS$"

MauvilleCity_GameCorner_Text_HubBalls:
	.string "POKé BALLS$"

MauvilleCity_GameCorner_Text_HubHealing:
	.string "HEALING$"

MauvilleCity_GameCorner_Text_HubBerries:
	.string "BERRIES$"

MauvilleCity_GameCorner_Text_HubTraining:
	.string "TRAINING$"

MauvilleCity_GameCorner_Text_HubTms:
	.string "TMs$"

MauvilleCity_GameCorner_Text_HubOther:
	.string "OTHER$"

MauvilleCity_GameCorner_Text_HubMegaNoRing:
	.string "MEGA STONES are no use without a\n"
	.string "MEGA RING. The MAJOR PRIZES have one!$"

	.include "data/scripts/mp_item_hub_lists.inc"
