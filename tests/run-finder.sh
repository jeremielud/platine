#!/bin/sh
# Lance platine-wm + platine-finder dans Xvfb 800x480, capture le bureau,
# ouvre Documents depuis l'icône du bureau, puis crée un dossier par le
# menu Fichier. Les positions d'icônes sont amorcées dans l'état (comme
# run-finder-trash.sh) : le test ne dépend pas de la grille par défaut.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

# Le scénario crée un dossier : ne jamais toucher au HOME de l'utilisateur.
TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-finder.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/finder}
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$HOME_TEST/Documents" "$HOME_TEST/Images" "$OUT/log" "$STATE"
# Ancienne colonne enregistrée : le Finder doit la migrer de x=687 à x=735.
printf 'I\t__bureau__\tDocuments\t687\t88\n' >"$STATE/icones"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
cleanup() {
    [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
D=:$(cat "$TMP/display")
export DISPLAY="$D"
export HOME="$HOME_TEST"

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-finder >"$OUT/log/finder.log" 2>&1 &
APP=$!
# par défaut, le bureau seul : aucune fenêtre (finder_fenetre_demarrage 0)
i=0; until [ -s "$STATE/icones" ] || [ $i -ge 50 ]; do sleep 0.1; i=$((i+1)); done
sleep 1
if xdotool search --onlyvisible --name '^Eee PC 701$' >/dev/null 2>&1; then
    echo "ÉCHEC : une fenêtre du Finder s'ouvre au démarrage" >&2
    exit 1
fi

import -window root "$OUT/bureau.png"
awk -F '\t' '$1 == "I" && $2 == "__bureau__" && $3 == "Documents" &&
    $4 == 735 && $5 == 88 { ok=1 } END { exit !ok }' "$STATE/icones" || {
    echo "ÉCHEC : la position de Documents n'a pas été migrée vers 735,88" >&2
    exit 1
}

# Double-clic sur le glyphe de l'icône Documents du bureau (32×32 à 735,88
# dans la fenêtre bureau, qui commence sous la barre de menus : +20 en y).
xdotool mousemove 751 125 click --repeat 2 --delay 200 1
sleep 0.8
xdotool search --sync --onlyvisible --name '^Documents$' >/dev/null
import -window root "$OUT/documents.png"

# Menu Fichier du Finder, puis Nouveau dossier au clavier.
xdotool mousemove 45 10 click 1
sleep 0.4
import -window root "$OUT/menu.png"
xdotool key Down Return
sleep 0.8
import -window root "$OUT/nouveau-dossier.png"
[ -d "$HOME/Documents/Nouveau dossier" ] || {
    echo "ÉCHEC : Nouveau dossier n'a pas été créé" >&2
    exit 1
}

echo "Captures dans $OUT"
echo "--- wm.log"; cat "$OUT/log/wm.log"
echo "--- finder.log"; cat "$OUT/log/finder.log"
