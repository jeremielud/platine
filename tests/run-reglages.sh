#!/bin/sh
# Réglages Apparence/Son : couleurs et polices au démarrage, thème « Aucun »
# sans supprimer l'alerte sonore choisie. Un faux aplay enregistre les appels.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-reglages.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/reglages}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC" "$OUT/log" "$STATE" "$TMP/bin"
printf 'PLAL%s/nulle-part\n' "$SRC" >"$SRC/cible.txt alias"
printf 'W\t%s\t20\t40\t350\t360\t0\n' "$SRC" >"$STATE/fenetres"
printf 'I\t%s\tcible.txt alias\t35\t50\n' "$SRC" >"$STATE/icones"
cat >"$TMP/bin/aplay" <<'FIN'
#!/bin/sh
printf '%s\n' "$*" >>"$APLAY_LOG"
FIN
chmod +x "$TMP/bin/aplay"
: >"$TMP/aplay.log"
export PATH="$TMP/bin:$PATH" APLAY_LOG="$TMP/aplay.log"
export PLATINE_ASSETS="$PWD/assets" PLATINE_SONS=1
export PLATINE_SONS_JOURNAL="$TMP/sons.log"

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
export DISPLAY="$D" HOME="$HOME_TEST"

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
lance() {
    ./build/platine-wm >>"$OUT/log/wm.log" 2>&1 & WM=$!
    sleep 0.4
    ./build/platine-finder "$SRC" >>"$OUT/log/finder.log" 2>&1 & APP=$!
    xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
    sleep 0.5
}
stoppe() {
    kill "$APP" "$WM" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    APP=
    WM=
}
couleur() {
    convert "$1" -format %c histogram:info:- | grep -qi "$2" ||
        fail "$1 ne contient pas la couleur $2"
}

lance
import -window root "$OUT/defaut.png"
xdotool mousemove 45 10 click 1
sleep 0.3
import -window root "$OUT/defaut-menu.png"
couleur "$OUT/defaut-menu.png" '#333399'
xdotool key Escape
stoppe

cat >"$HOME_TEST/.config/platine/reglages" <<'FIN'
surbrillance Vert
variante Cuivre
police_grande Chicago
police_petite Monaco
police_vues Monaco
lissage 1
theme_sonore Aucun
son_alerte sosumi
FIN
lance
import -window root "$OUT/personnalise.png"
couleur "$OUT/personnalise.png" '#e0d0c0'

# Même étiquette d'alias, avant sélection : le fond est identique, donc une
# différence dans ce crop vient bien du dessin des glyphes.
convert "$OUT/defaut.png" -crop 110x16+20+142 +repage -colorspace gray -threshold 50% "$OUT/glyphes-defaut.png"
convert "$OUT/personnalise.png" -crop 110x16+20+142 +repage -colorspace gray -threshold 50% "$OUT/glyphes-personnalise.png"
N=$(compare -metric AE "$OUT/glyphes-defaut.png" "$OUT/glyphes-personnalise.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "les polices choisies ne changent pas les glyphes"

xdotool mousemove 45 10 click 1
sleep 0.3
import -window root "$OUT/personnalise-menu.png"
couleur "$OUT/personnalise-menu.png" '#006633'

# Fichier > Nouveau dossier : l'action reste disponible, mais aucun lecteur
# audio ne doit être lancé lorsque le thème sonore est « Aucun ».
: >"$APLAY_LOG"
xdotool key Down Return
sleep 0.8
[ -d "$SRC/Nouveau dossier" ] || fail "Nouveau dossier n'a pas été créé"
[ ! -s "$APLAY_LOG" ] || fail "thème Aucun : un son d'interface a été joué"

# L'alerte n'appartient pas au thème d'interface : elle utilise son propre son.
: >"$APLAY_LOG"
xdotool mousemove 83 129 click --repeat 2 --delay 200 1
sleep 1
xdotool search --onlyvisible --name '^Alerte$' >/dev/null 2>&1 ||
    fail "aucune alerte pour l'alias brisé"
[ "$(wc -l <"$APLAY_LOG")" -eq 1 ] || fail "l'alerte n'a pas appelé le lecteur exactement une fois"
grep -Fx -- "-q $PWD/assets/alertes/sosumi.wav" "$APLAY_LOG" >/dev/null ||
    fail "l'alerte n'utilise pas sosumi.wav"
import -window root "$OUT/alerte-sosumi.png"
xdotool key Escape

echo 'Réglages Apparence/Son : OK'
echo "Captures et journaux: $OUT"
