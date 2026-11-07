#!/bin/sh
# Instantané (inspiré de Snapz Pro 2) : ⌘⇧3 fige l'écran et ouvre le panneau ;
# Écran, Fenêtre (désignée sur l'image figée), Sélection (rectangle tracé),
# échelle, presse-papiers image/png et nom choisi par la zone de dialogue
# d'enregistrement de la Toolbox.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-instantane build/platine-calculette

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-instantane.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/instantane}
H="$TMP/home"
mkdir -p "$OUT/log" "$H/Documents"
cc -O2 -o "$TMP/presse" tests/clipboard-client.c -lX11

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp 3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
CA=
IN=
cleanup() {
    [ -n "$IN" ] && kill "$IN" 2>/dev/null || true
    [ -n "$CA" ] && kill "$CA" 2>/dev/null || true
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
journal() { grep '^instantane : ' "$OUT/log/instantane.log" | tail -1 | sed 's/^instantane : //'; }
raccourci() {
    xdotool keydown alt shift; sleep 0.1; xdotool key 3; sleep 0.1; xdotool keyup alt shift
    i=0; until [ "$(journal)" = panneau ]; do i=$((i + 1)); [ $i -lt 40 ] || return 1; sleep 0.1; done
    sleep 0.6
}
attendre() { # attendre MOTIF : une ligne de journal qui commence par MOTIF
    i=0; until journal | grep -q "^$1"; do i=$((i + 1)); [ $i -lt 50 ] || return 1; sleep 0.1; done
}
dims() { identify -format '%wx%h' "$1"; }
C="$H/Captures"

./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
./build/platine-calculette >"$OUT/log/calculette.log" 2>&1 & CA=$!
./build/platine-instantane >"$OUT/log/instantane.log" 2>&1 & IN=$!
attendre 'prêt' || fail "Instantané ne démarre pas"
xdotool search --sync --onlyvisible --name '^Calculette$' >/dev/null
sleep 0.5
import -window root "$OUT/avant.png"

# 1) Écran : l'écran tel qu'il était à la frappe (sans le panneau)
raccourci || fail "⌘⇧3 n'ouvre pas le panneau"
import -window root "$OUT/panneau.png"
xdotool key e
attendre "$C/Capture 1.png" || fail "la capture de l'écran n'est pas enregistrée"
[ "$(dims "$C/Capture 1.png")" = 800x480 ] || fail "capture d'écran : $(dims "$C/Capture 1.png")"
N=$(compare -metric AE -crop 800x460+0+20 "$OUT/avant.png" "$C/Capture 1.png" null: 2>&1 || true)
[ "${N%%.*}" -lt 400 ] 2>/dev/null || fail "la capture ne montre pas l'écran figé ($N pixels différents)"

# 2) Fenêtre : la Calculette désignée sur l'image figée
CW=$(xdotool search --name '^Calculette$' | head -1)
CX=$(xwininfo -id "$CW" | sed -n 's/^ *Absolute upper-left X: *//p')
CY=$(xwininfo -id "$CW" | sed -n 's/^ *Absolute upper-left Y: *//p')
raccourci || fail "⌘⇧3 (2)"
xdotool key f
sleep 0.6
xdotool mousemove $((CX + 30)) $((CY + 60))
sleep 0.4
import -window root "$OUT/fenetre-designee.png"
xdotool click 1
attendre "$C/Capture 2.png" || fail "la capture d'une fenêtre n'est pas enregistrée"
D=$(dims "$C/Capture 2.png")
W=${D%x*} HH=${D#*x}
[ "$W" -ge 100 ] && [ "$W" -le 130 ] && [ "$HH" -ge 160 ] && [ "$HH" -le 200 ] ||
    fail "capture de la Calculette : $D"

# 3) Sélection : rectangle tracé, à 50 %
raccourci || fail "⌘⇧3 (3)"
xdotool key s
sleep 0.6
xdotool mousemove 120 300 mousedown 1 mousemove 200 340 mousemove 320 400
sleep 0.4
import -window root "$OUT/selection.png"
xdotool mouseup 1
attendre "$C/Capture 3.png" || fail "la capture d'une sélection n'est pas enregistrée"
[ "$(dims "$C/Capture 3.png")" = 201x101 ] || fail "sélection : $(dims "$C/Capture 3.png")"

# 4) Presse-papiers, échelle 50 % : réglages passés par le fichier de Préférences
kill "$IN" 2>/dev/null || true; wait "$IN" 2>/dev/null || true
sleep 0.5
mkdir -p "$H/.config/platine"
printf 'envoyer=Presse-papiers\ncouleurs=Niveaux de gris\nechelle=50 %%\nformat=PNG\ncurseur=0\nchoisir=0\n' \
    >"$H/.config/platine/instantane"
./build/platine-instantane >"$OUT/log/instantane.log" 2>&1 & IN=$!
attendre 'prêt' || fail "Instantané ne redémarre pas"
raccourci || fail "⌘⇧3 (4)"
xdotool key e
attendre 'presse-papiers 400x240' || fail "capture vers le presse-papiers : « $(journal) »"
timeout 5 "$TMP/presse" cibles | grep -qx 'image/png' ||
    fail "le presse-papiers n'annonce pas image/png"
timeout 5 "$TMP/presse" get-cible image/png >"$OUT/presse-papiers.png"
[ "$(dims "$OUT/presse-papiers.png")" = 400x240 ] || fail "image du presse-papiers : $(dims "$OUT/presse-papiers.png")"

# 5) Choisir le nom : zone de dialogue d'enregistrement de la Toolbox
kill "$IN" 2>/dev/null || true; wait "$IN" 2>/dev/null || true
sleep 0.5
printf 'envoyer=Dossier Captures\ncouleurs=Inchangées\nechelle=100 %%\nformat=PNG\ncurseur=1\nchoisir=1\n' \
    >"$H/.config/platine/instantane"
./build/platine-instantane >"$OUT/log/instantane.log" 2>&1 & IN=$!
attendre 'prêt' || fail "Instantané ne redémarre pas (2)"
raccourci || fail "⌘⇧3 (5)"
xdotool key e
sleep 1.2
import -window root "$OUT/enregistrer.png"
xdotool type --delay 30 'Mon bureau.png'
xdotool key Return
attendre "$C/Mon bureau.png" || fail "le nom choisi n'est pas respecté : « $(journal) »"

# 6) captures directes (module Control Strip) : une nouvelle instance prend la
#    relève et capture sans panneau — l'écran tout de suite, la sélection tracée
printf 'envoyer=Dossier Captures\ncouleurs=Inchangées\nechelle=100 %%\nformat=PNG\ncurseur=0\nchoisir=0\n' \
    >"$H/.config/platine/instantane"
./build/platine-instantane ecran >"$OUT/log/directe.log" 2>&1 &
D1=$!
i=0; until [ -f "$C/Capture 4.png" ]; do i=$((i + 1)); [ $i -lt 50 ] || fail "capture directe de l'écran absente"; sleep 0.1; done
[ "$(dims "$C/Capture 4.png")" = 800x480 ] || fail "capture directe : $(dims "$C/Capture 4.png")"
grep -q panneau "$OUT/log/directe.log" && fail "la capture directe a ouvert le panneau"
kill -0 "$IN" 2>/dev/null && fail "l'ancienne instance n'a pas cédé la place"
./build/platine-instantane selection >"$OUT/log/directe2.log" 2>&1 &
D2=$!
sleep 1
xdotool mousemove 100 100 mousedown 1 mousemove 160 140 mousemove 200 160
sleep 0.3
xdotool mouseup 1
i=0; until [ -f "$C/Capture 5.png" ]; do i=$((i + 1)); [ $i -lt 50 ] || fail "capture directe de la sélection absente"; sleep 0.1; done
[ "$(dims "$C/Capture 5.png")" = 101x61 ] || fail "sélection directe : $(dims "$C/Capture 5.png")"
kill -0 "$D1" 2>/dev/null && fail "deux instances tournent"
IN=$D2

# 7) Envoyer vers : Enregistrer sous… — dossier choisi dans la zone de dialogue
#    (Bureau, puis double-clic sur Documents), retenu pour la capture suivante
kill "$IN" 2>/dev/null || true; wait "$IN" 2>/dev/null || true
printf 'envoyer=Enregistrer sous…\ncouleurs=Inchangées\nechelle=100 %%\nformat=PNG\ncurseur=0\nchoisir=0\n' \
    >"$H/.config/platine/instantane"
DX=222 DY=76	# zone de dialogue de 356×250 sur l'écran de 800×480
./build/platine-instantane ecran >"$OUT/log/sous.log" 2>&1 & IN=$!
sleep 1.5
import -window root "$OUT/enregistrer-sous.png"
xdotool mousemove $((DX + 298)) $((DY + 76)) click 1
sleep 0.5
xdotool mousemove $((DX + 80)) $((DY + 35 + 2 + 17 + 8)) click --repeat 2 --delay 120 1
sleep 0.5
import -window root "$OUT/enregistrer-sous-documents.png"
xdotool type --delay 30 'Ailleurs.png'
xdotool key Return
i=0; until [ -f "$H/Documents/Ailleurs.png" ]; do i=$((i + 1)); [ $i -lt 50 ] || fail "Enregistrer sous… n'a pas écrit dans Documents"; sleep 0.1; done
grep -qx "dossier=$H/Documents" "$H/.config/platine/instantane" || fail "le dossier choisi n'est pas retenu"
./build/platine-instantane ecran >"$OUT/log/sous2.log" 2>&1 & IN=$!
sleep 1.5
xdotool key Return
i=0; until [ -f "$H/Documents/Capture 1.png" ]; do i=$((i + 1)); [ $i -lt 50 ] || fail "la capture suivante ne s'ouvre pas dans Documents"; sleep 0.1; done

echo 'Instantané : panneau, écran figé, fenêtre, sélection, presse-papiers, nom choisi, captures directes, Enregistrer sous : OK'
echo "Captures et journaux: $OUT"
