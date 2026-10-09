MarineCave_End_MapScripts::
	map_script MAP_SCRIPT_ON_RESUME, MarineCave_End_OnResume
	map_script MAP_SCRIPT_ON_TRANSITION, MarineCave_End_OnTransition
	.byte 0

MarineCave_End_OnResume:
	call_if_set FLAG_SYS_CTRL_OBJ_DELETE, MarineCave_End_EventScript_TryRemoveKyogre
	end

MarineCave_End_EventScript_TryRemoveKyogre::
	specialvar VAR_RESULT, GetBattleOutcome
	goto_if_ne VAR_RESULT, B_OUTCOME_CAUGHT, Common_EventScript_NopReturn
	removeobject LOCALID_MARINE_CAVE_KYOGRE
	return

MarineCave_End_OnTransition:
	call_if_unset FLAG_DEFEATED_KYOGRE, MarineCave_End_EventScript_ShowKyogre
	end

MarineCave_End_EventScript_ShowKyogre::
	clearflag FLAG_HIDE_MARINE_CAVE_KYOGRE
	setvar VAR_TEMP_1, 1
	return

MarineCave_End_EventScript_Kyogre::
	lockall
	applymovement LOCALID_PLAYER, Common_Movement_FaceUp
	waitmovement 0
	applymovement LOCALID_MARINE_CAVE_KYOGRE, MarineCave_End_Movement_KyogreApproach
	waitmovement 0
	waitse
	playmoncry SPECIES_KYOGRE, CRY_MODE_ENCOUNTER
	delay 40
	waitmoncry
	setvar VAR_LAST_TALKED, LOCALID_MARINE_CAVE_KYOGRE
#if MP_LOCAL_RESET_STATIC || TESTING
MarineCave_End_EventScript_KyogreAdmission::
	special MultiplayerStaticReset_BeginCurrent
	goto_if_ne VAR_RESULT, MP_LOCAL_RESET_ADMISSION_PENDING, MarineCave_End_EventScript_KyogreAdmissionReady
	waitstate
MarineCave_End_EventScript_KyogreAdmissionReady::
	goto_if_eq VAR_RESULT, FALSE, MarineCave_End_EventScript_KyogreResetReturn
#endif
	setwildbattle SPECIES_KYOGRE, 70
	setflag FLAG_SYS_CTRL_OBJ_DELETE
	special BattleSetup_StartLegendaryBattle
MarineCave_End_EventScript_KyogrePersonalReturn::
	clearflag FLAG_SYS_CTRL_OBJ_DELETE
#if MP_LOCAL_RESET_STATIC || TESTING
	special MultiplayerLocalReset_ConsumeResult
	goto_if_eq VAR_RESULT, TRUE, MarineCave_End_EventScript_KyogreResetReturn
#endif
	setvar VAR_TEMP_1, 0
	specialvar VAR_RESULT, GetBattleOutcome
	goto_if_eq VAR_RESULT, B_OUTCOME_WON, MarineCave_End_EventScript_DefeatedKyogre
	goto_if_eq VAR_RESULT, B_OUTCOME_RAN, MarineCave_End_EventScript_RanFromKyogre
	goto_if_eq VAR_RESULT, B_OUTCOME_PLAYER_TELEPORTED, MarineCave_End_EventScript_RanFromKyogre
	setvar VAR_SHOULD_END_ABNORMAL_WEATHER, 1
	setflag FLAG_DEFEATED_KYOGRE
	releaseall
	end

#if MP_LOCAL_RESET_STATIC || TESTING
MarineCave_End_EventScript_KyogreResetReturn::
	special MultiplayerStaticReset_ReturnWeatherSource
	releaseall
	end
#endif

MarineCave_End_EventScript_DefeatedKyogre::
	setvar VAR_SHOULD_END_ABNORMAL_WEATHER, 1
	setflag FLAG_DEFEATED_KYOGRE
	goto Common_EventScript_RemoveStaticPokemon
	end

MarineCave_End_EventScript_RanFromKyogre::
	setvar VAR_0x8004, SPECIES_KYOGRE
	goto Common_EventScript_LegendaryFlewAway
	end

MarineCave_End_Movement_KyogreApproach:
	init_affine_anim
	walk_down_start_affine
	delay_16
	delay_16
	walk_down_affine
	delay_16
	delay_16
	walk_down_affine
	step_end
