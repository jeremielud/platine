#!/bin/sh
# Théière : rendu OpenGL/GLX sous le bureau, images par seconde au titre, et
# décision D8 — TextEdit et Théière sont au menu Pomme et s'y lancent.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-theiere build/platine-textedit

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-theiere.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/theiere}
mkdir -p "$HOME_TEST" "$OUT/log"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
TE=
cleanup() {
    [ -n "$TE" ] && kill "$TE" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$TE" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1))
    [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
D=:$(cat "$TMP/display")
export DISPLAY="$D"
export HOME="$HOME_TEST"

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
# xdotool search méconnaît les accents de WM_NAME (octets UTF-8 dans une
# propriété STRING) mais getwindowname les rend exacts : on filtre en shell.
noms_theiere() { xdotool search --name '^Th' getwindowname %@ 2>/dev/null || true; }
present_th() { noms_theiere | grep -q "^$1"; }
attendre_theiere() {
    i=0; until present_th "$1"; do i=$((i+1)); [ $i -lt 100 ] || return 1; sleep 0.1; done
}
attendre_absent_th() {
    i=0; while present_th "$1"; do i=$((i+1)); [ $i -lt 100 ] || return 1; sleep 0.1; done
}
present() { xdotool search --name "$1" >/dev/null 2>&1; }
attendre_absent() {
    i=0; while present "$1"; do i=$((i+1)); [ $i -lt 50 ] || return 1; sleep 0.1; done
}

# PATH restreint au répertoire des binaires pour le WM : le menu Pomme montre
# exactement Calculette, Tableaux de bord, TextEdit et Théière (les programmes
# absents ne sortent pas), et les hauteurs de lignes sont connues :
# À propos 16, séparateur 6, puis 18 px par ligne à icône depuis y = 20.
PATH="$PWD/build" ./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5
./build/platine-theiere >"$OUT/log/theiere.log" 2>&1 &
TE=$!
attendre_theiere 'Théière$' || fail "la fenêtre Théière n'est pas apparue"
sleep 0.5

# 1) Le moteur rendu est annoncé, avec sa nature (matériel ou logiciel).
grep -q "Théière : moteur" "$OUT/log/theiere.log" ||
    fail "le moteur rendu n'est pas annoncé"

# 2) Une seconde plus tard, le titre porte les images par seconde.
sleep 2
present_th 'Théière — .* — [0-9]* i/s$' ||
    fail "le titre n'affiche pas les images par seconde"

# 3) Elle tourne : deux captures espacées diffèrent.
import -window root "$OUT/image-a.png"
sleep 0.6
import -window root "$OUT/image-b.png"
N=$(compare -metric AE "$OUT/image-a.png" "$OUT/image-b.png" null: 2>&1 || true)
[ "$N" -gt 100 ] 2>/dev/null || fail "la théière ne s'anime pas ($N pixels changés)"

# 3b) Elle suit la taille de la fenêtre : la silhouette couvre une bonne part
# de la zone cliente (le fond plat 0,725 compte pour rien).
WID=$(xdotool search --name '^Th' | head -1)
X=$(xwininfo -id "$WID" | sed -n 's/^ *Absolute upper-left X: *//p')
Y=$(xwininfo -id "$WID" | sed -n 's/^ *Absolute upper-left Y: *//p')
W=$(xwininfo -id "$WID" | sed -n 's/^ *Width: *//p')
H=$(xwininfo -id "$WID" | sed -n 's/^ *Height: *//p')
import -window root -crop "${W}x${H}+${X}+${Y}" +repage "$OUT/zone.png"
convert -size "${W}x${H}" 'xc:rgb(185,185,185)' "$TMP/fond.png"
AE=$(compare -metric AE -fuzz 5% "$OUT/zone.png" "$TMP/fond.png" null: 2>&1 || true)
TOT=$((W * H))
P=$(awk -v n="$AE" -v t="$TOT" 'BEGIN { printf "%d", 100 * n / t }')
echo "couverture de la zone cliente : ${P} %"
[ "$P" -ge 15 ] 2>/dev/null || fail "la théière est trop petite dans la fenêtre (${P} % de la zone)"

# 3c) Toolbox GL : repliée (WindowShade, double-clic sur la barre de titre),
# plus aucun rendu — le processus dort sur la connexion X ; la rotation
# reprend au dépli.
ticks() { awk '{ print $14 + $15 }' "/proc/$TE/stat"; }
replier() { xdotool mousemove $((X + W / 2)) $((Y - 9)) click --repeat 2 --delay 120 1; }
replier
sleep 0.6
T0=$(ticks); sleep 2; T1=$(ticks)
echo "processeur pendant 2 s fenêtre cachée : $((T1 - T0)) ticks"
[ $((T1 - T0)) -le 2 ] || fail "la théière cachée continue de dessiner ($((T1 - T0)) ticks en 2 s)"
replier
sleep 0.6
import -window root "$OUT/image-c.png"
sleep 0.6
import -window root "$OUT/image-d.png"
N=$(compare -metric AE "$OUT/image-c.png" "$OUT/image-d.png" null: 2>&1 || true)
[ "$N" -gt 100 ] 2>/dev/null || fail "la théière ne reprend pas après la pause ($N pixels changés)"

# 4) Décision D8 : TextEdit au menu Pomme (y = 20 + 16 + 6 + 18 + 18 + 9).
import -window root -crop 200x130+0+18 +repage "$OUT/pomme-ferme.png" || true
xdotool mousemove 22 10 click 1
sleep 0.3
import -window root "$OUT/pomme-ouvert.png"
xdotool mousemove 70 87 click 1
timeout 5 xdotool search --sync --onlyvisible --name '^Sans titre$' >/dev/null ||
    fail "l'entrée TextEdit du menu Pomme n'a pas lancé TextEdit"
xdotool keydown alt; sleep 0.15; xdotool key w; sleep 0.1; xdotool keyup alt
attendre_absent '^Sans titre$' || fail "TextEdit ouvert du menu ne se ferme pas"

# 5) Théière au menu Pomme (y = 20 + 16 + 6 + 18 + 18 + 18 + 9).
xdotool mousemove 22 10 click 1
sleep 0.3
xdotool mousemove 70 105 click 1
sleep 1
[ "$(noms_theiere | grep -c '^Théière')" -ge 2 ] ||
    fail "l'entrée Théière du menu Pomme n'a pas ouvert une seconde théière"
xdotool key Escape
sleep 0.5
if present_th 'Théière'; then
    # le WM n'a pas _NET_ACTIVE_WINDOW : on focalise la théière restante
    # par un clic (focus au clic), géométrie réelle via le serveur
    for WID in $(xdotool search --name '^Th'); do
        case "$(xdotool getwindowname "$WID")" in
        Théière*)
            X=$(xwininfo -id "$WID" | sed -n 's/^ *Absolute upper-left X: *//p')
            Y=$(xwininfo -id "$WID" | sed -n 's/^ *Absolute upper-left Y: *//p')
            W=$(xwininfo -id "$WID" | sed -n 's/^ *Width: *//p')
            H=$(xwininfo -id "$WID" | sed -n 's/^ *Height: *//p')
            xdotool mousemove $((X + W / 2)) $((Y + H / 2)) click 1
            break
            ;;
        esac
    done
    sleep 0.3
    xdotool key Escape
fi
attendre_absent_th 'Théière' || fail "les théières ne se sont pas fermées"

echo 'Théière : GLX, images par seconde et menu Pomme (D8) : OK'
echo "Captures et journaux: $OUT"
