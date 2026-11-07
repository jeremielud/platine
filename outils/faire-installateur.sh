#!/bin/sh
# faire-installateur.sh INSTALLATEUR CONTENU MANIFESTE SORTIE : fabrique un
# programme d'installation autonome, à l'image d'Installer VISE sous Mac OS 9.
#   INSTALLATEUR  le binaire platine-installateur (Linux ou NetBSD)
#   CONTENU       dossier dont le contenu est installé (racine de l'archive)
#   MANIFESTE     texte clé=valeur (voir src/apps/installateur/installateur.c)
#   SORTIE        fichier produit, par exemple « Installation de Théière »
# Fichier produit : [installateur][archive tar][manifeste][ligne de fin, 64 octets].
set -eu
I=${1:?installateur}; C=${2:?contenu}; M=${3:?manifeste}; O=${4:?sortie}
T=$(mktemp "${TMPDIR:-/tmp}/faire-installateur.XXXXXX")
trap 'rm -f "$T"' EXIT
# ustar : noms stockés tels quels (pax exige une conversion UTF-8 que la locale C
# de NetBSD refuse pour « Théière ») ; membres nommés comme dans le manifeste
(cd "$C" && tar --format=ustar -cf - *) >"$T"
taille() { wc -c <"$1" | tr -d ' '; }
ti=$(taille "$I"); tt=$(taille "$T"); tm=$(taille "$M")
cat "$I" "$T" "$M" >"$O"
# ligne de fin : magie, position et longueur de l'archive puis du manifeste
fin=$(printf 'PLATINE-INSTALLATEUR-1 %s %s %s %s' "$ti" "$tt" $((ti + tt)) "$tm")
printf '%-63s\n' "$fin" >>"$O"
chmod 755 "$O"
echo "$O : $((ti + tt + tm + 64)) octets"
