#!/bin/sh
# Aperçu en direct de Platine dans une fenêtre 800×480 (Xephyr) sur le bureau du PC.
# Usage : ./tests/apercu.sh start|relancer|stop
# Dossier de départ séparé (~/.cache/platine-apercu) : le vrai dossier n'est jamais touché.
set -eu
cd "$(dirname "$0")/.."
D=:30
H=$HOME/.cache/platine-apercu
L=/tmp/platine-apercu
mkdir -p "$H" "$L"

arreter_bureau() {
	for b in platine-wm platine-finder platine-strip; do
		for p in $(pgrep -x "$b" || true); do
			tr '\0' '\n' <"/proc/$p/environ" 2>/dev/null | grep -qx "DISPLAY=$D" && kill "$p" || true
		done
	done
	sleep 0.3
}
lancer_bureau() {
	export DISPLAY=$D HOME="$H" XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}" PLATINE_ASSETS="$PWD/assets" PATH="$PWD/build:$PATH" PLATINE_APERCU=1 PLATINE_SONS=1 PLATINE_SONS_JOURNAL="$L/sons.log"
	setsid ./build/platine-wm >"$L/wm.log" 2>&1 &
	sleep 0.5
	setsid ./build/platine-finder >"$L/finder.log" 2>&1 &
	setsid ./build/platine-strip >"$L/strip.log" 2>&1 &
}

case ${1:-start} in
start)
	if ! DISPLAY=$D xdotool getdisplaygeometry >/dev/null 2>&1; then
		setsid Xephyr $D -screen 800x480 -title "Platine" -ac -nolisten tcp -no-host-grab -resizeable >"$L/xephyr.log" 2>&1 &
		sleep 1
	fi
	make -s >"$L/make.log" 2>&1 || { grep -E "error" "$L/make.log"; exit 1; }
	arreter_bureau
	XDG_DATA_HOME=$HOME/.local/share lancer_bureau
	;;
relancer)
	# compiler d'abord : une erreur laisse le bureau en place
	make -s >"$L/make.log" 2>&1 || { grep -E "error" "$L/make.log"; exit 1; }
	arreter_bureau
	XDG_DATA_HOME=$HOME/.local/share lancer_bureau
	;;
stop)
	arreter_bureau
	pkill -f "Xephyr $D " || true
	;;
*)
	echo "usage: $0 start|relancer|stop" >&2; exit 1 ;;
esac
