@ Shared world event Pokemon (host-scheduled World Events, owner rule:
@ the first player to catch it wins; running, losing or defeating it lets
@ everyone keep trying until its timer ends).
EventScript_MultiplayerWorldEvent::
	lock
	faceplayer
	special MultiplayerWorld_TryClaimEvent
	waitstate
	goto_if_eq VAR_RESULT, 0, EventScript_MultiplayerWorldEventBusy
	special MultiplayerWorld_StartEventBattle
	waitstate
	special MultiplayerWorld_ReportEventResult
	release
	end

EventScript_MultiplayerWorldEventBusy:
	msgbox EventScript_MultiplayerWorldEvent_Text_Busy, MSGBOX_DEFAULT
	release
	end

EventScript_MultiplayerWorldEvent_Text_Busy:
	.string "Another TRAINER is battling\n"
	.string "this POKéMON right now!$"
