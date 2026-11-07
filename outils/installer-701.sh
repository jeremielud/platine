#!/bin/sh
# Installe le bureau Platine sur le 701, dans /opt/platine, avec ce dont il a besoin hors de
# son dossier (polices, curseurs, actions root, touches Fn). Compiler d'abord :
# ../eeepc-alpine/alpine-x86.sh (voir README). installer-701.sh [hôte] [racine, /mnt pendant
# l'installation du système par installer-base-701.sh]
set -eu
cd "$(dirname "$0")/.."
H=${1:-eeepc}
C=${2:-}
R=../eeepc-alpine/racine-x86/root/platine/build
if [ -z "$C" ]; then
	ssh "$H" 'test "$(uname -s)" = Linux || { echo "701 NetBSD : utiliser outils/installer-netbsd.sh" >&2; exit 1; }'
fi
ssh "$H" "mkdir -p $C/opt/platine/bin $C/opt/platine/assets $C/usr/local/libexec/platine $C/etc/acpi $C/usr/share/fonts/macos9 $C/usr/share/icons/Platine9 $C/etc/fonts/conf.d"
for b in platine-wm platine-finder platine-strip platine-about platine-info platine-tableau; do
	scp -q "$R/$b" "$H:$C/opt/platine/bin/$b.neuf"
	ssh "$H" "mv -f $C/opt/platine/bin/$b.neuf $C/opt/platine/bin/$b && chmod 755 $C/opt/platine/bin/$b"
done
scp -q systeme/platine-session systeme/platine-connexion systeme/platine-diction "$H:$C/opt/platine/bin/"
scp -q systeme/privilege "$H:$C/usr/local/libexec/platine/privilege"
scp -q systeme/acpi-handler.sh "$H:$C/etc/acpi/handler.sh"
scp -q systeme/90-platine-polices.conf "$H:$C/etc/fonts/conf.d/"
ssh "$H" "chmod 755 $C/opt/platine/bin/platine-session $C/opt/platine/bin/platine-connexion $C/opt/platine/bin/platine-diction $C/usr/local/libexec/platine/privilege $C/etc/acpi/handler.sh"
tar --exclude='*.wav.tmp' -C assets -cf - . | ssh "$H" "tar -C $C/opt/platine/assets -xf -"
tar -C systeme/polices -cf - . | ssh "$H" "tar -C $C/usr/share/fonts/macos9 -xf -"
tar -C systeme/curseurs/Platine9 -cf - . | ssh "$H" "tar -C $C/usr/share/icons/Platine9 -xf -"
ssh "$H" "mkdir -p $C/usr/share/themes/Mac-OS-9"
tar -C systeme/themes/Mac-OS-9 -cf - . | ssh "$H" "tar -C $C/usr/share/themes/Mac-OS-9 -xf -"
ssh "$H" C="$C" sh -s <<'FIN'
set -e
PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/X11R7/bin
D=$C/home/jeremie
X=$D/.xinitrc
cat > "$X" <<'XI'
# Bureau Platine ; si son gestionnaire de fenêtres meurt tout de suite, terminal de secours.
/opt/platine/bin/platine-session demarrer && exit 0
exec xterm -ls
XI
rm -f "$X.ancien"
chown 1000:1000 "$X"
A="$D/.local/share/platine/Eee PC/Applications"
mkdir -p "$A"
[ -e "$D/Applications" ] || [ -L "$D/Applications" ] || ln -s "/home/jeremie/.local/share/platine/Eee PC/Applications" "$D/Applications"
mkdir -p "$D/.cache"
chown -R 1000:1000 "$D/.local/share/platine" "$D/.cache"
chown -h 1000:1000 "$D/Applications"
FIN
echo "installé sur $H"
