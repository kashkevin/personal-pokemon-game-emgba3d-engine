@ Shared world event Pokemon (host-scheduled World Events, owner rule:
@ the first player to catch it wins; running, losing or defeating it lets
@ everyone keep trying until its timer ends).
@ TryClaimEvent: VAR_RESULT 1 granted, 0 another trainer is battling it,
@ 2 unavailable (gone, disconnected or no answer from the host).
EventScript_MultiplayerWorldEvent::
	lock
	faceplayer
	special MultiplayerWorld_TryClaimEvent
	waitstate
	goto_if_eq VAR_RESULT, 0, EventScript_MultiplayerWorldEventBusy
	goto_if_ne VAR_RESULT, 1, EventScript_MultiplayerWorldEventGone
	special MultiplayerWorld_StartEventBattle
	waitstate
	special MultiplayerWorld_ReportEventResult
	release
	end

EventScript_MultiplayerWorldEventBusy:
	msgbox EventScript_MultiplayerWorldEvent_Text_Busy, MSGBOX_DEFAULT
	release
	end

EventScript_MultiplayerWorldEventGone:
	msgbox EventScript_MultiplayerWorldEvent_Text_Gone, MSGBOX_DEFAULT
	release
	end

EventScript_MultiplayerWorldEvent_Text_Busy:
	.string "Another TRAINER is battling\n"
	.string "this POKéMON right now!$"

EventScript_MultiplayerWorldEvent_Text_Gone:
	.string "The POKéMON can't be reached\n"
	.string "right now. Try again in a moment!$"
