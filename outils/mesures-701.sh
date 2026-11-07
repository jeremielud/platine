#!/bin/sh
# Mesure la session réelle puis la réactivité dans un Xvfb jetable, sans toucher à :0.
# Usage : ./outils/mesures-701.sh [hôte] [vm de compilation]
set -eu
cd "$(dirname "$0")/.."
H=${1:-eeepc}
V=${2:-vm-netbsd}
TVM=
TH=
cleanup() {
	[ -z "$TH" ] || ssh "$H" "rm -rf '$TH'" >/dev/null 2>&1 || true
	[ -z "$TVM" ] || ssh "$V" "rm -rf '$TVM'" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

TVM=$(ssh "$V" 'mktemp -d /tmp/platine-mesures.XXXXXX')
TH=$(ssh "$H" 'mktemp -d /tmp/platine-mesures.XXXXXX')
tar -cf - outils/mesure.c outils/xclic.c | ssh "$V" "set -e
tar -C '$TVM' -xf -
cc -O2 -Wall -Wextra -I/usr/X11R7/include -o '$TVM/mesure' '$TVM/outils/mesure.c' -L/usr/X11R7/lib -Wl,-R/usr/X11R7/lib -lX11
cc -O2 -Wall -Wextra -I/usr/X11R7/include -o '$TVM/xclic' '$TVM/outils/xclic.c' -L/usr/X11R7/lib -Wl,-R/usr/X11R7/lib -lXtst -lXext -lX11"
for f in mesure xclic; do
	ssh "$V" "cat '$TVM/$f'" | ssh "$H" "cat >'$TH/$f'; chmod 755 '$TH/$f'"
done

ssh "$H" "sh -s -- '$TH'" <<'FIN'
set -eu
T=$1
PATH=$T:/opt/platine/bin:/usr/X11R7/bin:/sbin:/usr/sbin:/bin:/usr/bin
export PATH
printf '\nSession réelle (RSS)\n'
ps -axo rss= -o comm= | awk '
$2 == "X" || $2 == "/usr/X11R7/bin/X" || $2 ~ /^platine-/ {
	total += $1
	printf "  %-24s %6.1f Mio\n", $2, $1 / 1024
}
END { printf "  %-24s %6.1f Mio\n", "TOTAL", total / 1024 }'

mkdir -p "$T/home"
Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset 3>"$T/display" >"$T/xvfb.log" 2>&1 &
XPID=$!
nettoyer() {
	pkill -u 0 -x platine-wm 2>/dev/null || true
	pkill -u 0 -x platine-finder 2>/dev/null || true
	pkill -u 0 -x platine-strip 2>/dev/null || true
	pkill -u 0 -x platine-about 2>/dev/null || true
	pkill -u 0 -x platine-calculette 2>/dev/null || true
	kill "$XPID" 2>/dev/null || true
}
trap nettoyer EXIT INT TERM
i=0
while [ ! -s "$T/display" ]; do
	i=$((i + 1))
	[ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
	sleep 0.05
done
DISPLAY=:$(cat "$T/display")
HOME=$T/home
PLATINE_ASSETS=/opt/platine/assets
PLATINE_SONS=0
export DISPLAY HOME PLATINE_ASSETS PLATINE_SONS
platine-wm >"$T/wm.log" 2>&1 &
WM=$!
sleep 0.5
FINDER_MS=$(mesure Bureau sh -c 'platine-finder >/dev/null 2>&1 &')
FPID=$(pgrep -u 0 -x platine-finder)
platine-strip >"$T/strip.log" 2>&1 &
SPID=$!
sleep 0.5

printf '\nSession Xvfb jetable (RSS)\n'
for e in "Xvfb:$XPID" "platine-wm:$WM" "platine-finder:$FPID" "platine-strip:$SPID"; do
	N=${e%%:*}
	P=${e#*:}
	R=$(ps -o rss= -p "$P" | awk '{ s += $1 } END { print s + 0 }')
	printf '  %-24s %6.1f Mio\n' "$N" "$(awk -v n="$R" 'BEGIN { print n / 1024 }')"
done
R=0
for P in "$XPID" "$WM" "$FPID" "$SPID"; do
	N=$(ps -o rss= -p "$P" | awk '{ s += $1 } END { print s + 0 }')
	R=$((R + N))
done
printf '  %-24s %6.1f Mio\n' TOTAL "$(awk -v n="$R" 'BEGIN { print n / 1024 }')"

printf '\nRéactivité (millisecondes)\n'
printf '  %-38s %6s\n' 'Finder : bureau affiché' "$FINDER_MS"
for i in 1 2 3 4 5; do
	if ! M=$(mesure -a xclic clicrapide 22 10); then
		xwininfo -root -tree
		cat "$T/wm.log" "$T/strip.log" >&2
		exit 1
	fi
	printf '  %-38s %6s\n' "Menu Pomme, essai $i" "$M"
	xclic touche Escape
done
xclic clicrapide 22 10
M=$(mesure "À propos de cet ordinateur" xclic clicrapide 60 30)
printf '  %-38s %6s\n' 'À propos : fenêtre affichée' "$M"
M=$(mesure Calculette sh -c 'platine-calculette >/dev/null 2>&1 &')
printf '  %-38s %6s\n' 'Calculette : fenêtre affichée' "$M"
FIN
