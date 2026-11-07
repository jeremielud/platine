#!/bin/sh
# StuffIt Expander : les moteurs hors-ligne (tar.gz, MacBinary III, BinHex ➔
# tar emboîté) et le passage au premier plan de l'instance unique.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-expander

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-expander.XXXXXX)
OUT=${OUT:-/tmp/platine-tests/expander}
mkdir -p "$TMP/home/Documents" "$TMP/archives" "$OUT/log"

# --- fabriquer les archives d'essai (outils de la base + encodeurs en python) ---
(
    cd "$TMP/archives"
    printf 'un\n' >un.txt
    printf 'deux\n' >deux.txt
    tar czf mon.tar.gz un.txt deux.txt
    python3 - "$TMP/archives" <<'PY'
import struct, sys, tarfile, io

archives = sys.argv[1]

# MacBinary III : nom « essai.txt », data fork = le texte, en-tête de 128 octets
nom = b"essai.txt"
data = b"le fork de cet essai decode"
h = bytearray(128)
h[0] = 0
h[1] = len(nom)
h[2:2 + len(nom)] = nom
h[65:69] = b"TEXT"
h[69:73] = b"unsi"
h[83:87] = len(data).to_bytes(4, "big")
h[87:91] = (0).to_bytes(4, "big")
open(archives + "/essai.bin", "wb").write(bytes(h) + data)

# --- BinHex 4 : fork « essai.tar », data = un vraie tar (avec un texte dedans) ---
import tarfile, io
tampon = io.BytesIO()
with tarfile.open(mode="w", fileobj=tampon) as t:
    ti = tarfile.TarInfo("interieur.txt")
    ti.size = 12
    t.addfile(ti, io.BytesIO(b"interieur\n\x00\x00"))
tar = tampon.getvalue()

# macfile : nom, type, createur, data fork seuls (CRC non vérifié ici)
def macfile(nom, ty, creator, data):
    b = bytearray()
    b.append(len(nom))
    b += nom
    b += ty
    b += creator
    b += b"\0\0"
    b += len(data).to_bytes(4, "big")
    b += (0).to_bytes(4, "big")
    b += b"\0\0"            # CRC du data fork
    b += data
    return bytes(b)

mf = macfile(b"essai.tar", b"TARF", b"unsi", tar)

# encode BinHex 4 : 6 bits par caractère, RLE jamais déclenché (pas de 0x90)
BX = b"!\"#$%&'()*+,-0123456789@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr"
assert 0x90 not in mf
acc = 0
bits = 0
chiffres = bytearray()
for octet in mf:
    acc = (acc << 8) | octet
    bits += 8
    while bits >= 6:
        bits -= 6
        chiffres.append(BX[(acc >> bits) & 63])
if bits > 0:
    chiffres.append(BX[(acc << (6 - bits)) & 63])
corps = bytearray()
corps.extend(b"(This file must be converted with BinHex 4.0)\n:\n")
for i in range(0, len(chiffres), 64):
    corps.extend(chiffres[i:i + 64])
    corps.extend(b"\n")
corps.extend(b":\n")
open("essai.hqx", "wb").write(bytes(corps))
PY
)

echo "archives prêtes"
ls -la "$TMP/archives"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp -noreset \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
EXP=
cleanup() {
    [ -n "$EXP" ] && kill "$EXP" 2>/dev/null || true
    [ -n "$WM" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1)); [ $i -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
D=:$(cat "$TMP/display")
export DISPLAY="$D"
export HOME="$TMP/home"

fail() { echo "ÉCHEC: $*" >&2; exit 1; }

./build/platine-wm >"$OUT/log/wm.log" 2>&1 &
WM=$!
sleep 0.5

# 1) un tar.gz posé sur l'Expander -> dossier « mon » à côté de l'archive
PLATINE_DOCUMENT="$TMP/archives/mon.tar.gz" ./build/platine-expander \
    >"$OUT/log/expander.log" 2>&1 &
EXP=$!
xdotool search --sync --onlyvisible --name '^StuffIt Expander$' >/dev/null
sleep 1.2
import -window root "$OUT/essaie-1.png"
[ -f "$TMP/archives/mon/un.txt" ] || fail "un.txt n'a pas été extrait"
[ -f "$TMP/archives/mon/deux.txt" ] || fail "deux.txt n'a pas été extrait"
kill $EXP 2>/dev/null; wait $EXP 2>/dev/null || true
EXP=

# 2) le .bin décodé (MacBinary III) — la nouvelle copie va dans la précédente
PLATINE_DOCUMENT="$TMP/archives/essai.bin" ./build/platine-expander \
    >"$OUT/log/expander2.log" 2>&1 &
EXP=$!
sleep 1.0
[ -f "$TMP/archives/essai.txt" ] || fail "essai.txt n'a pas été écrit depuis le MacBinary"
kill $EXP 2>/dev/null; wait $EXP 2>/dev/null || true
EXP=

# 3) le .hqx décodé, « essai.tar » contenu puis expandé à son tour (comme
#    l'Expander d'origine), les flèches et le pôle de barbier en fenêtre
PLATINE_DOCUMENT="$TMP/archives/essai.hqx" ./build/platine-expander \
    >"$OUT/log/expander3.log" 2>&1 &
EXP=$!
xdotool search --sync --onlyvisible --name '^StuffIt Expander$' >/dev/null
sleep 1.4
import -window root "$OUT/expander-3.png"
[ -f "$TMP/archives/essai/interieur.txt" ] ||
    fail "l'archive du .hqx n'a pas été posée puis expandée"
x=$(cat "$TMP/archives/essai/interieur.txt" 2>/dev/null || true)
[ "$x" = "interieur" ] || fail "le contenu du tar emboîté est abîmé"

kill $EXP 2>/dev/null; wait $EXP 2>/dev/null || true
EXP=

# 4) DropStuff : un dossier, un document et l'alias d'un dossier sont compressés
#    (« nom.tar.gz » à côté de l'élément, l'alias suivi jusqu'à son original)
mkdir -p "$TMP/compr/Dossier/sous" "$TMP/compr/orig/Vrai"
printf 'a\n' >"$TMP/compr/Dossier/sous/a.txt"
printf 'lettre\n' >"$TMP/compr/lettre.txt"
printf 'v\n' >"$TMP/compr/orig/Vrai/v.txt"
ln -s "$TMP/compr/orig/Vrai" "$TMP/compr/Alias"
./build/platine-expander "$TMP/compr/Dossier" "$TMP/compr/lettre.txt" "$TMP/compr/Alias" \
    >"$OUT/log/expander4.log" 2>&1 &
EXP=$!
i=0
while [ ! -s "$TMP/compr/Alias.tar.gz" ]; do
    i=$((i + 1)); [ "$i" -lt 60 ] || fail "compression de l'alias absente"
    sleep 0.1
done
sleep 0.3
tar -tzf "$TMP/compr/Dossier.tar.gz" | grep -q '^Dossier/sous/a.txt$' || fail "Dossier.tar.gz incomplet"
tar -tzf "$TMP/compr/lettre.txt.tar.gz" | grep -q '^lettre.txt$' || fail "lettre.txt.tar.gz incomplet"
tar -tzf "$TMP/compr/Alias.tar.gz" | grep -q '^Vrai/v.txt$' || fail "l'alias n'a pas été suivi"
[ -d "$TMP/compr/Dossier" ] && [ -f "$TMP/compr/lettre.txt" ] || fail "les originaux ont disparu"

kill $EXP 2>/dev/null; wait $EXP 2>/dev/null || true
EXP=

# 5) lancé par double-clic sur son icône ou son alias (le Finder passe le programme
#    lui-même dans PLATINE_DOCUMENT) : il s'ouvre, sans se compresser lui-même
mkdir -p "$TMP/lance/Applications" "$TMP/lance/Bureau"
cp build/platine-expander "$TMP/lance/Applications/StuffIt Expander"
ln -s "../Applications/StuffIt Expander" "$TMP/lance/Bureau/StuffIt Expander"
PLATINE_DOCUMENT="$TMP/lance/Bureau/StuffIt Expander" ./build/platine-expander \
    >"$OUT/log/expander5.log" 2>&1 &
EXP=$!
xdotool search --sync --onlyvisible --name '^StuffIt Expander$' >/dev/null
sleep 1.5
[ -z "$(ls "$TMP/lance/Bureau" "$TMP/lance/Applications" | grep -i 'tar.gz')" ] ||
    fail "l'Expander lancé par son alias s'est compressé lui-même"
[ -z "$(xdotool search --onlyvisible --name '^Compression$' 2>/dev/null)" ] ||
    fail "fenêtre Compression ouverte au simple lancement"

echo 'Expander : tar.gz, MacBinary, BinHex ➔ tar emboîté, instance unique, compression, lancement par alias : OK'
