#!/bin/sh
# Rend la fenêtre de démarrage avec le vrai platine-demarrage (Xvfb 800×480) et la découpe :
# assets/demarrage-fenetre-vide.ppm (logo seul) et assets/demarrage-fenetre.ppm (« Démarrage… »
# et barre vide). platine-ecran-netbsd les affiche avant X : même image au pixel près.
set -eu
cd "$(dirname "$0")/.."
make build/platine-demarrage >/dev/null
T=$(mktemp -d)
trap 'kill $X 2>/dev/null; rm -rf "$T"' EXIT
Xvfb :59 -screen 0 800x480x24 -nolisten tcp -noreset >/dev/null 2>&1 & X=$!
sleep 1
mkfifo "$T/f"
exec 3<>"$T/f"
DISPLAY=:59 PLATINE_ASSETS=$PWD/assets HOME=$T ./build/platine-demarrage "$T/f" & D=$!
sleep 1.4; DISPLAY=:59 import -window root "$T/vide.png"
sleep 1.1; DISPLAY=:59 import -window root "$T/barre.png"
kill $D
# fenêtre 422×323 en (189, 58) pour 800×480 (voir FW, FH et fy dans demarrage.c)
convert "$T/vide.png" -crop 422x323+189+58 +repage assets/demarrage-fenetre-vide.ppm
convert "$T/barre.png" -crop 422x323+189+58 +repage assets/demarrage-fenetre.ppm
echo "fenêtres de démarrage générées"
