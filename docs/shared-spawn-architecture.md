# Shared wild spawns: architecture (Phases 3–5)

Status: DESIGN, approved by the owner for implementation (2026-10-13). Nothing in this
document is implemented or tested yet. The owner's requirements are numbered R1–R8 below.

## 1. What exists today

- The host owns a persistent shared world (app: `shared_world.py`, class `SharedWorld`,
  a SQLite store with a 12x world clock). It already runs single-winner events: a
  spawn is created by `spawn_event`, a player takes it with `claim_event` (first claim
  wins, others get `busy`), `event_result` ends it (`caught` consumes it, otherwise the
  claim is released), timers expire it, and `player_disconnected` starts a grace period
  before the claim is released.
- The game talks to the host through the EMW1 mailbox (`src/multiplayer_world.c`,
  `include/multiplayer_world.h`): fixed 256-byte records, `MAX_PEERS` 3, action codes
  `MP_WORLD_ACTION_CLAIM/RESULT/RELEASE/...`, and an inbound record that carries the
  shared clock and the event flags.
- Wild encounters are rolled locally. `MPWild_RandomizeSpecies` (`src/mp_wild_random.c`)
  picks a species from the 312-family pool with a Random32 on each client. Two clients on
  the same route therefore see different Pokémon. This is what R1 and R2 forbid.
- Shiny odds for wild Pokémon come from the local RNG at creation (`MP_GEN6_SHINY_ODDS`).

## 2. Model: host-authoritative spawn slots

A **spawn slot** is one visible wild Pokémon in one area, owned by the host.

Fields (host record, stored in the shared world database):

| Field | Type | Meaning |
| --- | --- | --- |
| `spawn_id` | u32 | Unique for the world, never reused. |
| `map_group`, `map_number` | u8, u8 | The map the spawn belongs to. |
| `x`, `y` | u8, u8 | Tile position (for visible spawns; ignored for traditional encounters). |
| `species` | u16 | Chosen by the host when the slot is created. |
| `level` | u8 | Chosen by the host. |
| `shiny` | bool | Chosen by the host, fixed for the spawn's whole life (R3). |
| `kind` | enum | `TRADITIONAL`, `VISIBLE`, or `OUTBREAK` (Phase 4). |
| `state` | enum | `OPEN`, `ENGAGED`, `CAUGHT`, `DEFEATED`, `FLED`, `EXPIRED`. |
| `claimant` | player id or none | Set while `ENGAGED`. |
| `expires_at` | world time | Despawn time; paused while nobody is online (Phase 11). |

The host chooses species, level and shiny at creation. Clients never roll them for a
shared spawn (R2). Using explicit values rather than a shared seed avoids having to keep
the Python and C random generators bit-identical.

## 3. Ownership rules

1. Only the host creates, claims, releases and expires spawns.
2. A client may **engage** a spawn only if it is `OPEN`, or `ENGAGED` by itself (a reconnect
   resumes its own claim). Everyone else gets `busy` or `gone`.
3. The first valid engage wins (R7). The host serialises requests in arrival order; there
   is no second winner.
4. A client's `RESULT` (caught, defeated, fled) is accepted only from the claimant and only
   for the spawn it claimed. Anything else is ignored with a reason code.
5. A `CAUGHT` spawn is consumed for everybody. A `DEFEATED` or `FLED` spawn is released to
   `OPEN` again (the current event behaviour), unless the owner's rules say otherwise.
6. Disconnect: the claim is held for `event_claim_grace_seconds`, then released (existing
   behaviour, reused).
7. Expiry and despawn set `EXPIRED` for all players. Expiry only runs while someone is
   online (Phase 11).

## 4. Message contracts (EMW1 extension)

The mailbox stays 256 bytes per record. Shared spawn traffic uses new action codes; the
ABI version is bumped so older clients refuse the new records instead of misreading them.

- Host → client (inbound record): the **spawn list for the player's current map**, up to
  three slots (`MP_WORLD_MAX_SPAWNS_PER_MAP` = 3), each 12 bytes: `spawn_id`, `x`, `y`,
  `species`, `level`, `flags` (shiny, kind, state), `seconds_left`. That is 36 bytes plus a
  header, well inside 256 bytes.
- Client → host (`MP_WORLD_ACTION_SPAWN_ENGAGE`): `spawn_id`, `map_group`, `map_number`.
  Reply in the next inbound record: `granted`, `busy`, `gone`.
- Client → host (`MP_WORLD_ACTION_SPAWN_RESULT`): `spawn_id`, `result` (caught, defeated,
  fled). Reply: accepted or refused with reason.
- Client → host (`MP_WORLD_ACTION_SPAWN_RELEASE`): `spawn_id` when the player walks away
  from an engaged spawn or leaves the map without a result.

Every request carries a per-client sequence number. The host replies with the same
sequence number, so a repeated request after a reconnect returns the same answer and is
never applied twice (R7).

## 5. Traditional (grass/water) and visible encounters

- Traditional encounters are rolled when the player steps onto grass. With shared spawns on,
  the client asks the host for the slot of that encounter area. The host answers with an
  existing `OPEN` slot or creates one from the weighted pool (Phase 2 weights apply).
  The client then engages it.
- Visible (overworld) Pokémon (Phase 3) use the same slots. The sprite shows when the
  inbound record lists the slot for the current map. Shininess comes from the slot, so
  every player who can see the sprite sees the same sparkle (R3).

## 6. Mass outbreaks (Phase 4)

Outbreaks are slots of kind `OUTBREAK` with a larger count, created by the host from the
outbreak catalogue, announced through the existing notice path, and expired with the
normal timer. They reuse the same claim and result rules; the shiny bonus is a host roll
at creation, so it can't be rerolled by a client (R5).

## 7. Following Pokémon (Phase 5)

A follower is local presentation. It is not a spawn and never changes ownership. Each
player's follower is sent in the appearance record that already exists
(`MP_WORLD_ACTION_APPEARANCE`), and remote players draw it from that record only. A follower
is never created from a spawn; catching a Pokémon adds it to the owner's party once,
through the normal result path (R6).

## 8. Reconnects, joins and leaves (R7)

- Joining a map: the client requests the slot list; the host returns current state only.
- Leaving a map: a claim that was not resolved is released (`SPAWN_RELEASE`).
- Reconnect: the host keeps claims for the grace period; the reconnecting client resumes
  its claim only with the same player id and sequence number.
- Simultaneous engage: the host's arrival order decides; the loser gets `busy` and must
  not start a battle.

## 9. Battles, saves and compatibility (R8)

- Battle mechanics are unchanged: a granted engage starts the normal wild battle with the
  host's species, level and shiny flag. Only the source of those values changes.
- Saves: the local save stores nothing about shared slots except the caught Pokémon and
  the caught/defeated result, so older saves still load.
- GBA ROM: all new spawn code is compiled only for the PC port (`PLATFORM_SDL2`), so the
  pinned GBA ROM (f214735b…) stays byte-identical.
- Wire format: ABI version bump. Older clients get a clear "update required" refusal.

## 10. What is decided and what is open

Decided: host authority, explicit species/level/shiny in the host record, first-claim-wins,
the message set above, PC-only code.

Open (need the owner's call before Phase 3 code): the maximum number of visible slots per
map; whether a `DEFEATED` shared spawn returns to `OPEN` for others (current event rule)
or ends for everybody; and the outbreak size.

## 11. Testing plan (not yet run)

- App: unit tests for claim, release, grace period, expiry pause and sequence replay on
  `SharedWorld` (pure Python, runs here).
- Engine: native unit tests for the record encode/decode and the ABI check; in-game
  two-player runs only on the owner's PC (multi-PC and GPU results stay unverified until then).
