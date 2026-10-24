@ Battle Tower: Legends (PC game only; src/tower_legends.c). A solo floor: the host answered the Players menu's choice
@ with the floor's command and the field is calm. The floor's intro, the battle against its trainer (no whiteout, no
@ money; the party is healed and its held items restored afterwards), then the result. The host settles the result,
@ confirms the progress and pays the prizes on its own (the wallet stream).
EventScript_TowerLegendsSolo::
	lockall
	special TowerLegends_BufferIntro
	message gStringVar4
	waitmessage
	waitbuttonpress
	closemessage
	special TowerLegends_StartSoloBattle @ waits for the battle (def_special waitstate=1)
	special TowerLegends_BufferResult
	msgbox gStringVar4, MSGBOX_DEFAULT
	@ The owner's request (2026-10-24): "go on to floor N?" - YES saves on the host and starts that floor through the
	@ host (the Players menu's page 4), NO saves on the host (TowerLegends_Continue, once the script has ended).
	special TowerLegends_BufferContinue
	goto_if_eq VAR_RESULT, FALSE, EventScript_TowerLegendsSoloEnd
	msgbox gStringVar4, MSGBOX_YESNO
	special TowerLegends_Continue
EventScript_TowerLegendsSoloEnd:
	releaseall
	end
