# Benchmark suite (native PC, 3D view)

Reproducible frame-rate and frame-time measurements of the 3D view, made by the
game itself (`--benchmark`, `src/platform/benchmark.c`) in fixed scenes. Added by
the performance work of 2026-10-11. Project-owned.

## What one run measures

The game Continues a scene's save by itself (Start, then A until the 3D field is
drawn), lets the map name fade, then draws the scene at each resolution in turn
(`--bench-heights`, 16:9; 1080p, 1440p and 2160p in the suite) with VSync off, no
frame-rate limit and render scale 100 % (native: every pixel of the resolution is
drawn; when the window is smaller the scene is drawn offscreen at that size and
scaled into it). After a 3 s warm-up it counts N seconds per resolution and writes:

* `SCENE.log`: the GPU (renderer, vendor, OpenGL and driver version strings), CPU,
  RAM, display, window and graphics settings; per resolution the frame count,
  average fps, 1 % and 0.1 % lows, median / 90th / 95th / 99th / 99.9th percentile
  and worst frame times, stutters (frames over twice the median and at least 8 ms
  over it), CPU time per frame (all of it, the sync of a new game frame), GPU time
  per frame (timer queries, where the driver has them), draw calls, triangles,
  uploads, process RAM and the GPU memory the game allocated; the last resolution's
  per-pass CPU and GPU breakdown and memory lines.
* `results.csv`: the same, one row per scene and resolution (appended), for
  comparisons across scenes, settings and builds.

The game never saves during a benchmark (the in-game Save is never chosen), and
each scene runs from a copy of its save.

## Scenes

| name | save | why |
| --- | --- | --- |
| route101 | route101 | the first route: grass, trees, ledges |
| petalburg-woods | woods | dense trees, shade |
| rustboro | rustboro | a large city, many buildings |
| slateport-market | slateport-market | the market: stalls, people (most vertices) |
| mauville | mauville | city with the most detail objects |
| granite-cave | granite | a cave |
| dewford-coast | dewford | coast: sea, beach, small town |
| dense-objects | route110 | the most detail objects (flowers, fences, grass) |
| jagged-pass | jagged | the Lavaridge tileset's rocky relief: the heaviest terrain (KNOWN_ISSUES V37) |
| night-town | mauville + `PKMN_CLOCK_HOUR=22` | night: street lamps, lit windows |
| heavy-rain | route104 + `PKMN_DIORAMA_WEATHER=13` | downpour over the coast route |
| *-details-off | route101, rustboro, route110 | World details off (F10), the rest high |

`PKMN_CLOCK_HOUR=H` (development and benchmark only) makes the game's own clock read
H:00 at start (the save's clock setting accounted for). `PKMN_DIORAMA_WEATHER=N`
previews weather N in the 3D view (13: downpour).

The scene saves are private game data: not in Git. They are packed with the private
checkpoint's benchmark folder.

## Running it

Windows (the owner's PC): `run-benchmark.bat` in the checkpoint's `benchmark`
folder; `run-benchmark.bat quick` for four scenes at 1080p and 2160p. It writes
`results\run-N\`. When the previous checkpoint's build is beside it, it also runs that
build's own benchmark (720p to 2160p) in four scenes (Jagged Pass among them), for
before/after.

Linux (development): `tools/benchmark/run_benchmark.sh GAME SCENES_DIR OUT_DIR
[quick|full] [SCENE...]` with `DISPLAY` set (`GAME_RUNNER=wine` for the .exe).

Single run: `pokeemerald-mp-diorama --save S.sav --config C.cfg --benchmark 10
--bench-name NAME --bench-heights 1080,1440,2160 --bench-log NAME.log --bench-csv
results.csv`.

## Reading results

Numbers measured in a software renderer (llvmpipe, the development container) say
nothing about a graphics card's frame rate; only runs on real hardware do, and the
log names the renderer. Compare like with like: the same scene, resolution and
settings, the same machine.
