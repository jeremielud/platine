#!/bin/sh
# Control Strip : modules et personnalisation, sans toucher au système.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-strip build/platine-tableau

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-strip.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/strip}
mkdir -p "$HOME_TEST/.config/platine" "$OUT/log" "$TMP/bin"

cat >"$TMP/bin/wpa_cli" <<'FIN'
#!/bin/sh
case "$*" in
    *status) cat "$WPA_STATUS" ;;
    *list_networks) cat "$WPA_RESEAUX" ;;
esac
FIN
cat >"$TMP/bin/platine-diction" <<'FIN'
#!/bin/sh
[ "${1:-}" = etat ] && cat "$DICTION_ETAT"
FIN
cat >"$TMP/bin/xrandr" <<'FIN'
#!/bin/sh
cat <<'XR'
Screen 0: minimum 640 x 480, current 800 x 480, maximum 800 x 480
LVDS-1 connected primary 800x480+0+0
  800x480      60.00*+
  640x480      60.00
XR
FIN
cat >"$TMP/bin/xclip" <<'FIN'
#!/bin/sh
exit 0
FIN
cat >"$TMP/bin/espeak-ng" <<'FIN'
#!/bin/sh
cat >/dev/null
FIN
cat >"$TMP/bin/pkill" <<'FIN'
#!/bin/sh
[ "$*" = '-USR1 -x platine-strip' ] && kill -USR1 "$APP"
FIN
chmod +x "$TMP/bin/"*
cat >"$TMP/status" <<'FIN'
wpa_state=COMPLETED
ssid=Livebox-F550
FIN
printf '0\tLivebox-F550\tany\t[CURRENT]\n1\tReseau invite\tany\n' >"$TMP/reseaux"
printf 'arret\n' >"$TMP/diction"
export PATH="$TMP/bin:$PATH" WPA_STATUS="$TMP/status" WPA_RESEAUX="$TMP/reseaux"
export DICTION_ETAT="$TMP/diction" PLATINE_ASSETS="$PWD/assets" PLATINE_APERCU=1
export PLATINE_SONS_JOURNAL="$OUT/sons.log"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
TAB=
cleanup() {
    [ -n "${TAB:-}" ] && kill "$TAB" 2>/dev/null || true
    [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    if [ -f "$TMP/runtime/platine-diction.pid" ]; then
        kill "$(cat "$TMP/runtime/platine-diction.pid")" 2>/dev/null || true
    fi
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
export DISPLAY=:$(cat "$TMP/display") HOME="$HOME_TEST"
./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.4
./build/platine-strip >"$OUT/log/strip.log" 2>&1 & APP=$!
export APP
xdotool search --sync --onlyvisible --name '^Control Strip$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
assert_action() {
    grep -F -- "aperçu : $1" "$OUT/log/strip.log" >/dev/null ||
        fail "action absente: $1"
}
choisir() {
    x=$1
    shift
    xdotool mousemove "$x" 468 click 1
    sleep 0.2
    xdotool key "$@" Return
    sleep 0.4
}

# Le réseau choisi est retrouvé par son nom, mais seul son identifiant numérique
# est injecté dans la commande wpa_cli.
xdotool mousemove 44 468 click 1
sleep 0.2
import -window root "$OUT/airport.png"
xdotool key Down Down Down Return
sleep 0.4
assert_action 'wpa_cli -i wlan0 select_network 1'
choisir 44 Down
assert_action 'doas -n /usr/local/libexec/platine/privilege radio off'

printf 'wpa_state=INTERFACE_DISABLED\n' >"$WPA_STATUS"
choisir 44 Down
assert_action 'doas -n /usr/local/libexec/platine/privilege radio on'

choisir 106 Down
assert_action 'platine-diction marche'
printf 'marche\n' >"$DICTION_ETAT"
choisir 106 Down Down
assert_action 'platine-diction arret'
choisir 106 Down Down Down
assert_action 'platine-diction lire'

xdotool mousemove 199 468 click 1
sleep 0.2
import -window root "$OUT/resolution.png"
xdotool key Down Down Return
sleep 0.4
assert_action 'xrandr -s 640x480'
choisir 199 Down Down Down
assert_action 'platine-tableau ecran'

# Module externe (System/Control Strip Modules), posé comme le fait
# l'Installateur : le Strip le relit sur SIGUSR1 et l'ajoute en 9e position ;
# ses éléments lancent Instantané ou font ouvrir le dossier Captures.
M="$HOME_TEST/System/Control Strip Modules"
mkdir -p "$M"
cp "systeme/installateurs/instantane/Control Strip Modules/"* "$M/"
kill -USR1 "$(cat "$HOME_TEST/.cache/platine-strip.pid")"
sleep 0.8
xdotool mousemove 292 468 click 1
sleep 0.3
import -window root "$OUT/module-instantane.png"
xdotool key Escape
sleep 0.3
choisir 292 Down
assert_action 'platine-instantane ecran'
choisir 292 Down Down Down Down
assert_action 'platine-instantane panneau'
choisir 292 Down Down Down Down Down
assert_action "ouvrir $HOME_TEST/Captures"
rm -rf "$M"
kill -USR1 "$(cat "$HOME_TEST/.cache/platine-strip.pid")"
sleep 0.5

# Le service réel conserve le choix pour la prochaine session, sans doublon.
mkdir -p "$TMP/runtime"
printf 'volume 60\ndiction 0\n' >"$HOME_TEST/.config/platine/reglages"
XDG_RUNTIME_DIR="$TMP/runtime" ./systeme/platine-diction marche
sleep 0.2
[ "$(XDG_RUNTIME_DIR="$TMP/runtime" ./systeme/platine-diction etat)" = marche ] ||
    fail "le service Diction ne démarre pas"
grep -Fx 'diction 1' "$HOME_TEST/.config/platine/reglages" >/dev/null ||
    fail "l'activation de Diction n'est pas enregistrée"
XDG_RUNTIME_DIR="$TMP/runtime" ./systeme/platine-diction arret
[ "$(XDG_RUNTIME_DIR="$TMP/runtime" ./systeme/platine-diction etat)" = arret ] ||
    fail "le service Diction ne s'arrête pas"
grep -Fx 'diction 0' "$HOME_TEST/.config/platine/reglages" >/dev/null ||
    fail "l'arrêt de Diction n'est pas enregistré"
[ "$(grep -c '^diction ' "$HOME_TEST/.config/platine/reglages")" -eq 1 ] ||
    fail "le réglage Diction est dupliqué"

# Une bulle nominative apparaît au survol et disparaît en quittant la barre.
xdotool mousemove 400 400 mousemove 44 468
sleep 1
xwininfo -root -tree >"$OUT/log/fenetres.log"
BULLE=$(sed -n 's/^ *\(0x[^ ]*\) "Control Strip — info".*/\1/p' "$OUT/log/fenetres.log")
xwininfo -id "$BULLE" | grep -F 'Map State: IsViewable' >/dev/null ||
    fail "la bulle AirPort n'apparaît pas"
import -window root "$OUT/bulle.png"
xdotool mousemove 400 400
sleep 0.2
if xwininfo -id "$BULLE" | grep -F 'Map State: IsViewable' >/dev/null; then
    fail "la bulle reste visible après la sortie"
fi

# La touche par défaut reste compatible avec les anciens fichiers de réglages.
xdotool key ctrl+alt+s
sleep 0.3
grep -E '^1 ' "$HOME_TEST/.config/platine-strip" >/dev/null ||
    fail "le raccourci par défaut ne replie pas la barre"
xdotool key ctrl+alt+s
sleep 0.3

# Capture interactive : Échap annule, puis contrôle + majuscule + F7 est appliqué à chaud.
./build/platine-tableau controle >"$OUT/log/tableau.log" 2>&1 & TAB=$!
sleep 0.6
TABWIN=$(xdotool search --onlyvisible --name '^Control Strip$' | head -1)
xdotool windowfocus "$TABWIN"
xdotool mousemove 500 151 click 1
sleep 0.2
xdotool key Escape
xdotool mousemove 500 151 click 1
sleep 0.2
xdotool key ctrl+shift+F7
sleep 0.5
grep -Fx 'touche F7 5' "$HOME_TEST/.config/platine-strip" >/dev/null ||
    fail "le nouveau raccourci n'est pas enregistré"
import -window root "$OUT/reglages-raccourci.png"
xdotool key F7
sleep 0.2
grep -E '^0 ' "$HOME_TEST/.config/platine-strip" >/dev/null ||
    fail "la touche seule déclenche le raccourci"
xdotool key ctrl+shift+F7
sleep 0.3
grep -E '^1 ' "$HOME_TEST/.config/platine-strip" >/dev/null ||
    fail "le raccourci rechargé ne replie pas la barre"
xdotool key ctrl+shift+F7
sleep 0.3
xdotool key ctrl+q
sleep 0.3
TAB=

for code in btnp btnr tabp tabr; do
    grep -Fx "$code" "$PLATINE_SONS_JOURNAL" >/dev/null ||
        fail "retour sonore absent: $code"
done
import -window root "$OUT/controle-strip.png"
if grep -F 'tableau de bord à venir' "$OUT/log/strip.log" >/dev/null; then
    fail "une commande est restée sans action"
fi

echo 'Control Strip : modules, raccourci, bulles et sons : OK'
echo "Captures et journaux: $OUT"
