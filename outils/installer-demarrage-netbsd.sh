#!/bin/sh
# EN PAUSE (2026-11-06) : projet abandonné pour l'instant, ne pas lancer sans décision.
# Démarrage façon Mac OS 9 sur le 701 (NetBSD) : chargeur muet (Maj au démarrage = texte et
# menu), noyau PLATINE avec SPLASHSCREEN (Happy Mac dès qu'intelfb prend l'écran, messages
# noyau en noir sur noir avant), écran rc platine-ecran-netbsd, reprise par platine-demarrage.
# Le noyau et le chargeur viennent de la VM build.img (voir ~/Projets/eeepc-netbsd/noyau).
# installer-demarrage-netbsd.sh [hôte] [vm]
set -eu
cd "$(dirname "$0")/.."
H=${1:-eeepc}
V=${2:-vm-netbsd}
N=$HOME/Projets/eeepc-netbsd/noyau
SRC=/home/jeremie/usr/src/sys/arch/i386
ssh "$V" "cat $SRC/compile/PLATINE/netbsd" >"$N/netbsd-platine"
ssh "$V" "cat $SRC/stand/boot/biosboot/boot" >"$N/boot"
convert assets/happymac.ppm "$N/platine-splash.png"
tar -C "$N" -cf - netbsd-platine boot platine-splash.png |
	ssh "$H" 'T=/root/demarrage; rm -rf $T; mkdir -p $T; tar -C $T -xf -'
tar -C systeme/netbsd/demarrage -cf - platine_ecran boot.cfg | ssh "$H" 'tar -C /root/demarrage -xf -'
ssh "$H" sh -s <<'FIN'
set -e
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
T=/root/demarrage
# noyau : l'ancien reste disponible (entrée « noyau précédent »)
[ -e /netbsd-platine.old ] || cp -p /netbsd-platine /netbsd-platine.old
install -m 755 $T/netbsd-platine /netbsd-platine
# chargeur : bootxx charge /boot par son nom ; l'original est gardé en /boot.netbsd
[ -e /boot.netbsd ] || cp -p /boot /boot.netbsd
install -m 444 $T/boot /boot
install -m 444 $T/platine-splash.png /platine-splash.png
[ -e /boot.cfg.netbsd ] || cp -p /boot.cfg /boot.cfg.netbsd
install -m 644 $T/boot.cfg /boot.cfg
install -m 555 $T/platine_ecran /etc/rc.d/platine_ecran
# /etc/rc n'est pas modifié : l'écran lit /var/run/rc.log (une ancienne version ajoutait une ligne)
if grep -q platine-etapes /etc/rc; then cp -p /etc/rc.netbsd /etc/rc; fi
# l'écran rc démarre juste après le montage de la racine
grep -q platine_ecran /etc/rc.conf || sed -i.netbsd 's|/etc/rc.d/root |/etc/rc.d/root /etc/rc.d/platine_ecran |' /etc/rc.conf
grep -q 'rc.d/platine_ecran' /etc/rc.conf
# X reprend le contenu du framebuffer (dernière image de l'écran rc) au lieu d'un écran noir
grep -q 'background none' /home/jeremie/.profile ||
	sed -i 's|exec startx -- -nolisten tcp|exec startx -- -nolisten tcp -background none|' /home/jeremie/.profile
echo "démarrage Platine installé"
FIN
