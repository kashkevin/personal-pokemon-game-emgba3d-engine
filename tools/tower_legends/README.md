# Battle Tower: Legends - floor generator

`generate.py` makes the 100 floors of Battle Tower: Legends (the native PC game only; the design and the contract with
the app are `docs/battle-tower-legends.md` in the app repository, sections 2, 6, 7 and 8). From the game's own data
(dumped by the compiled game), the hand-written bosses (`bosses.py`) and the curated trainer pools (`pools.py`) it builds
300 trainers with one fixed seed, checks every rule below and writes:

| Output | What |
| --- | --- |
| `src/data/tower_legends.party` | the 300 trainers in the game's Showdown-style trainer format; `trainer_rules.mk` converts it with `tools/trainerproc` into `src/data/tower_legends.h` (a build product, included by `src/tower_legends.c`) |
| `include/constants/tower_legends.h` | `TOWER_FLOOR_COUNT`, the 300 trainer indexes `TOWER_TRAINER_F001` ... `TOWER_TRAINER_F100_DUO_B` (0..299), `TOWER_LEGENDS_TRAINER_COUNT`, `TOWER_LEGENDS_TABLE_HASH`, the floor kinds and regions |
| `src/data/tower_legends_floors.h` | the floor texts and `gTowerLegendsFloors[TOWER_FLOOR_COUNT]` (`struct TowerLegendsFloor`, declared in `include/tower_legends.h`) |
| `tools/tower_legends/tower_legends.json` | the same floors for the app and the tests: `{"version": 1, "hash", "floors"}` |
| `tools/tower_legends/tower_floors_app.py` | the app's `emerald_multiplayer.tower_floors` (copied over): `TABLE_HASH`, `FLOORS` (the JSON's floors), `MILESTONE_SPECIES`, `LEGENDARY_SPECIES` |
| `tools/tower_legends/FLOORS.md` | every floor in readable form, the boss roster with picture sources, every substitution and deviation |

All six are generated: never edit them by hand. Change the sources and regenerate.

| Source | What |
| --- | --- |
| `bosses.py` | the twelve boss floors: teams, Duo partners, texts, music, picture sources, the documented deviations |
| `pools.py` | the regular trainer classes: pictures, encounter music, floor range, species pool, Duo partners, defeat lines; the name banks; species never used on regular floors |
| `rules.py` | floor kinds, team sizes, regions, difficulty, AI tiers, IV/EV bands, held-item groups, Mega floors |
| `movedex.py` | the move judgement: banned moves, the status moves the AI can use (with roles), move value estimates |
| `builder.py` | species legality and choice, move sets, Abilities, items, natures, IVs and EVs, Megas |
| `assemble.py` | the seed (`SEED = 20261021`), the class schedule, names, Duo A/B, texts |
| `validate.py` | every rule check; `writers.py` the output formats; `gamedata.py`, `naming.py`, `typechart.py` read the dump and the engine's headers |

## Regenerating

1. Build the native Linux game (with the committed outputs, which it compiles): `make -f Makefile_pc NATIVE_LINUX=1`.
2. Dump its data: `./pokeemerald-mp --dump-tower-data /tmp/tower_data.json` (every enabled species, move, item, Ability,
   trainer class and trainer picture, as numbers; `src/platform/native_selftest_tower.c`).
3. Generate: `python3 tools/tower_legends/generate.py --data /tmp/tower_data.json`. It prints the validation summary
   (each rule and how often it was checked) and writes the outputs (an unchanged output keeps its timestamp, so make
   rebuilds nothing for it); when any rule fails it lists the violations, writes nothing and exits 1.
4. Check without writing: `--check` regenerates in memory and exits 1 when a written output differs or a rule fails.
   `--verify-trainerproc` also runs the C preprocessor and `tools/trainerproc/trainerproc` on the generated `.party` in a
   temporary directory (the pipeline of `trainer_rules.mk`) and compares every field it writes with the intended trainer.
   `--quiet` prints only the totals and any failure.
5. Rebuild the game (the `.party` changed, so `src/data/tower_legends.h` is remade) and copy `tower_floors_app.py` to the
   app as `emerald_multiplayer/tower_floors.py`. The game refuses a Tower command whose table hash is not its own
   `TOWER_LEGENDS_TABLE_HASH`, so the app and the game must come from the same generation.

The generator is deterministic: Python 3's standard library only (run with 3.12 and 3.13), one
`random.Random(20261021)`, no dependence on hash or file order. The same dump gives byte-identical outputs; a dump with
different game data (learnsets, items, species) can change the floors and therefore the hash. Exit status: 0 when every
rule holds (and, with `--check`, nothing differs), 1 otherwise, 2 when the dump is missing or does not match the
engine's headers.

## The rules

`validate.py` checks each of these on every floor, trainer and Pokemon; FLOORS.md lists the judgement calls and
deviations.

* **Floors.** 100 floors. Bosses 10, 20, 30, 40, 60, 70, 80, 90; specials 25, 50, 75; the final 100; those twelve are the
  checkpoints. Floors 10, 20, 25, 90 and 100 (the FRLG bosses) are Kanto, every other floor Hoenn. Difficulty (1-5):
  1 + (floor - 1) / 20, +1 on a boss floor, +2 on a special floor and the final, at most 5.
* **Trainers.** Per floor the solo trainer, Duo A (the solo trainer with min(3, its team size) of its team: its ace,
  written last, and its strongest) and Duo B (a different trainer, a fitting partner, min(3, solo size) Pokemon). Ids
  `TOWER_TRAINER_F001`, `_DUO_A`, `_DUO_B`, ... in that order. Singles battle type, no bag items.
* **Team sizes.** Floors 1-4: 1; 5-9: 2; 11-24: 3; 26-44: 4; 45-49 and 51-69: 5; 71-99: 6. Bosses: 10: 3, 20: 4, 25 to
  40: 5, 50 to 100: 6.
* **Levels.** Every Pokemon is exactly the floor's level and can legally exist there, with every earlier stage's
  requirement chained: a level evolution at or above its level, an item evolution from max(20, the previous stage's
  minimum), friendship from 18, trade from 30 (an item evolution that stands in for a trade, such as the Linking Cord,
  counts as a trade), any other method from 25.
* **Species.** Enabled, national dex 1-721, the base form (Mega forms only through a held stone; no species that changes
  form in battle otherwise), front, back, shiny and icon pictures all real. Regular floors and Duo partners use no
  legendary, mythical, Ultra Beast or paradox species; only Brandon (75) uses legendaries, his Regirock, Regice and
  Registeel.
* **Moves.** One to four distinct moves, four unless fewer are legal; at least one damaging move; from floor 30 a STAB
  move when the species can learn one. Floors 1-29: the species' own level-up moves at or below the level; from 30 also
  its TM and tutor moves; from 50 also its evolution line's egg moves. Never: OHKO moves, explosion moves, moves that
  faint the user, status moves the AI has no use for, accuracy drops from floor 20, the judgement exclusions of
  `movedex.py`, fixed damage below floor 20, damaging moves above 60 power on floors 1-9.
* **Abilities.** One of the species' own Abilities; the hidden one only from floor 50.
* **Held items.** None on floors 1-4; simple Berries from 5; Sitrus Berry and the type-boosting items from 20; Leftovers,
  Quick Claw, Scope Lens, Shell Bell, Focus Band, King's Rock, Lum Berry and Mega Stones from 30; the competitive items
  from 50 (`rules.ITEM_GROUPS`). Every item exists with a hold effect; no item twice in one team from floor 30 (the
  generator already avoids it from 20); Choice items and the Assault Vest only with attacking moves; Black Sludge only
  on Poison types; Eviolite only on a Pokemon that can still evolve; Light Ball only on Pikachu.
* **IVs, EVs, natures.** Floors 1-19: IVs 3 + floor/2, no EVs, a seeded random nature. 20-39: IVs 15-24 and light EVs
  (bosses up to 252 in their best stat). 40-69: IVs 24-31, full spreads (500-510 EVs, 252/252/4 by role) and a nature
  that raises the attacking stat the set uses, Speed or a stat with 252 EVs. Specials, the final and floors 70-100: IVs
  31 and the same full spreads. From floor 20 no nature lowers the attacking stat the set uses (neither of them for a
  set that attacks with both). EVs at most 252 each and 510 in all.
* **AI flags.** Floors 1-9 Check Bad Move; 10-29 Basic Trainer; 30-49 Basic Trainer + Smart Switching; 50-69 Smart
  Trainer; 70-100 Smart Trainer + Prediction. Bosses one tier above their band (from 70 the strongest set: + HP Aware);
  specials and the final Smart Trainer + Prediction + HP Aware + Ace Pokemon. The game adds the double-battle flag
  itself in a Duo (`battle_ai_main.c`).
* **Mega Evolution.** None on floors 1-29. 30-49: Flannery's Houndoom, Norman's Kangaskhan and at most 3 regular
  trainers. 50-69: the bosses and about a third of the regular trainers (5-7 of 18). 70-89: every trainer's ace holds
  its stone when its species has a Mega. 90-100: every trainer has one Mega. At most one stone per trainer, only on the
  species whose form-change table names it, the Mega form enabled with real pictures (the builder also picks the
  holder's moves, EVs and nature for the Mega form).
* **Bosses.** The design's table: each boss's class, picture, type theme, core species, ace and Mega, Duo partner and
  battle music; no class that prints the rival's name; every boss text present.
* **Regular trainers.** The classes and pictures of `pools.py` (each picture drawn, no class that prints a placeholder
  or the rival's name), the class's encounter music, a unique name of at most 10 capital letters, a gender that fits the
  picture. A regular floor's class (as the game names it) differs from the regular floor before (the generator also
  avoids the three before), and a floor's ace differs from the floor before's (the generator also avoids the four
  before where the pool allows).
* **Difficulty.** Per band of ten floors, the regular trainers' mean team strength rises strictly, and IVs, EVs, team
  size and AI tier never fall.
* **Texts.** The game's escapes `\n` (a new line) and `\p` (a new page), at most 30 characters a line and two lines a
  page, letters, digits, spaces and `.,!?'-:` only. Regular intros name the floor and the trainer; only bosses have a
  victory text (shown when the player loses).
* **Outputs agree.** The app module's floors and hash equal the JSON's; the header's 300 indexes run 0..299 in the
  `.party`'s order; with `--verify-trainerproc`, trainerproc's output matches every trainer and Pokemon.

## The table hash

`hash = zlib.crc32(json.dumps(floors, sort_keys=True, separators=(",", ":")).encode())`, written `0x%08x`, over the
JSON's `floors` list (names and the numeric ids alike). The same value is `TOWER_LEGENDS_TABLE_HASH` in the game and
`TABLE_HASH` in the app module.

## The JSON

Each floor: `floor`, `level`, `kind` (`regular`, `boss`, `special`, `final`), `checkpoint`, `region` (`kanto`,
`hoenn`), `difficulty`, `music` (the battle music), `intro`, `defeat`, `defeatB`, `victory` (null on regular floors),
`solo` and `duo` ([Duo A, Duo B]). Each trainer: `id`, `name`, `class`, `classId`, `pic`, `picId`, `gender`,
`encounterMusic`, `ai`, `mons`. Each Pokemon: `species`, `speciesId`, `level`, `item`, `itemId` (0: none), `ability`,
`abilityId`, `nature`, `natureId`, `ivs` and `evs` (HP, Atk, Def, SpA, SpD, Spe), `moves`, `moveIds` (four, 0 for an
empty slot), `mega` (the Mega form's species, or null). Texts keep the escapes as two characters (`\` and `n`).
