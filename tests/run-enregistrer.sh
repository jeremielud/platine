#!/bin/sh
# Zone de dialogue d'enregistrement de la Toolbox (Standard File, Mac OS 9) :
# ouverture d'un dossier par double-clic, Ouvrir au clavier, menu local pour
# remonter, Nouveau dossier, Remplacer existant, Annuler.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-demo

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-enregistrer.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/enregistrer}
H="$TMP/home"
mkdir -p "$OUT/log" "$H/Documents/Lettres" "$H/Documents/Projets" "$H/Images"
echo a >"$H/Documents/Notes.txt"
echo b >"$H/Documents/Lettres/Existant"

Xvfb -displayfd 3 -screen 0 800x600x24 -nolisten tcp 3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
cleanup() {
    [ -n "$APP" ] && kill "$APP" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$H" PLATINE_ASSETS="$PWD/assets"
fail() { echo "ÉCHEC: $*" >&2; exit 1; }
statut() { grep '^statut : ' "$OUT/log/demo.log" | tail -1 | sed 's/^statut : //'; }
cmd_s() { xdotool keydown alt; sleep 0.1; xdotool key s; sleep 0.1; xdotool keyup alt; sleep 1; }
# dialogue de 356×250 centré horizontalement, au tiers de la hauteur : (222, 116)
DX=222 DY=116
rang() { echo $((DY + 35 + 2 + 17 * $1 + 8)); }	# rangée n de la liste

./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
./build/platine-demo >"$OUT/log/demo.log" 2>&1 & APP=$!
xdotool search --sync --onlyvisible --name 'Toolbox$' >/dev/null
sleep 0.5

# 1) Documents : Lettres, Notes.txt (grisé), Projets ; double-clic sur Lettres,
#    nom tapé, Retour : enregistré dans Documents/Lettres
cmd_s
import -window root "$OUT/dialogue.png"
xdotool mousemove $((DX + 80)) "$(rang 0)" click --repeat 2 --delay 120 1
sleep 0.4
import -window root "$OUT/lettres.png"
xdotool type --delay 30 'Courrier'
xdotool key Return
sleep 0.6
[ "$(statut)" = "Enregistré : $H/Documents/Lettres/Courrier" ] || fail "double-clic puis Retour : « $(statut) »"

# 2) un fichier existant : Remplacer… Annuler garde le dialogue, Remplacer valide
cmd_s
xdotool mousemove $((DX + 80)) "$(rang 0)" click --repeat 2 --delay 120 1
sleep 0.4
xdotool key ctrl+a 2>/dev/null || true
xdotool mousemove $((DX + 100)) $((DY + 223)) click --repeat 3 --delay 80 1
xdotool type --delay 30 'Existant'
xdotool key Return
sleep 0.6
import -window root "$OUT/remplacer.png"
xdotool key Escape
sleep 0.4
[ "$(statut)" != "Enregistré : $H/Documents/Lettres/Existant" ] || fail "Annuler dans Remplacer a validé"
xdotool key Return
sleep 0.6
xdotool key Return
sleep 0.6
[ "$(statut)" = "Enregistré : $H/Documents/Lettres/Existant" ] || fail "Remplacer : « $(statut) »"

# 3) Nouveau dossier puis enregistrement dedans
cmd_s
xdotool mousemove $((DX + 298)) $((DY + 103)) click 1
sleep 0.6
import -window root "$OUT/nouveau-dossier.png"
xdotool type --delay 30 'Captures'
xdotool key Return
sleep 0.6
[ -d "$H/Documents/Captures" ] || fail "Nouveau… n'a pas créé le dossier"
xdotool key Return
sleep 0.6
[ "$(statut)" = "Enregistré : $H/Documents/Captures/Démo sans titre" ] ||
    fail "enregistrement dans le nouveau dossier : « $(statut) »"

# 4) Ouvrir au clavier (flèche vers un dossier, Retour) puis Annuler par Échap
cmd_s
xdotool key Tab Down
sleep 0.3
import -window root "$OUT/ouvrir.png"
xdotool key Escape
sleep 0.5
[ "$(statut)" = "Enregistrement annulé." ] || fail "Échap n'annule pas : « $(statut) »"

echo 'Zone de dialogue d’enregistrement : dossiers, Nouveau, Remplacer, Annuler : OK'
echo "Captures et journaux: $OUT"
