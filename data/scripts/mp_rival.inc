@ Update 31 (native PC game only; data/event_scripts.s includes this file under PORTABLE, so the GBA ROM is unchanged).
@ Multiplayer rival checkpoints and the streamlined new-character introduction: include/multiplayer_rival.h,
@ src/multiplayer_rival.c. The map scripts of the five rival scenes call these with VAR_0x8004 = the checkpoint
@ (MP_RIVAL_ROUTE103 .. MP_RIVAL_LILYCOVE). Outside a multiplayer world whose host runs rival checkpoints,
@ MultiplayerRival_ScriptMode answers MP_RIVAL_MODE_ORIGINAL and the original scene plays unchanged.
@ The personal effects of a pass are the original scene's own (guarded by the save's own state, so they
@ happen once): the battle is the players' own native PvP battle, not a scripted NPC team.

@ ---------------------------------------------------------------------------------------------------------
@ The trigger lines of Route 110 and Route 119 (called at the top of the original scene).

MpRival_EventScript_TriggerLine::
	special MultiplayerRival_ScriptMode @ sets VAR_RESULT
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_ORIGINAL, MpRival_EventScript_Original
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_BLOCK, MpRival_EventScript_Gate
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_WAIT, MpRival_EventScript_GateWait
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_CATCH_UP, MpRival_EventScript_CatchUp
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_REWARD, MpRival_EventScript_LineReward
	goto MpRival_EventScript_StepOn

MpRival_EventScript_Original::
	return

@ A trigger tile whose scene does not happen must still let the player off it: a coordinate trigger fires again on
@ every frame the player stands on it mid-walk (the original scenes always move the player or change the variable).
@ The lines are walled at both ends, so a passing player walks on north or south; VAR_0x8005 = the facing direction.
MpRival_EventScript_StepOn::
	lockall
	call_if_eq VAR_0x8005, DIR_NORTH, MpRival_EventScript_StepNorth
	call_if_eq VAR_0x8005, DIR_SOUTH, MpRival_EventScript_StepSouth
	releaseall
	end

MpRival_EventScript_StepNorth::
	applymovement LOCALID_PLAYER, MpRival_Movement_StepNorth
	waitmovement 0
	return

MpRival_EventScript_StepSouth::
	applymovement LOCALID_PLAYER, MpRival_Movement_StepSouth
	waitmovement 0
	return

MpRival_Movement_StepNorth:
	walk_up
	step_end

MpRival_Movement_StepSouth:
	walk_down
	step_end

@ The world has not completed this checkpoint and the player has not passed it: stop, step back, and open
@ the Players menu, whose Rival checkpoint row has Start Now / Wait. Nothing here waits for the network,
@ so the player is never left locked.
MpRival_EventScript_Gate::
	lockall
	playse SE_PIN
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	waitmovement 0
	msgbox MpRival_Text_RivalsMeetHere, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, MpRival_Movement_StepBack
	waitmovement 0
	special MultiplayerRival_RequestMenu
	releaseall
	end

MpRival_EventScript_GateWait::
	lockall
	msgbox MpRival_Text_CheckingRivals, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, MpRival_Movement_StepBack
	waitmovement 0
	releaseall
	end

@ Completed by the world while this player was away: never a barrier; the banner says how to catch up.
MpRival_EventScript_CatchUp::
	special MultiplayerRival_CatchUpNotice
	goto MpRival_EventScript_StepOn

MpRival_EventScript_LineReward::
	goto_if_eq VAR_0x8004, MP_RIVAL_ROUTE110, MpRival_EventScript_RewardRoute110
	goto_if_eq VAR_0x8004, MP_RIVAL_ROUTE119, MpRival_EventScript_RewardRoute119
	end

MpRival_Movement_StepBack:
	walk_down
	face_up
	step_end

@ Route 110 (Route110_EventScript_RivalScene, Route110_EventScript_GiveItemfinder, Route110_EventScript_RivalExit)
MpRival_EventScript_RewardRoute110::
	lockall
	playse SE_PIN
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	waitmovement 0
	msgbox MpRival_Text_Route110Passed, MSGBOX_DEFAULT
	giveitem ITEM_DOWSING_MACHINE
	msgbox MpRival_Text_ExplainDowsingMachine, MSGBOX_DEFAULT
	closemessage
	setflag FLAG_HIDE_ROUTE_110_RIVAL
	setflag FLAG_HIDE_ROUTE_110_RIVAL_ON_BIKE
	setvar VAR_ROUTE110_STATE, 1
	releaseall
	end

@ Route 119 (Route119_EventScript_GiveFlyHM, Route119_EventScript_RivalExitScottArrive): HM02 Fly, then Scott,
@ who arrives as he does after the original battle (VAR_SCOTT_STATE counts his scenes).
MpRival_EventScript_RewardRoute119::
	lockall
	playse SE_PIN
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	waitmovement 0
	msgbox MpRival_Text_Route119Passed, MSGBOX_DEFAULT
	giveitem ITEM_HM_FLY
	setflag FLAG_RECEIVED_HM_FLY
	msgbox MpRival_Text_ExplainFly, MSGBOX_DEFAULT
	closemessage
	setflag FLAG_HIDE_ROUTE_119_RIVAL
	setflag FLAG_HIDE_ROUTE_119_RIVAL_ON_BIKE
	setvar VAR_ROUTE119_STATE, 1
	delay 60
	call_if_ne VAR_TEMP_1, 2, Route119_EventScript_SetScottPos1
	call_if_eq VAR_TEMP_1, 2, Route119_EventScript_SetScottPos2
	addobject LOCALID_ROUTE119_SCOTT
	applymovement LOCALID_ROUTE119_SCOTT, Route119_Movement_ScottEnter
	waitmovement 0
	addvar VAR_SCOTT_STATE, 1
	msgbox MpRival_Text_ScottSawTheBattle, MSGBOX_DEFAULT
	closemessage
	call_if_ne VAR_TEMP_1, 2, Route119_EventScript_ScottExit1
	call_if_eq VAR_TEMP_1, 2, Route119_EventScript_ScottExit2
	removeobject LOCALID_ROUTE119_SCOTT
	releaseall
	end

@ ---------------------------------------------------------------------------------------------------------
@ The talk-to rivals (Route 103, Rustboro City, Route 104, Lilycove City) and Rustboro's optional triggers.
@ In multiplayer the rival objects stay away (MultiplayerRival_HidesObject); these cover one already on screen.

MpRival_EventScript_TalkObject::
	special MultiplayerRival_ScriptMode @ sets VAR_RESULT
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_ORIGINAL, MpRival_EventScript_Original
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_REWARD, MpRival_EventScript_TalkReward
	lockall
	msgbox MpRival_Text_YourRivalsArePlayers, MSGBOX_DEFAULT
	releaseall
	end

MpRival_EventScript_TalkReward::
	goto_if_eq VAR_0x8004, MP_RIVAL_ROUTE103, MpRival_EventScript_RewardRoute103
	goto_if_eq VAR_0x8004, MP_RIVAL_RUSTBORO, MpRival_EventScript_RewardRustboro
	goto_if_eq VAR_0x8004, MP_RIVAL_LILYCOVE, MpRival_EventScript_RewardLilycove
	end

MpRival_EventScript_OptionalTrigger::
	special MultiplayerRival_ScriptMode @ sets VAR_RESULT
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_ORIGINAL, MpRival_EventScript_Original
	goto_if_eq VAR_RESULT, MP_RIVAL_MODE_REWARD, MpRival_EventScript_RewardRustboro
	goto MpRival_EventScript_StepOn

@ Route 103 (Route103_EventScript_RivalEnd). Birch's lab then gives the Pokedex and the Poke Balls itself.
MpRival_EventScript_RewardRoute103::
	lockall
	playse SE_PIN
	applymovement LOCALID_PLAYER, Common_Movement_ExclamationMark
	waitmovement 0
	msgbox MpRival_Text_Route103Passed, MSGBOX_DEFAULT
	closemessage
	removeobject LOCALID_ROUTE103_RIVAL
	setvar VAR_BIRCH_LAB_STATE, 4
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_RIVAL
	setflag FLAG_DEFEATED_RIVAL_ROUTE103
	setvar VAR_OLDALE_RIVAL_STATE, 1
	clearflag FLAG_HIDE_OLDALE_TOWN_RIVAL
	releaseall
	end

@ Rustboro City / Route 104 (RustboroCity_EventScript_MayEncounter): the rival's Match Call registration.
MpRival_EventScript_RewardRustboro::
	lockall
	msgbox MpRival_Text_RustboroPassed, MSGBOX_DEFAULT
	closemessage
	setflag FLAG_MET_RIVAL_RUSTBORO
	delay 30
	playfanfare MUS_REGISTER_MATCH_CALL
	checkplayergender
	call_if_eq VAR_RESULT, MALE, MpRival_EventScript_RegisteredMay
	call_if_eq VAR_RESULT, FEMALE, MpRival_EventScript_RegisteredBrendan
	waitfanfare
	closemessage
	setflag FLAG_ENABLE_RIVAL_MATCH_CALL
	setvar VAR_RUSTBORO_CITY_STATE, 8
	setvar VAR_ROUTE104_STATE, 2
	releaseall
	end

MpRival_EventScript_RegisteredMay::
	msgbox RustboroCity_Text_RegisteredMay, MSGBOX_DEFAULT
	return

MpRival_EventScript_RegisteredBrendan::
	msgbox RustboroCity_Text_RegisteredBrendan, MSGBOX_DEFAULT
	return

@ Lilycove City (LilycoveCity_EventScript_DefeatedMay / DefeatedBrendan, RivalFlyAway): the rival goes home.
MpRival_EventScript_RewardLilycove::
	lockall
	msgbox MpRival_Text_LilycovePassed, MSGBOX_DEFAULT
	closemessage
	checkplayergender
	call_if_eq VAR_RESULT, MALE, MpRival_EventScript_MayGoesHome
	call_if_eq VAR_RESULT, FEMALE, MpRival_EventScript_BrendanGoesHome
	setflag FLAG_HIDE_LILYCOVE_CITY_RIVAL
	setflag FLAG_MET_RIVAL_LILYCOVE
	releaseall
	end

MpRival_EventScript_MayGoesHome::
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_2F_PICHU_DOLL
	return

MpRival_EventScript_BrendanGoesHome::
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_RIVAL_BEDROOM
	clearflag FLAG_HIDE_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F_SWABLU_DOLL
	return

@ ---------------------------------------------------------------------------------------------------------
@ The streamlined introduction: name and look (Birch's speech), the 18-starter screen, then here, in front of
@ the player's own house, at the moment Birch asks the player to go to Route 103 (VAR_BIRCH_LAB_STATE 3).
@ The Pokedex, the Poke Balls and the Running Shoes are still earned the original way.

@ Update 32, the short introduction: the new character arrives in Littleroot with no Pokemon, is told where
@ the starters are and the game is saved.
MpIntro_EventScript_Arrive::
	lockall
	msgbox MpIntro_Text_Welcome, MSGBOX_DEFAULT
	special MultiplayerIntro_Save
	goto_if_eq VAR_RESULT, FALSE, MpIntro_EventScript_SaveFailed
	playse SE_SAVE
	waitse
	msgbox MpIntro_Text_Saved, MSGBOX_DEFAULT
	releaseall
	end

MpIntro_EventScript_Nickname::
	setvar VAR_0x8004, 0
	call Common_EventScript_NameReceivedPartyMon
	return

MpIntro_EventScript_SaveFailed::
	msgbox gText_SaveError, MSGBOX_DEFAULT
	releaseall
	end

@ The path to Route 101 with no Pokemon yet (multiplayer_rival.c MultiplayerIntro_TryStartGuardScript).
MpIntro_EventScript_NeedPokemon::
	lockall
	msgbox MpIntro_Text_NeedPokemon, MSGBOX_DEFAULT
	closemessage
	applymovement LOCALID_PLAYER, MpIntro_Movement_StepBack
	waitmovement 0
	releaseall
	end

MpIntro_Movement_StepBack:
	walk_down
	step_end

@ A starter Poke Ball on the table in Birch's lab: the 18-starter screen, then the starter joins the party.
@ Nothing is saved here: the save of the arrival (or the player's own) is the one a soft reset goes back to.
MpIntro_EventScript_LabBall::
	lock
	special MultiplayerIntro_ChooseLabStarter
	special MultiplayerIntro_GiveLabStarter
	goto_if_eq VAR_RESULT, FALSE, MpIntro_EventScript_LabBallNotTaken
	removeobject LOCALID_BIRCHS_LAB_CHIKORITA_BALL
	removeobject LOCALID_BIRCHS_LAB_CYNDAQUIL_BALL
	removeobject LOCALID_BIRCHS_LAB_TOTODILE_BALL
	playfanfare MUS_OBTAIN_ITEM
	message MpIntro_Text_ReceivedStarter
	waitmessage
	waitfanfare
	msgbox MpIntro_Text_Nickname, MSGBOX_YESNO
	call_if_eq VAR_RESULT, YES, MpIntro_EventScript_Nickname
	msgbox MpIntro_Text_GoToRoute103, MSGBOX_DEFAULT
	release
	end

MpIntro_EventScript_LabBallNotTaken::
	release
	end

MpIntro_EventScript_LabBirch::
	lock
	faceplayer
	msgbox MpIntro_Text_BirchChooseOne, MSGBOX_DEFAULT
	release
	end

@ ---------------------------------------------------------------------------------------------------------

MpRival_Text_RivalsMeetHere:
	.string "Your rivals meet here!\p"
	.string "Battle them before you go on:\n"
	.string "open PLAYERS, then RIVAL CHECKPOINT.$"

MpRival_Text_CheckingRivals:
	.string "Checking for your rivals…\n"
	.string "Try again in a moment.$"

MpRival_Text_YourRivalsArePlayers:
	.string "Your rivals are the other players\n"
	.string "now: PLAYERS, then RIVAL CHECKPOINT.$"

MpRival_Text_Route110Passed:
	.string "Your rival battle on ROUTE 110\n"
	.string "is behind you!\p"
	.string "The rival left something useful\n"
	.string "for you here.$"

MpRival_Text_ExplainDowsingMachine:
	.string "The DOWSING MACHINE reacts to items\n"
	.string "hidden on the ground.\p"
	.string "Use it from the KEY ITEMS pocket\n"
	.string "of your BAG.$"

MpRival_Text_Route119Passed:
	.string "Your rival battle on ROUTE 119\n"
	.string "is behind you!\p"
	.string "The rival left a present for you.$"

MpRival_Text_ExplainFly:
	.string "HM02 is FLY.\p"
	.string "With the FEATHER BADGE from FORTREE\n"
	.string "GYM, it flies you to any town you\l"
	.string "have visited.$"

MpRival_Text_ScottSawTheBattle:
	.string "SCOTT: Hahahah!\n"
	.string "Way to go, {PLAYER}{KUN}!\p"
	.string "I saw that rival battle just now.\n"
	.string "What a match!\p"
	.string "Keep it up. I'll be seeing you!$"

MpRival_Text_Route103Passed:
	.string "Your first rival battle is behind\n"
	.string "you!\p"
	.string "PROF. BIRCH will want to hear about\n"
	.string "it. Go back to his LAB in LITTLEROOT.$"

MpRival_Text_RustboroPassed:
	.string "You met your rivals in RUSTBORO!$"

MpRival_Text_LilycovePassed:
	.string "Your rival battle in LILYCOVE CITY\n"
	.string "is behind you!$"

MpIntro_Text_Welcome:
	.string "Welcome to LITTLEROOT TOWN, {PLAYER}!\p"
	.string "Your first POKéMON is waiting in\n"
	.string "PROF. BIRCH's LAB, south of here.\p"
	.string "Choose one from the POKé BALLS\n"
	.string "on the table there.$"

MpIntro_Text_NeedPokemon:
	.string "There are wild POKéMON in the tall\n"
	.string "grass of ROUTE 101!\p"
	.string "Get your first POKéMON at\n"
	.string "PROF. BIRCH's LAB before you go.$"

MpIntro_Text_ReceivedStarter:
	.string "{PLAYER} received the {STR_VAR_1}!$"

MpIntro_Text_BirchChooseOne:
	.string "PROF. BIRCH: Welcome, {PLAYER}!\p"
	.string "The POKé BALLS on that table hold\n"
	.string "POKéMON from every region I know.\p"
	.string "Choose the one you like best as your\n"
	.string "partner. It's yours to keep!$"

MpIntro_Text_Nickname:
	.string "Give a nickname to the\n"
	.string "{STR_VAR_1}?$"

MpIntro_Text_GoToRoute103:
	.string "PROF. BIRCH asked you to meet your\n"
	.string "rival on ROUTE 103, north of\l"
	.string "OLDALE TOWN. Your adventure begins!$"

MpIntro_Text_Saved:
	.string "{PLAYER} saved the game.$"
