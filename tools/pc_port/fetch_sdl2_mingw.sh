#!/bin/sh
# Downloads the official SDL2 2.30.7 MinGW development release (zlib license)
# used for the Windows cross build, and verifies its checksum.
#   tools/pc_port/fetch_sdl2_mingw.sh DEST_DIR
# Then: make -f Makefile_pc SDL_DIR=DEST_DIR/SDL2-2.30.7/i686-w64-mingw32
set -eu
dest="${1:-build/deps}"
mkdir -p "$dest"
url=https://github.com/libsdl-org/SDL/releases/download/release-2.30.7/SDL2-devel-2.30.7-mingw.tar.gz
curl -fsSL -o "$dest/SDL2-devel-2.30.7-mingw.tar.gz" "$url"
echo "147c3dc03c9be6b23e625fd877421098e93487f1f55e0e1d4e3ce571e79b91c4  $dest/SDL2-devel-2.30.7-mingw.tar.gz" | sha256sum -c -
tar xzf "$dest/SDL2-devel-2.30.7-mingw.tar.gz" -C "$dest"
echo "SDL_DIR=$dest/SDL2-2.30.7/i686-w64-mingw32"
