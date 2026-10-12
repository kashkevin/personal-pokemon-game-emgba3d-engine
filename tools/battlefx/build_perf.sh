#!/bin/sh
# Builds tools/battlefx/bfx_perf (the game's Battle FX host + QuickJS-ng, no SDL, no game code) for Linux i386
# with the game build's flags for QuickJS (Makefile_pc BFX_QJS_CFLAGS: -O2, SSE2 doubles) into
# build/battlefx-perf/bfx_perf32.
#   sh tools/battlefx/build_perf.sh && build/battlefx-perf/bfx_perf32 battlefx MOVE_BOLT_STRIKE,MOVE_SURF 3
set -e
OUT=build/battlefx-perf
rm -rf "$OUT"; mkdir -p "$OUT/o"
QJS="third_party/quickjs-ng/cutils.c third_party/quickjs-ng/libregexp.c third_party/quickjs-ng/libunicode.c third_party/quickjs-ng/quickjs.c third_party/quickjs-ng/xsum.c"
SRC="tools/battlefx/bfx_perf.c src/diorama/battlefx/bfx_host.c"
FLAGS="-m32 -msse2 -mfpmath=sse -O2 -DBFX_HOST_STANDALONE -D_GNU_SOURCE"
for f in $QJS; do gcc $FLAGS -w -I third_party/quickjs-ng -c "$f" -o "$OUT/o/$(basename "$f" .c).o"; done
for f in $SRC; do gcc $FLAGS -std=gnu99 -Wall -Wextra -Wno-unused-parameter -iquote include -I third_party/quickjs-ng -c "$f" -o "$OUT/o/$(basename "$f" .c).o"; done
gcc -m32 "$OUT"/o/*.o -lm -lpthread -o "$OUT/bfx_perf32"
echo "built $OUT/bfx_perf32"
