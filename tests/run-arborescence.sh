#!/bin/sh
# Arborescence façon Mac OS X (décision D11), sur un faux disque (PLATINE_VOLUME) :
# la racine ne montre que Applications, Library, System et home — les dossiers BSD
# et ceux de /.hidden sont masqués — et les noms anglais du disque sont montrés en
# français (Système, Bibliothèque, Utilisateurs, Bureau, Téléchargements…). Les
# zones de dialogue de la Toolbox voient la même chose (testé par test-identite).
set -eu
cd "$(dirname "$0")/.."

make build/platine-wm build/platine-finder build/ptk.o build/ptk_identite.o >/dev/null
cc -O2 -Itoolbox $(pkg-config --cflags x11 xft) -o build/test-identite tests/test-identite.c \
    build/ptk.o build/ptk_identite.o $(pkg-config --libs x11 xft xpm fontconfig) -lXext -lXrender

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-arbo.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/arborescence}
V="$TMP/disque"
H="$V/home/jean"
mkdir -p "$OUT/log" "$V/Applications/Utilities" "$V/System/Control Panels" "$V/Library" \
    "$V/bin" "$V/etc" "$V/usr" "$V/var" "$V/tmp" "$V/opt" "$V/secret" \
    "$H/Desktop" "$H/Documents" "$H/Downloads" "$H/Pictures" "$H/bin" "$H/.config/platine/finder2"
printf 'secret\n' >"$V/.hidden"
printf 'W\t%s\t20\t40\t400\t300\t0\n' "$V" >"$H/.config/platine/finder2/fenetres"

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
export DISPLAY=:$(cat "$TMP/display") HOME="$H" PLATINE_VOLUME="$V"
export PLATINE_ASSETS="$PWD/assets"

fail() { echo "ÉCHEC: $*" >&2; echo "disque conservé: $V" >&2; exit 1; }
pos() {
    awk -F '\t' -v f="$1" -v n="$2" '$1 == "I" && $2 == f && $3 == n { print $4; exit }' \
        "$H/.config/platine/finder2/icones" 2>/dev/null
}
attendre_fenetre() {
    timeout 6 xdotool search --sync --onlyvisible --name "^$1\$" >/dev/null ||
        fail "fenêtre « $1 » absente"
}

# 1. noms montrés : la Toolbox seule (platine-identite nom DOSSIER NOM)
I=./build/test-identite
[ "$($I nom "$V" System)" = "Système" ] || fail "System → $($I nom "$V" System)"
[ "$($I nom "$V" Library)" = "Bibliothèque" ] || fail "Library → $($I nom "$V" Library)"
[ "$($I nom "$V" home)" = "Utilisateurs" ] || fail "home → $($I nom "$V" home)"
[ "$($I nom "$V/System" "Control Panels")" = "Tableaux de bord" ] || fail "Control Panels"
[ "$($I nom "$V/Applications" Utilities)" = "Utilitaires" ] || fail "Utilities"
[ "$($I nom "$H" Desktop)" = "Bureau" ] || fail "Desktop"
[ "$($I nom "$H" Downloads)" = "Téléchargements" ] || fail "Downloads"
[ "$($I nom "$H/Documents" Downloads)" = "Downloads" ] || fail "un Downloads ailleurs ne se traduit pas"
# 2. masqués : à la racine du disque seulement
for n in bin etc usr var tmp opt secret .hidden; do
    [ "$($I masque "$V" "$n")" = 1 ] || fail "$n devrait être masqué à la racine"
done
for n in Applications Library System home; do
    [ "$($I masque "$V" "$n")" = 0 ] || fail "$n ne devrait pas être masqué"
done
[ "$($I masque "$H" bin)" = 0 ] || fail "~/bin ne doit pas être masqué"
echo 'Noms montrés et masquage : OK'

# 3. le Finder : la fenêtre du disque, les titres traduits
./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.5
./build/platine-finder "$V" >"$OUT/log/finder.log" 2>&1 & APP=$!
attendre_fenetre 'Eee PC 701'
sleep 0.6
for n in Applications Library System home; do
    [ -n "$(pos "$V" "$n")" ] || fail "$n absent de la fenêtre du disque"
done
for n in bin etc usr var tmp opt secret; do
    [ -z "$(pos "$V" "$n")" ] || fail "$n visible dans la fenêtre du disque"
done
import -window root "$OUT/disque.png"
kill "$APP"; wait "$APP" 2>/dev/null || true
./build/platine-finder "$V/System" "$H/Downloads" >>"$OUT/log/finder.log" 2>&1 & APP=$!
attendre_fenetre 'Syst.*me'
attendre_fenetre 'T.*chargements'
import -window root "$OUT/titres.png"
echo 'Finder : disque, masquage, titres traduits : OK'
echo "Captures et journaux: $OUT"
