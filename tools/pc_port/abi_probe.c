// ABI layout probe for the native PC build (see tools/pc_port/abi_layout.py).
//
// Every type listed here crosses a boundary where the GBA byte layout must be
// reproduced exactly by the native x86 build:
//   - save data written to flash (and imported from existing GBA saves),
//   - mailboxes exchanged with the multiplayer companion,
//   - battle controller packets exchanged between linked players.
// The tool compiles this file for both targets and compares DWARF layouts.
#include "global.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_message.h"
#include "multiplayer.h"
#include "multiplayer_appearance.h"
#include "multiplayer_battle.h"
#include "multiplayer_battle_schema.h"
#include "multiplayer_coop.h"
#include "multiplayer_local_reset.h"
#include "multiplayer_menu.h"
#include "multiplayer_ownership.h"
#include "multiplayer_reset_admission.h"
#include "multiplayer_ride.h"
#include "multiplayer_save.h"
#include "multiplayer_static_reset.h"
#include "multiplayer_trade_effects.h"
#include "multiplayer_world.h"
#include "fieldmap.h"
#include "gba/m4a_internal.h"

#define PROBE(type) struct type gAbiProbe_##type;

// Save data
PROBE(SaveBlock1)
PROBE(SaveBlock2)
PROBE(SaveBlock3)
PROBE(PokemonStorage)
PROBE(Pokemon)
PROBE(BoxPokemon)

// Data emitted by assembler macros (data/maps, sound) and read through C structs
PROBE(MapHeader)
PROBE(MapLayout)
PROBE(MapEvents)
PROBE(ObjectEventTemplate)
PROBE(WarpEvent)
PROBE(CoordEvent)
PROBE(BgEvent)
PROBE(MapConnections)
PROBE(MapConnection)
PROBE(Tileset)
PROBE(Song)
PROBE(SongHeader)
PROBE(ToneData)
PROBE(WaveData)

// Battle link packets
PROBE(BattlePokemon)
PROBE(BattleMsgData)
PROBE(ChooseMoveStruct)
PROBE(LinkBattleAnim)
PROBE(MovePPInfo)
PROBE(HpAndStatus)

// Companion mailboxes
PROBE(MultiplayerMailbox)
PROBE(MultiplayerSaveControl)
PROBE(MultiplayerSaveMailbox)
PROBE(MultiplayerBattleMailbox)
PROBE(MultiplayerBattleControl)
PROBE(MultiplayerBattleBinding)
PROBE(MultiplayerBattleDiagnostic)
PROBE(MultiplayerBattlePreparation)
PROBE(MultiplayerMenuMailbox)
PROBE(MultiplayerResetMailbox)
PROBE(MultiplayerWorldMailbox)
PROBE(MultiplayerTradeRecordPreparation)
PROBE(MultiplayerTradeEffectPreview)
PROBE(MultiplayerTradeChoiceView)
PROBE(MultiplayerTradeMoveInfo)
PROBE(MultiplayerTradeMoveChoice)
