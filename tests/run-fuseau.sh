#!/bin/sh
# Fuseau horaire à chaud : test unitaire de ptk_fuseau_suivre (libptk).
set -eu
cd "$(dirname "$0")/.."
make build/ptk.o >/dev/null 2>&1
OUT=${OUT:-/tmp/platine-tests}
mkdir -p "$OUT"
${CC:-cc} -O2 -g -Wall -Wextra -std=gnu99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -Itoolbox $(pkg-config --cflags x11 xft xpm) \
    -o "$OUT/test-fuseau" tests/test-fuseau.c build/ptk.o \
    $(pkg-config --libs x11 xft xpm fontconfig) -lXext -lXrender
"$OUT/test-fuseau"
