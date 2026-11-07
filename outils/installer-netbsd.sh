#!/bin/sh
# Installe Platine sur le 701 sous NetBSD, depuis le PC. Compile dans la VM (vm-netbsd), à cause
# des en-têtes X qui ne sont pas sur le 701. Les programmes vont dans l'arborescence Macintosh
# du volume (Applications, Dossier Système : outils/poser-arborescence.sh, décision D9) ; le côté
# BSD (session, privilèges, outils, assets) dans /opt/platine. installer-netbsd.sh [hôte] [vm]
set -eu
cd "$(dirname "$0")/.."
H=${1:-eeepc}
V=${2:-vm-netbsd}
tar --exclude='*.o' --exclude='./platine-*' -cf - Makefile src toolbox outils/construire-netbsd.sh outils/faire-installateur.sh outils/xclic.c systeme/netbsd/platine-privilege.c systeme/installateurs |
	ssh "$V" 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin TMPDIR=/root/tmp; set -e; mkdir -p "$TMPDIR"; rm -rf /root/platine; mkdir -p /root/platine; cd /root/platine; tar -xf -
sh outils/construire-netbsd.sh >/tmp/construction.log 2>&1 || { grep -E "error|Error" /tmp/construction.log; exit 1; }
cc -O2 -o platine-privilege systeme/netbsd/platine-privilege.c; strip platine-privilege'
ssh "$H" 'mkdir -p /opt/platine/bin /opt/platine/libexec /opt/platine/assets /usr/X11R7/lib/X11/fonts/macos9 /usr/X11R7/share/icons /etc/fonts/conf.d'
# côté BSD : écran rc, outil de clic, privilèges — dans /opt/platine
ssh "$V" 'cd /root/platine; tar -cf - platine-ecran-netbsd xclic platine-privilege' |
	ssh "$H" 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin; cd /opt/platine/bin; tar -xf -; mv -f platine-privilege ../libexec/; chown root:wheel ../libexec/platine-privilege; chmod 4750 ../libexec/platine-privilege'
# les programmes Platine : dans l'arborescence Macintosh du volume de l'utilisateur
ssh "$H" 'rm -rf /tmp/platine-binaires; mkdir -p /tmp/platine-binaires'
ssh "$V" 'cd /root/platine; tar -cf - platine-wm platine-finder platine-strip platine-about platine-info platine-demo platine-osd platine-demarrage platine-calculette platine-tableau platine-alerte platine-textedit platine-theiere platine-expander platine-instantane platine-keycaps platine-pensebetes platine-profileur platine-quicktime' |
	ssh "$H" 'tar -C /tmp/platine-binaires -xf -'
ssh "$H" 'sh -s /tmp/platine-binaires jeremie; rm -rf /tmp/platine-binaires' < outils/poser-arborescence.sh
# un programme d'installation façon VISE, comme téléchargé : « Installation de Théière »
ssh "$V" 'set -e; cd /root/platine; rm -rf /tmp/contenu-theiere; mkdir /tmp/contenu-theiere
cp platine-theiere "/tmp/contenu-theiere/Théière"; cp "systeme/installateurs/theiere/Lisez-moi Théière" /tmp/contenu-theiere/
sh outils/faire-installateur.sh platine-installateur /tmp/contenu-theiere systeme/installateurs/theiere/manifeste "/tmp/Installation de Théière" >/dev/null
cat "/tmp/Installation de Théière"; rm -rf /tmp/contenu-theiere "/tmp/Installation de Théière"' |
	ssh "$H" 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin; D="/home/jeremie/Downloads"; mkdir -p "$D"; cat > "$D/Installation de Théière"; chmod 755 "$D/Installation de Théière"; chown jeremie:users "$D" "$D/Installation de Théière"'
# et celui d'Instantané, façon installeur de Snapz Pro 2 : licence, extension dans
# System/Extensions, démarrée à la fin (relève de l'instance en cours)
ssh "$V" 'set -e; cd /root/platine; rm -rf /tmp/contenu-inst; mkdir /tmp/contenu-inst
cp platine-instantane "/tmp/contenu-inst/Instantané"
cp "systeme/installateurs/instantane/Lisez-moi Instantané" "systeme/installateurs/instantane/Licence Instantané" /tmp/contenu-inst/
cp -R "systeme/installateurs/instantane/Control Strip Modules" /tmp/contenu-inst/
sh outils/faire-installateur.sh platine-installateur /tmp/contenu-inst systeme/installateurs/instantane/manifeste "/tmp/Installation d'"'"'Instantané" >/dev/null
cat "/tmp/Installation d'"'"'Instantané"; rm -rf /tmp/contenu-inst "/tmp/Installation d'"'"'Instantané"' |
	ssh "$H" 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin; F="/home/jeremie/Downloads/Installation d'"'"'Instantané"; cat > "$F"; chmod 755 "$F"; chown jeremie:users "$F"'
tar -C systeme -cf - platine-session platine-diction | ssh "$H" 'tar -C /opt/platine/bin -xf -; chmod 755 /opt/platine/bin/platine-session /opt/platine/bin/platine-diction'
tar -C systeme/netbsd -cf - privilege | ssh "$H" 'export PATH=/sbin:/usr/sbin:/bin:/usr/bin; tar -C /opt/platine/libexec -xf -; chown root:wheel /opt/platine/libexec/privilege; chmod 700 /opt/platine/libexec/privilege'
# scripts powerd : couvercle et bouton d'alimentation selon /etc/platine-energie (originaux gardés en .netbsd)
tar -C systeme/netbsd/powerd -cf - commun.sh lid_switch power_button | ssh "$H" 'set -e; export PATH=/sbin:/usr/sbin:/bin:/usr/bin
D=/etc/powerd/scripts; T=$(mktemp -d /tmp/powerd.XXXX); tar -C $T -xf -
for f in lid_switch power_button; do [ -e $D/$f.netbsd ] || cp -p $D/$f $D/$f.netbsd; install -m 555 -o root -g wheel $T/$f $D/$f; done
install -m 444 -o root -g wheel $T/commun.sh $D/platine-commun.sh; rm -rf $T
[ -e /etc/platine-energie ] || install -m 644 -o root -g wheel /dev/null /etc/platine-energie'
tar -C assets -cf - . | ssh "$H" 'rm -rf /opt/platine/assets/*; tar -C /opt/platine/assets -xf -'
tar -C systeme/polices -cf - . | ssh "$H" 'tar -C /usr/X11R7/lib/X11/fonts/macos9 -xf -'
tar -C systeme/curseurs -cf - Platine9 | ssh "$H" 'tar -C /usr/X11R7/share/icons -xf -'
tar -C systeme -cf - 90-platine-polices.conf | ssh "$H" 'tar -C /etc/fonts/conf.d -xf -'
# toutes les ressources extraites de Mac OS 9, à portée du Finder (/Library/Desktop Pictures)
tar -C ressources/macos9/extrait --exclude=afterdark --exclude=classilla --exclude=winamp --exclude=quicktime -cf - . |
	ssh "$H" 'mkdir -p /opt/platine/ressources; tar -C /opt/platine/ressources -xf -'
ssh "$H" sh -s <<'FIN'
set -e
export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/X11R7/bin
chown -R root:wheel /opt/platine/assets /usr/X11R7/lib/X11/fonts/macos9 /usr/X11R7/share/icons/Platine9
D=/home/jeremie
cat > $D/.xinitrc <<'XI'
# Bureau Platine ; si son gestionnaire de fenêtres meurt tout de suite, terminal de secours.
/opt/platine/bin/platine-session demarrer && exit 0
exec xterm -ls
XI
# /Applications, /System, /Library : posés par outils/poser-arborescence.sh (D11) ;
# dossiers personnels en anglais sur le disque, montrés en français par le Finder
mkdir -p $D/.cache $D/Documents $D/Pictures $D/Music $D/Movies $D/Downloads $D/Desktop
[ -e /Library/Desktop\ Pictures ] || ln -s /opt/platine/ressources/images "/Library/Desktop Pictures"
chown -R root:wheel /opt/platine/ressources
chown -R jeremie:users $D
fc-cache -f /usr/X11R7/lib/X11/fonts/macos9 >/dev/null 2>&1 || true
FIN
echo "installé sur $H"
