#!/bin/sh
# Disposition des icônes, règles de Mac OS 9 : grille de 128 × 64 (première
# colonne centrée à 57 px du contenu), nouveaux éléments dans la première case
# libre, Options de présentation (⌘J) : aucune / toujours aligner sur la grille
# (Ctrl inverse au dépôt) / toujours ranger par … (menu local), persistance dans
# finder2/fenetres, Nettoyer.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-finder

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-grille.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/grille}
SRC="$HOME_TEST/Source"
STATE="$HOME_TEST/.config/platine/finder2"
mkdir -p "$SRC" "$OUT/log" "$STATE"
head -c 1 /dev/zero >"$SRC/a.txt"
head -c 300 /dev/zero >"$SRC/b.txt"
head -c 20 /dev/zero >"$SRC/c.txt"
head -c 4000 /dev/zero >"$SRC/d.txt"
head -c 50 /dev/zero >"$SRC/e.txt"
printf 'W\t%s\t20\t40\t480\t380\t0\t0\t0\t0\t0\n' "$SRC" >"$STATE/fenetres"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp 3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
cleanup() {
    [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM
i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$HOME_TEST"

fail() { echo "ÉCHEC: $*" >&2; echo "HOME conservé: $HOME_TEST" >&2; trap - EXIT; cleanup; exit 1; }
lance_finder() {
    ./build/platine-finder "$SRC" >>"$OUT/log/finder.log" 2>&1 &
    APP=$!
    xdotool search --sync --onlyvisible --name '^Source$' >/dev/null
    sleep 0.6
}
# pos NOM : position enregistrée « x y »
pos() { awk -F '\t' -v d="$SRC" -v n="$1" '$1 == "I" && $2 == d && $3 == n { print $4, $5 }' "$STATE/icones"; }
attend_pos() { # attend_pos NOM "X Y" MESSAGE
    i=0
    until [ "$(pos "$1")" = "$2" ]; do
        i=$((i + 1)); [ $i -lt 30 ] || fail "$3 : $1 à « $(pos "$1") », attendu « $2 »"; sleep 0.1
    done
}
# centre de l'image d'une icône à l'écran : client à (+6, +22) de la fenêtre
# (20, 40), image à (x + 6, y + 1) dans le client
cx() { echo $(($1 + 20 + 6 + 6 + 16)); }
cy() { echo $(($1 + 40 + 22 + 1 + 16)); }
glisser() { # glisser NOM DX DY [ctrl]
    set -- "$1" "$2" "$3" "${4:-}" $(pos "$1")
    x0=$(cx "$5"); y0=$(cy "$6")
    xdotool mousemove "$x0" "$y0" mousedown 1
    sleep 0.1
    xdotool mousemove $((x0 + $2 / 2)) $((y0 + $3 / 2))
    sleep 0.1
    xdotool mousemove $((x0 + $2)) $((y0 + $3))
    sleep 0.1
    [ -z "$4" ] || xdotool keydown ctrl
    xdotool mouseup 1
    [ -z "$4" ] || xdotool keyup ctrl
    sleep 0.5
}
options() { # ouvre les Options de présentation, rend l'origine « X Y » de la zone
    xdotool key alt+j
    i=0
    until xwininfo -name 'Options de présentation' >/dev/null 2>&1; do
        i=$((i + 1)); [ $i -lt 30 ] || fail "⌘J n'ouvre pas les Options de présentation"; sleep 0.1
    done
    sleep 0.3
    xwininfo -name 'Options de présentation' | awk '/Absolute upper-left X/ { x = $4 } /Absolute upper-left Y/ { y = $4 } END { print x, y }'
}

./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
lance_finder

# 1) Premier placement : par nom, rangées de 3 cases de 128 × 64
attend_pos a.txt "35 50" "premier placement"
[ "$(pos b.txt)" = "163 50" ] && [ "$(pos c.txt)" = "291 50" ] &&
    [ "$(pos d.txt)" = "35 114" ] && [ "$(pos e.txt)" = "163 114" ] ||
    fail "grille initiale : b=$(pos b.txt) c=$(pos c.txt) d=$(pos d.txt) e=$(pos e.txt)"

# 2) Disposition « Aucune » : l'icône reste où on la pose
glisser e.txt 40 75
attend_pos e.txt "203 189" "dépôt libre"

# 3) ⌘J, « Toujours aligner sur la grille », OK : e rejoint la case la plus proche
set -- $(options)
import -window root "$OUT/options.png"
xdotool mousemove $(($1 + 58)) $(($2 + 148)) click 1
sleep 0.2
xdotool key Return
attend_pos e.txt "163 178" "alignement à l'activation"
grep -qP "^W\t$SRC\t.*\t1\t0$" "$STATE/fenetres" || fail "disposition « grille » non enregistrée"

# 4) Dépôt aimanté dans la case libre la plus proche ; Ctrl l'empêche
glisser d.txt 100 5
attend_pos d.txt "163 114" "dépôt aimanté"
glisser c.txt 20 20 ctrl
attend_pos c.txt "311 70" "Ctrl pendant le glisser"

# 5) Nettoyer (menu contextuel du fond) : c revient sur la grille
xdotool mousemove 400 380 click 3
sleep 0.4
import -window root "$OUT/contexte.png"
xdotool key Escape
sleep 0.2

# 6) « Toujours ranger » par taille, par le menu local
set -- $(options)
xdotool mousemove $(($1 + 58)) $(($2 + 172)) click 1
sleep 0.2
xdotool mousemove $(($1 + 200)) $(($2 + 171)) mousedown 1
sleep 0.4
import -window root "$OUT/menu-local.png"
xdotool mousemove $(($1 + 200)) $(($2 + 200))
sleep 0.1
xdotool mousemove $(($1 + 200)) $(($2 + 225))
sleep 0.2
xdotool mouseup 1
sleep 0.3
import -window root "$OUT/options-par-taille.png"
xdotool key Return
attend_pos d.txt "35 50" "rangement par taille"
[ "$(pos b.txt)" = "163 50" ] && [ "$(pos e.txt)" = "291 50" ] &&
    [ "$(pos c.txt)" = "35 114" ] && [ "$(pos a.txt)" = "163 114" ] ||
    fail "ordre par taille : b=$(pos b.txt) e=$(pos e.txt) c=$(pos c.txt) a=$(pos a.txt)"
grep -qP "^W\t$SRC\t.*\t2\t3$" "$STATE/fenetres" || fail "« toujours ranger par taille » non enregistré"

# 7) Fenêtre toujours rangée : une icône déplacée reprend sa place
glisser a.txt 150 60
sleep 0.5
[ "$(pos a.txt)" = "163 114" ] || fail "icône déplacée dans une fenêtre rangée : a=$(pos a.txt)"

# 8) Persistance, et nouvel élément rangé à sa place dans l'ordre
kill "$APP"; wait "$APP" 2>/dev/null || true
head -c 1000 /dev/zero >"$SRC/f.txt"
lance_finder
attend_pos f.txt "163 50" "nouvel élément dans une fenêtre rangée"
[ "$(pos b.txt)" = "291 50" ] && [ "$(pos a.txt)" = "291 114" ] ||
    fail "rangement après redémarrage : b=$(pos b.txt) a=$(pos a.txt)"
import -window root "$OUT/range.png"

echo 'Disposition des icônes : grille, aligner, Ctrl, toujours ranger, menu local, persistance : OK'
echo "Captures et journaux: $OUT"
