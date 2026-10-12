#!/bin/sh
# Builds tools/battlefx/bfx_check (the game's Battle FX host + QuickJS-ng, no SDL, no game code) for the game's
# targets into build/battlefx-check/: Linux x86-64, Linux i386 (gcc -m32) and Windows i686 (i686-w64-mingw32-gcc).
# QuickJS is compiled with the flags the game build gives it (Makefile_pc BFX_QJS_CFLAGS): SSE2 floating point, so
# the 32-bit builds compute doubles as the 64-bit ones do (x87's extended precision would not).
#   sh tools/battlefx/build_check.sh            # all three
#   sh tools/battlefx/build_check.sh i386       # one
set -e
OUT=build/battlefx-check
mkdir -p "$OUT"
QJS="third_party/quickjs-ng/cutils.c third_party/quickjs-ng/libregexp.c third_party/quickjs-ng/libunicode.c third_party/quickjs-ng/quickjs.c third_party/quickjs-ng/xsum.c"
SRC="tools/battlefx/bfx_check.c src/diorama/battlefx/bfx_host.c"
INC="-iquote include -I third_party/quickjs-ng"
DEF="-DBFX_HOST_STANDALONE -D_GNU_SOURCE"
build() {
  name=$1; shift
  cc=$1; shift
  echo "bfx_check: $name"
  rm -rf "$OUT/$name"; mkdir -p "$OUT/$name"
  for f in $QJS; do $cc "$@" -O2 -w $DEF -I third_party/quickjs-ng -c "$f" -o "$OUT/$name/$(basename "$f" .c).o"; done
  for f in $SRC; do $cc "$@" -O2 -std=gnu99 -Wall -Wextra -Wno-unused-parameter $DEF $INC -c "$f" -o "$OUT/$name/$(basename "$f" .c).o"; done
}
want() { [ -z "$1" ] || [ "$1" = "$2" ]; }
if want "$1" x86_64; then build x86_64 gcc; gcc "$OUT"/x86_64/*.o -lm -lpthread -o "$OUT/bfx_check"; fi
if want "$1" i386; then build i386 gcc -m32 -msse2 -mfpmath=sse; gcc -m32 "$OUT"/i386/*.o -lm -lpthread -o "$OUT/bfx_check32"; fi
if want "$1" win32; then build win32 i686-w64-mingw32-gcc -msse2 -mfpmath=sse -mno-ms-bitfields -mno-align-double -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0601
  i686-w64-mingw32-gcc "$OUT"/win32/*.o -static -static-libgcc -o "$OUT/bfx_check.exe"; fi
