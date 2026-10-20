@ Update 32: START > PLAYER > RESET in a game started on its own (no world): the classic soft reset after a YES.
@ In a world the Players menu asks instead and the launcher starts the game again from the host's save
@ (src/multiplayer_soft_reset.c). NO is the default answer.
EventScript_SoftResetConfirm::
	lockall
	message Text_SoftResetConfirm
	waitmessage
	multichoicedefault 20, 8, MULTI_YESNO, 1, FALSE
	goto_if_eq VAR_RESULT, 0, EventScript_SoftResetYes
	closemessage
	releaseall
	end

EventScript_SoftResetYes:
	closemessage
	special MultiplayerSoftReset_Do
	releaseall
	end

Text_SoftResetConfirm:
	.string "Reset the game to your last save?\n"
	.string "Progress since then will be lost.$"
