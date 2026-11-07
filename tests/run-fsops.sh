#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
OUT=${OUT:-/tmp/platine-tests}
mkdir -p "$OUT"
${CC:-cc} -O2 -g -Wall -Wextra -std=gnu99 -D_POSIX_C_SOURCE=200809L \
    -o "$OUT/test-fsops" tests/test-fsops.c src/apps/finder/fsops.c
"$OUT/test-fsops"
