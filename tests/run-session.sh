#!/bin/sh
# Session : Éteindre ferme les applications puis appelle « privilege arret » ; une application
# têtue interrompt l'arrêt (alerte, « Éteindre quand même ») ; ⌘⌥Échap force à quitter.
# Un faux doas enregistre les appels privilégiés : rien n'est réellement éteint.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-alerte >/dev/null
TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-session.XXXXXX)
OUT=${OUT:-/tmp/platine-tests}/session
mkdir -p "$OUT" "$TMP/bin" "$TMP/home"
cc -O2 -Wall -o "$TMP/bin/session-client" tests/session-client.c -lX11
cat >"$TMP/bin/doas" <<'FIN'
#!/bin/sh
printf '%s\n' "$*" >>"$DOAS_LOG"
FIN
chmod +x "$TMP/bin/doas"
: >"$TMP/doas.log"
export PATH="$TMP/bin:$PWD/build:$PATH" DOAS_LOG="$TMP/doas.log" HOME="$TMP/home"
export PLATINE_ASSETS="$PWD/assets" PLATINE_ARRET_DELAI=3

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset 3>"$TMP/display" >/dev/null 2>&1 &
XPID=$!
WM=
cleanup() {
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    pkill -f "$TMP/bin/session-client" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display")
fail() { echo "ÉCHEC: $*" >&2; cat "$OUT/wm.log" >&2; exit 1; }
./build/platine-wm >"$OUT/wm.log" 2>&1 & WM=$!
sleep 0.5

client() { session-client "$1" "$2" >/dev/null 2>&1 & timeout 5 xdotool search --sync --name "^$1\$" >/dev/null || fail "fenêtre $1 absente"; }
present() { xdotool search --name "^$1\$" >/dev/null 2>&1; }
attendre_absent() {
    i=0; while present "$1"; do i=$((i+1)); [ $i -lt 50 ] || return 1; sleep 0.1; done
}
attendre_log() {
    i=0; while ! grep -q "$1" "$TMP/doas.log"; do i=$((i+1)); [ $i -lt 60 ] || return 1; sleep 0.1; done
}

# 1. Arrêt normal : une application obéissante et une sans WM_DELETE_WINDOW
client Obeissant obeit
client Sans sans
session-client --arret 1 0
attendre_absent Obeissant || fail "Obeissant n'a pas été fermé"
attendre_absent Sans || fail "Sans n'a pas été fermé"
attendre_log "privilege arret" || fail "privilege arret non appelé"
echo "ok   arrêt : applications fermées puis privilege arret"
: >"$TMP/doas.log"

# 2. Application têtue : arrêt interrompu, alerte, Retour = « Éteindre quand même »
client Tetu tetu
session-client --arret 2 0
sleep 4.5
present Tetu || fail "Tetu a disparu sans forçage"
grep -q privilege "$TMP/doas.log" && fail "redémarrage lancé malgré Tetu"
import -window root "$OUT/arret-bloque.png"
xdotool key Return
attendre_absent Tetu || fail "Tetu n'a pas été forcé"
attendre_log "privilege redemarrer" || fail "privilege redemarrer non appelé après forçage"
echo "ok   application têtue : alerte puis redémarrage forcé"
: >"$TMP/doas.log"

# 3. Têtue puis Annuler : rien n'est éteint
client Tetu tetu
session-client --arret 1 0
sleep 4.5
xdotool key Escape
sleep 1
present Tetu || fail "Tetu fermé malgré Annuler"
grep -q privilege "$TMP/doas.log" && fail "arrêt lancé malgré Annuler"
echo "ok   Annuler conserve la session"

# 4. ⌘⌥Échap sur l'application active
xdotool search --name '^Tetu$' windowactivate --sync 2>/dev/null || true
sleep 0.3
xdotool key ctrl+alt+Escape
sleep 1
import -window root "$OUT/forcer.png"
xdotool key Return
attendre_absent Tetu || fail "Forcer à quitter n'a pas tué Tetu"
grep -q privilege "$TMP/doas.log" && fail "Forcer à quitter a appelé privilege"
echo "ok   ⌘⌥Échap force l'application à quitter"
