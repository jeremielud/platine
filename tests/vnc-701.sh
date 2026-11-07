#!/bin/sh
# Aperçu VNC de Platine sur le 701, dans une session Xvfb séparée (:16).
# Usage : ./vnc-701.sh start|stop [hôte]
# Ne touche pas à la session réelle :0.
set -eu

H=${2:-eeepc}
ACTION=${1:-start}

case "$ACTION" in
start)
	scp -qr assets "$H:/tmp/platine/"
	# Polices personnelles extraites de Mac OS 9 (non publiées).
	FONTS=ressources/macos9/extrait/polices
	if [ -f "$FONTS/Charcoal.ttf" ]; then
		ssh "$H" 'mkdir -p ~/.local/share/fonts/platine'
		scp -q "$FONTS/Charcoal.ttf" "$FONTS/Geneva.ttf" \
		    "$H:.local/share/fonts/platine/"
		ssh "$H" 'fc-cache -f >/dev/null 2>&1 || true'
	fi
	ssh "$H" 'sh -s' <<'EOF'
set -eu
mkdir -p /tmp/platine/vnc
pkill -f 'Xvfb :16' 2>/dev/null || true
pkill -f '/tmp/platine/platine-wm' 2>/dev/null || true
pkill -f '/tmp/platine/platine-finder' 2>/dev/null || true
pkill -x x11vnc 2>/dev/null || true
sleep 0.5
x11vnc -storepasswd platine /tmp/platine/vnc/pass >/dev/null 2>&1
setsid Xvfb :16 -screen 0 800x480x24 -nolisten tcp >/tmp/platine/vnc/xvfb.log 2>&1 &
sleep 1
setsid su -s /bin/sh jeremie -c 'DISPLAY=:16 PLATINE_ASSETS=/tmp/platine/assets exec /tmp/platine/platine-wm' >/tmp/platine/vnc/wm.log 2>&1 &
sleep 0.7
setsid su -s /bin/sh jeremie -c 'DISPLAY=:16 PLATINE_ASSETS=/tmp/platine/assets exec /tmp/platine/platine-finder' >/tmp/platine/vnc/finder.log 2>&1 &
sleep 1.5
setsid x11vnc -display :16 -rfbauth /tmp/platine/vnc/pass -rfbport 5901 \
    -forever -shared -noxdamage -wait 30 -defer 30 -bg \
    -o /tmp/platine/vnc/x11vnc.log
sleep 1
DISPLAY=:16 import -window root /tmp/platine/vnc/bureau.png
echo "VNC : 192.168.1.11:5901 (ou :1), mot de passe platine"
EOF
	;;
stop)
	ssh "$H" 'pkill -x platine-wm 2>/dev/null || true; pkill -x platine-finder 2>/dev/null || true; pkill -x Xvfb 2>/dev/null || true; pkill -x x11vnc 2>/dev/null || true; echo arrêté'
	;;
*)
	echo "usage: $0 start|stop [hôte]" >&2
	exit 1
	;;
esac
