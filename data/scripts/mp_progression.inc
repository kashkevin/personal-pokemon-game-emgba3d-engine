@ Multiplayer progression rewards (owner amendments: Mega Evolution unlock,
@ purchasable Mega Stones, Shiny Charm as a guaranteed Game Corner prize).
@ Balance values: include/constants/multiplayer_balance.h.

@ Flannery hands over the Mega Ring with the 4th badge (also on a later visit
@ for saves that already had the badge).
EventScript_MultiplayerGiveMegaRing::
	checkitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, TRUE, EventScript_MultiplayerGiveMegaRingDone
	msgbox EventScript_MultiplayerMegaRing_Text_Offer, MSGBOX_DEFAULT
	giveitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, FALSE, EventScript_MultiplayerGiveMegaRingDone
	msgbox EventScript_MultiplayerMegaRing_Text_Explain, MSGBOX_DEFAULT
EventScript_MultiplayerGiveMegaRingDone:
	return

@ Mauville City Poke Mart: the Mega Stone dealer.
MauvilleCity_Mart_EventScript_MegaStoneDealer::
	lock
	faceplayer
	checkitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, TRUE, MauvilleCity_Mart_EventScript_MegaStoneShop
	goto_if_set FLAG_BADGE04_GET, MauvilleCity_Mart_EventScript_MegaStoneDealerRing
	msgbox MauvilleCity_Mart_Text_MegaStoneNoRing, MSGBOX_DEFAULT
	release
	end

MauvilleCity_Mart_EventScript_MegaStoneDealerRing::
	msgbox MauvilleCity_Mart_Text_MegaStoneFlanneryRing, MSGBOX_DEFAULT
	giveitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_Mart_EventScript_MegaStoneDealerEnd
	msgbox EventScript_MultiplayerMegaRing_Text_Explain, MSGBOX_DEFAULT
MauvilleCity_Mart_EventScript_MegaStoneShop::
	message MauvilleCity_Mart_Text_MegaStoneWelcome
	waitmessage
	pokemart MauvilleCity_Mart_Pokemart_MegaStones
	msgbox gText_PleaseComeAgain, MSGBOX_DEFAULT
MauvilleCity_Mart_EventScript_MegaStoneDealerEnd:
	release
	end

@ Mauville City Game Corner: the Shiny Charm prize counter.
MauvilleCity_GameCorner_EventScript_ShinyCharmClerk::
	lock
	faceplayer
	checkitem ITEM_SHINY_CHARM
	goto_if_eq VAR_RESULT, TRUE, MauvilleCity_GameCorner_EventScript_ShinyCharmOwned
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmIntro, MSGBOX_DEFAULT
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNoCase
	showcoinsbox 0, 0
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmConfirm, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, MauvilleCity_GameCorner_EventScript_ShinyCharmDeclined
	checkcoins VAR_TEMP_2
	goto_if_lt VAR_TEMP_2, MP_SHINY_CHARM_COINS, MauvilleCity_GameCorner_EventScript_ShinyCharmNotEnough
	checkitemspace ITEM_SHINY_CHARM
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNoRoom
	removecoins MP_SHINY_CHARM_COINS
	updatecoinsbox
	playse SE_SHOP
	hidecoinsbox
	giveitem ITEM_SHINY_CHARM
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmGiven, MSGBOX_DEFAULT
	release
	end

MauvilleCity_GameCorner_EventScript_ShinyCharmOwned::
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmOwned, MSGBOX_DEFAULT
	release
	end

MauvilleCity_GameCorner_EventScript_ShinyCharmNoCase::
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmNeedCase, MSGBOX_DEFAULT
	release
	end

MauvilleCity_GameCorner_EventScript_ShinyCharmDeclined::
	hidecoinsbox
	msgbox MauvilleCity_GameCorner_Text_OhIsThatSo, MSGBOX_DEFAULT
	release
	end

MauvilleCity_GameCorner_EventScript_ShinyCharmNotEnough::
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmNotEnough, MSGBOX_DEFAULT
	hidecoinsbox
	release
	end

MauvilleCity_GameCorner_EventScript_ShinyCharmNoRoom::
	call Common_EventScript_BagIsFull
	hidecoinsbox
	release
	end

EventScript_MultiplayerMegaRing_Text_Offer:
	.string "FLANNERY: One more thing!\p"
	.string "Trainers who burn this brightly\n"
	.string "deserve a MEGA RING. Take it!$"

EventScript_MultiplayerMegaRing_Text_Explain:
	.string "With the MEGA RING, a POKéMON holding\n"
	.string "its own MEGA STONE can MEGA EVOLVE,\l"
	.string "once per battle.\p"
	.string "MEGA STONES are sold at the\n"
	.string "MAUVILLE CITY POKé MART.$"

MauvilleCity_Mart_Text_MegaStoneNoRing:
	.string "I deal in MEGA STONES.\p"
	.string "They're no use without a MEGA RING.\n"
	.string "LAVARIDGE's GYM LEADER awards one\l"
	.string "to Trainers who earn her badge.$"

MauvilleCity_Mart_Text_MegaStoneFlanneryRing:
	.string "Ah, you have the HEAT BADGE!\p"
	.string "FLANNERY left this MEGA RING here\n"
	.string "for you. Please take it!$"

MauvilleCity_Mart_Text_MegaStoneWelcome:
	.string "MEGA STONES! Choose carefully,\n"
	.string "they don't come cheap!$"

MauvilleCity_GameCorner_Text_ShinyCharmIntro:
	.string "This counter offers our grand prize:\n"
	.string "the SHINY CHARM!\p"
	.string "Its owner meets SHINY POKéMON far\n"
	.string "more often. It costs 9,000 COINS.$"

MauvilleCity_GameCorner_Text_ShinyCharmConfirm:
	.string "Trade 9,000 COINS for the\n"
	.string "SHINY CHARM?$"

MauvilleCity_GameCorner_Text_ShinyCharmNotEnough:
	.string "I'm sorry, you need 9,000 COINS.\n"
	.string "Good luck at the tables!$"

MauvilleCity_GameCorner_Text_ShinyCharmNeedCase:
	.string "Prizes are paid in COINS.\n"
	.string "You'll need a COIN CASE first.$"

MauvilleCity_GameCorner_Text_ShinyCharmGiven:
	.string "Congratulations! The SHINY CHARM\n"
	.string "is yours to keep.\p"
	.string "It only works for you, so keep\n"
	.string "an eye out for sparkles!$"

MauvilleCity_GameCorner_Text_ShinyCharmOwned:
	.string "The SHINY CHARM suits you!\n"
	.string "Happy hunting!$"
