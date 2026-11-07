#!/bin/sh
# Menu Pomme alimenté par « System/Apple Menu Items », comme
# Mac OS 9 : éléments triés, programmes (vrais binaires ou alias) lancés, un
# dossier (alias de Tableaux de bord) ouvert dans le Finder, et ce qu'on pose
# dans le dossier apparaît à la réouverture du menu.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder build/platine-calculette build/platine-textedit \
    build/platine-theiere build/platine-tableau

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-pomme.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/pomme}
V="$TMP/vol"
S="$V/System"
P="$S/Apple Menu Items"
mkdir -p "$OUT/log" "$V/Applications" "$S/Control Panels" "$P" "$V/Documents" "$V/Images"
cp build/platine-calculette "$P/Calculette"
cp build/platine-textedit "$V/Applications/TextEdit"
cp build/platine-theiere "$V/Applications/Théière"
cp build/platine-tableau "$S/Control Panels/Apparence"
ln -s "../../Applications/TextEdit" "$P/TextEdit"
ln -s "../Control Panels" "$P/Tableaux de bord"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp 3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
FI=
cleanup() {
    pkill -P $$ 2>/dev/null || true
    [ -n "$FI" ] && kill "$FI" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    for p in $(pgrep -f "$TMP/vol" 2>/dev/null); do kill "$p" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$V" PLATINE_ASSETS="$PWD/assets"
unset PLATINE_VOLUME

fail() { echo "ÉCHEC: $*" >&2; exit 1; }
fenetre() { # attend une fenêtre dont le nom commence par $1
    i=0
    until xdotool search --name '' getwindowname %@ 2>/dev/null | grep -q "^$1"; do
        i=$((i + 1)); [ $i -lt 60 ] || return 1; sleep 0.1
    done
}
pomme() { xdotool mousemove 22 10 click 1; sleep 0.4; }
# lignes du menu : À propos 16 px, séparateur 6, puis 18 px par élément dès y = 42
ligne() { echo $((42 + 18 * $1 + 9)); }

./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
./build/platine-finder >"$OUT/log/finder.log" 2>&1 & FI=$!
sleep 1.5

# 1) ordre alphabétique : Calculette, Tableaux de bord, TextEdit
pomme
import -window root "$OUT/menu.png"
xdotool mousemove 70 "$(ligne 2)" click 1
fenetre 'Sans titre' || fail "l'alias TextEdit du menu Pomme n'a pas lancé TextEdit"
pomme
xdotool mousemove 70 "$(ligne 0)" click 1
fenetre 'Calculette' || fail "la Calculette du menu Pomme ne s'ouvre pas"

# 2) un dossier du menu (alias de Tableaux de bord) s'ouvre dans le Finder ;
#    son sous-menu montre son contenu
pomme
xdotool mousemove 70 "$(ligne 1)"
sleep 0.5
import -window root "$OUT/sous-menu.png"
xdotool click 1
fenetre 'Tableaux de bord' || fail "le dossier Tableaux de bord du menu Pomme ne s'ouvre pas"

# 3) poser un alias dans le dossier : il apparaît à la réouverture
ln -s "../../Applications/Théière" "$P/Théière"
pomme
import -window root "$OUT/menu-theiere.png"
xdotool mousemove 70 "$(ligne 3)" click 1
fenetre 'Théière' || fail "l'alias Théière posé dans Éléments du menu Pomme n'apparaît pas"

echo 'Menu Pomme : dossier Éléments du menu Pomme, alias, sous-menu, mise à jour : OK'
echo "Captures et journaux: $OUT"
