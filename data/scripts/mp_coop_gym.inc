@ Multiplayer co-op Gym battles (owner brief "GYMS AND TWIN GYM LEADERS").
@ A solo challenger keeps the ordinary Gym battle. When the player's
@ teammate is in the same Gym, the Leader offers a team Double Battle against
@ the Leader and a partner. The host issues a native co-op challenge that both
@ trainers accept and enter from the PLAYERS menu. Each game then grants its
@ own rewards: the Gym's original victory script for a first win (badge, TM,
@ story flags) and the Leader's prize money for every winner.

@ VAR_0x8004 = MP_COOP_GYM_*. Returns to the caller for the ordinary battle.
EventScript_MultiplayerCoopGymOffer::
	special MultiplayerCoop_CanOffer
	goto_if_ne VAR_RESULT, MP_COOP_OFFER_READY, EventScript_MultiplayerCoopGymOfferReturn
	lock
	faceplayer
	special MultiplayerCoop_BufferPartnerName
	msgbox gText_MultiplayerCoopGymOffer, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, EventScript_MultiplayerCoopGymOfferReturn
	special MultiplayerCoop_RequestBattle
	waitstate
	goto_if_eq VAR_RESULT, FALSE, EventScript_MultiplayerCoopGymOfferFailed
	msgbox gText_MultiplayerCoopGymRequested, MSGBOX_DEFAULT
	release
	end

EventScript_MultiplayerCoopGymOfferFailed::
	msgbox gText_MultiplayerCoopGymRequestFailed, MSGBOX_DEFAULT
	release
	end

EventScript_MultiplayerCoopGymOfferReturn::
	return

@ Started by the game itself after a co-op Gym victory (VAR_0x8004 = Gym).
EventScript_MultiplayerCoopVictory::
	lockall
	special MultiplayerCoop_GivePrizeMoney
	playse SE_SHOP
	msgbox gText_MultiplayerCoopPrize, MSGBOX_DEFAULT
	switch VAR_0x8004
	case MP_COOP_GYM_RUSTBORO, EventScript_MultiplayerCoopReward_Rustboro
	case MP_COOP_GYM_DEWFORD, EventScript_MultiplayerCoopReward_Dewford
	case MP_COOP_GYM_MAUVILLE, EventScript_MultiplayerCoopReward_Mauville
	case MP_COOP_GYM_LAVARIDGE, EventScript_MultiplayerCoopReward_Lavaridge
	case MP_COOP_GYM_PETALBURG, EventScript_MultiplayerCoopReward_Petalburg
	case MP_COOP_GYM_FORTREE, EventScript_MultiplayerCoopReward_Fortree
	case MP_COOP_GYM_MOSSDEEP, EventScript_MultiplayerCoopReward_Mossdeep
	case MP_COOP_GYM_SOOTOPOLIS, EventScript_MultiplayerCoopReward_Sootopolis
	goto EventScript_MultiplayerCoopHelper
	end

@ A first victory runs the Gym's own victory script, exactly as after a solo
@ win. A trainer who already beat this Leader (or, for Norman, could not yet
@ challenge him alone) helped a teammate and keeps only the prize money.
EventScript_MultiplayerCoopReward_Rustboro::
	goto_if_defeated TRAINER_ROXANNE_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_ROXANNE_1
	goto RustboroCity_Gym_EventScript_RoxanneDefeated
	end

EventScript_MultiplayerCoopReward_Dewford::
	goto_if_defeated TRAINER_BRAWLY_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_BRAWLY_1
	goto DewfordTown_Gym_EventScript_BrawlyDefeated
	end

EventScript_MultiplayerCoopReward_Mauville::
	goto_if_defeated TRAINER_WATTSON_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_WATTSON_1
	goto MauvilleCity_Gym_EventScript_WattsonDefeated
	end

EventScript_MultiplayerCoopReward_Lavaridge::
	goto_if_defeated TRAINER_FLANNERY_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_FLANNERY_1
	goto LavaridgeTown_Gym_1F_EventScript_FlanneryDefeated
	end

EventScript_MultiplayerCoopReward_Petalburg::
	goto_if_defeated TRAINER_NORMAN_1, EventScript_MultiplayerCoopHelper
	goto_if_ne VAR_PETALBURG_GYM_STATE, 6, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_NORMAN_1
	goto PetalburgCity_Gym_EventScript_NormanDefeated
	end

EventScript_MultiplayerCoopReward_Fortree::
	goto_if_defeated TRAINER_WINONA_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_WINONA_1
	goto FortreeCity_Gym_EventScript_WinonaDefeated
	end

EventScript_MultiplayerCoopReward_Mossdeep::
	goto_if_defeated TRAINER_TATE_AND_LIZA_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_TATE_AND_LIZA_1
	goto MossdeepCity_Gym_EventScript_TateAndLizaDefeated
	end

EventScript_MultiplayerCoopReward_Sootopolis::
	goto_if_defeated TRAINER_JUAN_1, EventScript_MultiplayerCoopHelper
	settrainerflag TRAINER_JUAN_1
	goto SootopolisCity_Gym_1F_EventScript_JuanDefeated
	end

EventScript_MultiplayerCoopHelper::
	msgbox gText_MultiplayerCoopHelped, MSGBOX_DEFAULT
	releaseall
	end

@ Started by the game itself after a co-op Gym loss or draw. Native battles
@ never change a party, so there is no whiteout.
EventScript_MultiplayerCoopDefeat::
	lockall
	msgbox gText_MultiplayerCoopDefeat, MSGBOX_DEFAULT
	releaseall
	end

gText_MultiplayerCoopGymOffer::
	.string "Two challengers at once?\n"
	.string "Then my partner {STR_VAR_2} joins in!\p"
	.string "Will you and your teammate take us\n"
	.string "on together in a Double Battle?$"

gText_MultiplayerCoopGymRequested::
	.string "Wonderful! Open the PLAYERS menu\n"
	.string "with your teammate, accept the\l"
	.string "challenge and choose ENTER BATTLE!$"

gText_MultiplayerCoopGymRequestFailed::
	.string "Hmm… Your teammate can't be\n"
	.string "reached right now. Try again soon!$"

gText_MultiplayerCoopPrize::
	.string "You won together!\n"
	.string "You got ¥{STR_VAR_1} for winning!$"

gText_MultiplayerCoopHelped::
	.string "Thanks for battling alongside\n"
	.string "your teammate!$"

gText_MultiplayerCoopDefeat::
	.string "You and your teammate were\n"
	.string "defeated… Train up and try again!$"
