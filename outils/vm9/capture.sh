#!/bin/sh
# Capture l'écran de la VM Mac OS 9 (QEMU, socket vm.sock) en PNG.
# Usage : ./capture.sh nom [dossier-de-sortie]
set -eu
VM=${VM:-$HOME/Projets/eeepc-openbsd/macos9}
OUT=${2:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)/audit/macos9-ref}
NAME=${1:?nom de capture requis}
mkdir -p "$OUT"
printf 'screendump %s/%s.ppm\n' "$OUT" "$NAME" | \
    socat - UNIX-CONNECT:"$VM/vm.sock" >/dev/null
sleep 0.3
convert "$OUT/$NAME.ppm" "$OUT/$NAME.png"
rm -f "$OUT/$NAME.ppm"
echo "$OUT/$NAME.png"
