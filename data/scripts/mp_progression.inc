@ Multiplayer progression rewards (owner amendments: Mega Evolution unlock,
@ purchasable Mega Stones, Shiny Charm as a guaranteed Game Corner prize).
@ Balance values: include/constants/multiplayer_balance.h.

@ Flannery hands over the Mega Ring with the 4th badge (also on a later visit
@ for saves that already had the badge).
EventScript_MultiplayerGiveMegaRing::
.ifdef PORTABLE
	@ Update 29 (PC game): the Mega Ring is a Game Corner prize now (25,000 coins). Nothing is given here, and a ring a player
	@ already owns stays theirs.
	return
.endif
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
.ifdef PORTABLE
	msgbox MauvilleCity_Mart_Text_MegaStoneNoRingCoins, MSGBOX_DEFAULT
	release
	end
.else
	goto_if_set FLAG_BADGE04_GET, MauvilleCity_Mart_EventScript_MegaStoneDealerRing
	msgbox MauvilleCity_Mart_Text_MegaStoneNoRing, MSGBOX_DEFAULT
	release
	end
.endif

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

@ Mauville City Game Corner: the major prize counter (Shiny Charm, and on the PC game the Mega Ring).
.ifdef PORTABLE
@ Update 29 (PC game). The clerk sells the MEGA RING and the SHINY CHARM for Game Corner coins (since 2026-10-25 3,750 and 15,000: MP_MEGA_RING_COINS, MP_SHINY_CHARM_COINS). A
@ prize the player already has is never offered again. VAR_TEMP_2 names the prize being offered (the native Game Corner screen
@ reads it). The prices are 32-bit: they travel in VAR_0x8004 (high) and VAR_0x8005 (low) to the CoinWallet specials.
@ The owner's request of 2026-10-24: the counter also sells the RARE GOODS (every item no player can get anywhere else, for
@ COINS; data/scripts/mp_rare_goods_shop.inc, tools/mp/gen_rare_goods.py). It asks first: MAJOR PRIZES (as before) or
@ RARE GOODS, whose kinds each open the coin shop (src/platform/coin_shop.c), and back to the kinds after a purchase.
MauvilleCity_GameCorner_EventScript_ShinyCharmClerk::
	lock
	faceplayer
	setvar VAR_TEMP_2, ITEM_NONE
	checkitem ITEM_COIN_CASE
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNoCase
	message MauvilleCity_GameCorner_Text_PrizeCounterWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 3, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_MajorPrizes, MauvilleCity_GameCorner_Text_RareGoods, gText_Exit
	switch VAR_RESULT
	case 1, MauvilleCity_GameCorner_EventScript_RareGoods
	case 2, MauvilleCity_GameCorner_EventScript_RareGoodsLeave
	case MULTI_B_PRESSED, MauvilleCity_GameCorner_EventScript_RareGoodsLeave
	msgbox MauvilleCity_GameCorner_Text_MajorPrizesIntro, MSGBOX_DEFAULT
	showcoinsbox 0, 0
	checkitem ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, TRUE, MauvilleCity_GameCorner_EventScript_CharmOffer
	setvar VAR_TEMP_2, ITEM_MEGA_RING
	msgbox MauvilleCity_GameCorner_Text_MegaRingConfirm, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, MauvilleCity_GameCorner_EventScript_CharmOffer
	setvar VAR_0x8004, (MP_MEGA_RING_COINS >> 16)
	setvar VAR_0x8005, (MP_MEGA_RING_COINS & 0xFFFF)
	specialvar VAR_RESULT, CoinWallet_Has
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_MegaRingNotEnough
	checkitemspace ITEM_MEGA_RING
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNoRoom
	specialvar VAR_RESULT, CoinWallet_Spend
	updatecoinsbox
	playse SE_SHOP
	giveitem ITEM_MEGA_RING
	msgbox MauvilleCity_GameCorner_Text_MegaRingGiven, MSGBOX_DEFAULT
MauvilleCity_GameCorner_EventScript_CharmOffer::
	checkitem ITEM_SHINY_CHARM
	goto_if_eq VAR_RESULT, TRUE, MauvilleCity_GameCorner_EventScript_AllPrizesOwned
	setvar VAR_TEMP_2, ITEM_SHINY_CHARM
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmConfirm, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, MauvilleCity_GameCorner_EventScript_ShinyCharmDeclined
	setvar VAR_0x8004, (MP_SHINY_CHARM_COINS_PC >> 16)
	setvar VAR_0x8005, (MP_SHINY_CHARM_COINS_PC & 0xFFFF)
	specialvar VAR_RESULT, CoinWallet_Has
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNotEnough
	checkitemspace ITEM_SHINY_CHARM
	goto_if_eq VAR_RESULT, FALSE, MauvilleCity_GameCorner_EventScript_ShinyCharmNoRoom
	specialvar VAR_RESULT, CoinWallet_Spend
	updatecoinsbox
	playse SE_SHOP
	hidecoinsbox
	giveitem ITEM_SHINY_CHARM
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmGiven, MSGBOX_DEFAULT
	setvar VAR_TEMP_2, ITEM_NONE
	release
	end

MauvilleCity_GameCorner_EventScript_RareGoods::
	message MauvilleCity_GameCorner_Text_RareGoodsWhich
	waitmessage
	dynmultichoice 0, 0, FALSE, 6, 0, DYN_MULTICHOICE_CB_NONE, MauvilleCity_GameCorner_Text_RareGoodsHeld, MauvilleCity_GameCorner_Text_RareGoodsBerries, MauvilleCity_GameCorner_Text_RareGoodsTraining, MauvilleCity_GameCorner_Text_RareGoodsEvolution, MauvilleCity_GameCorner_Text_RareGoodsOther, gText_Exit
	switch VAR_RESULT
	case 0, MauvilleCity_GameCorner_EventScript_RareGoodsHeld
	case 1, MauvilleCity_GameCorner_EventScript_RareGoodsBerries
	case 2, MauvilleCity_GameCorner_EventScript_RareGoodsTraining
	case 3, MauvilleCity_GameCorner_EventScript_RareGoodsEvolution
	case 4, MauvilleCity_GameCorner_EventScript_RareGoodsOther
MauvilleCity_GameCorner_EventScript_RareGoodsLeave::
	msgbox gText_PleaseComeAgain, MSGBOX_DEFAULT
	setvar VAR_TEMP_2, ITEM_NONE
	release
	end

MauvilleCity_GameCorner_EventScript_RareGoodsHeld::
	pokemart MauvilleCity_GameCorner_Pokemart_RareGoodsHeld
	goto MauvilleCity_GameCorner_EventScript_RareGoods

MauvilleCity_GameCorner_EventScript_RareGoodsBerries::
	pokemart MauvilleCity_GameCorner_Pokemart_RareGoodsBerries
	goto MauvilleCity_GameCorner_EventScript_RareGoods

MauvilleCity_GameCorner_EventScript_RareGoodsTraining::
	pokemart MauvilleCity_GameCorner_Pokemart_RareGoodsTraining
	goto MauvilleCity_GameCorner_EventScript_RareGoods

MauvilleCity_GameCorner_EventScript_RareGoodsEvolution::
	pokemart MauvilleCity_GameCorner_Pokemart_RareGoodsEvolution
	goto MauvilleCity_GameCorner_EventScript_RareGoods

MauvilleCity_GameCorner_EventScript_RareGoodsOther::
	pokemart MauvilleCity_GameCorner_Pokemart_RareGoodsOther
	goto MauvilleCity_GameCorner_EventScript_RareGoods

MauvilleCity_GameCorner_EventScript_AllPrizesOwned::
	hidecoinsbox
	msgbox MauvilleCity_GameCorner_Text_ShinyCharmOwned, MSGBOX_DEFAULT
	setvar VAR_TEMP_2, ITEM_NONE
	release
	end

MauvilleCity_GameCorner_EventScript_MegaRingNotEnough::
	msgbox MauvilleCity_GameCorner_Text_MegaRingNotEnough, MSGBOX_DEFAULT
	hidecoinsbox
	setvar VAR_TEMP_2, ITEM_NONE
	release
	end
.else
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

.endif

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

.ifdef PORTABLE
MauvilleCity_Mart_Text_MegaStoneWelcome:
	.string "MEGA STONES! Every one has its own\n"
	.string "price, paid in GAME CORNER COINS.$"
.else
MauvilleCity_Mart_Text_MegaStoneWelcome:
	.string "MEGA STONES! Choose carefully,\n"
	.string "they don't come cheap!$"
.endif

.ifdef PORTABLE
MauvilleCity_GameCorner_Text_ShinyCharmIntro:
	.string "This counter offers our grand prizes:\n"
	.string "the MEGA RING and the SHINY CHARM!$"
.else
MauvilleCity_GameCorner_Text_ShinyCharmIntro:
	.string "This counter offers our grand prize:\n"
	.string "the SHINY CHARM!\p"
	.string "Its owner meets SHINY POKéMON far\n"
	.string "more often. It costs 9,000 COINS.$"
.endif

.ifdef PORTABLE
MauvilleCity_GameCorner_Text_ShinyCharmConfirm:
	.string "The SHINY CHARM makes SHINY POKéMON\n"
	.string "turn up far more often.\p"
	.string "Trade 15,000 COINS for it?$"
.else
MauvilleCity_GameCorner_Text_ShinyCharmConfirm:
	.string "Trade 9,000 COINS for the\n"
	.string "SHINY CHARM?$"
.endif

.ifdef PORTABLE
MauvilleCity_GameCorner_Text_ShinyCharmNotEnough:
	.string "I'm sorry, you need 15,000 COINS.\n"
	.string "Good luck at the tables!$"
.else
MauvilleCity_GameCorner_Text_ShinyCharmNotEnough:
	.string "I'm sorry, you need 9,000 COINS.\n"
	.string "Good luck at the tables!$"
.endif

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

.ifdef PORTABLE
@ Update 29 (PC game): the major prize counter and the dealer.
MauvilleCity_GameCorner_Text_MajorPrizesIntro:
	.string "Welcome to the PRIZE COUNTER for\n"
	.string "our biggest prizes!$"

MauvilleCity_GameCorner_Text_MegaRingConfirm:
	.string "The MEGA RING lets a POKéMON holding\n"
	.string "its own MEGA STONE MEGA EVOLVE.\p"
	.string "Trade 3,750 COINS for it?$"

MauvilleCity_GameCorner_Text_MegaRingNotEnough:
	.string "I'm sorry, you need 3,750 COINS\n"
	.string "for the MEGA RING.$"

MauvilleCity_GameCorner_Text_MegaRingGiven:
	.string "The MEGA RING is yours to keep!\p"
	.string "MEGA STONES are sold at the\n"
	.string "MAUVILLE CITY POKé MART, for COINS.$"

MauvilleCity_GameCorner_Text_PrizeCounterWhich:
	.string "Welcome to the PRIZE COUNTER!\n"
	.string "What would you like to see?$"

MauvilleCity_GameCorner_Text_MajorPrizes:
	.string "MAJOR PRIZES$"

MauvilleCity_GameCorner_Text_RareGoods:
	.string "RARE GOODS$"

MauvilleCity_GameCorner_Text_RareGoodsWhich:
	.string "RARE GOODS: things you won't find\n"
	.string "anywhere else! Which kind?$"

MauvilleCity_GameCorner_Text_RareGoodsHeld:
	.string "HELD ITEMS$"

MauvilleCity_GameCorner_Text_RareGoodsBerries:
	.string "BERRIES$"

MauvilleCity_GameCorner_Text_RareGoodsTraining:
	.string "TRAINING$"

MauvilleCity_GameCorner_Text_RareGoodsEvolution:
	.string "EVOLUTION$"

MauvilleCity_GameCorner_Text_RareGoodsOther:
	.string "BALLS & OTHER$"

MauvilleCity_Mart_Text_MegaStoneNoRingCoins:
	.string "I deal in MEGA STONES, for GAME\n"
	.string "CORNER COINS.\p"
	.string "They're no use without a MEGA RING.\n"
	.string "The GAME CORNER's PRIZE COUNTER\l"
	.string "sells one for 3,750 COINS.$"
.endif
