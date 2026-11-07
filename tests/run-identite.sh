#!/bin/sh
# Identité embarquée (section .platine) et arborescence du volume : lecture,
# survie à strip, base de bureau par signature et par extension, références
# « @chemin|repli » et « #signature|repli » citées pour sh -c.
set -eu
cd "$(dirname "$0")/.."
make build/platine-textedit build/platine-tableau build/platine-theiere build/ptk.o build/ptk_identite.o
cc -O2 -Itoolbox $(pkg-config --cflags x11 xft) -o build/test-identite tests/test-identite.c \
    build/ptk.o build/ptk_identite.o $(pkg-config --libs x11 xft xpm fontconfig) -lXext -lXrender
T=$(mktemp -d ${TMPDIR:-/tmp}/platine-identite.XXXXXX)
trap 'rm -rf "$T"' EXIT
fail() { echo "ÉCHEC: $*" >&2; exit 1; }
I=./build/test-identite

$I lire build/platine-textedit >"$T/id"
grep -qx 'nom=TextEdit' "$T/id" && grep -qx 'signature=ttxt' "$T/id" &&
    grep -qx 'icone=app-texte' "$T/id" && grep -q '^types=.*\btxt\b' "$T/id" ||
    fail "identité de TextEdit : $(tr '\n' ' ' <"$T/id")"
cp build/platine-theiere "$T/th"; strip "$T/th"
$I lire "$T/th" | grep -qx 'nom=Théière' || fail "l'identité ne survit pas à strip"
printf '#!/bin/sh\n' >"$T/script"; chmod +x "$T/script"
! $I lire "$T/script" || fail "un script n'a pas d'identité"

# volume : Applications/TextEdit, System/Control Panels
V="$T/vol"
mkdir -p "$V/Applications/Utilities" "$V/System/Control Panels"
cp build/platine-textedit "$V/Applications/Utilities/TextEdit"
cp build/platine-tableau "$V/System/Control Panels/Économiseur d'énergie"
export PLATINE_VOLUME="$V"
[ "$($I signature ttxt)" = "$V/Applications/Utilities/TextEdit" ] || fail "signature ttxt introuvable"
[ "$($I extension md)" = "$V/Applications/Utilities/TextEdit" ] || fail "extension md : pas de programme"
! $I signature zzzz || fail "signature inconnue trouvée"
R=$($I resoudre "@System/Control Panels/Économiseur d'énergie|platine-tableau economiseur")
[ "$(sh -c "printf '%s' $R")" = "$V/System/Control Panels/Économiseur d'énergie" ] ||
    fail "citation de « Économiseur d'énergie » : $R"
[ "$($I resoudre '@Applications/Absent|platine-absent')" = platine-absent ] || fail "repli de @"
[ "$($I resoudre '#ttxt|platine-textedit')" = "'$V/Applications/Utilities/TextEdit'" ] || fail "résolution #ttxt"
PLATINE_VOLUME=/nulle-part
[ "$($I resoudre '#ttxt|platine-textedit')" = platine-textedit ] || fail "repli de #"
echo 'Identité embarquée et arborescence du volume : OK'
