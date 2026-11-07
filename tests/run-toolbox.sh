#!/bin/sh
# Contrôles Toolbox : maintien/sortie/rentrée, contrôle inactif et sons filtrés,
# petites flèches, triangle, clavier du Dialog Manager, répétition, fenêtre inactive.
set -eu
cd "$(dirname "$0")/.."
make build/platine-wm build/platine-demo build/platine-calculette

TMP=$(mktemp -d ${TMPDIR:-/tmp}/platine-toolbox.XXXXXX)
HOME_TEST="$TMP/home"
OUT=${OUT:-/tmp/platine-tests/toolbox}
mkdir -p "$HOME_TEST/.config/platine" "$OUT/log" "$TMP/bin"
cat >"$TMP/bin/aplay" <<'FIN'
#!/bin/sh
printf '%s\n' "$*" >>"$APLAY_LOG"
FIN
chmod +x "$TMP/bin/aplay"
: >"$TMP/aplay.log"
export PATH="$TMP/bin:$PATH" APLAY_LOG="$TMP/aplay.log"
export PLATINE_ASSETS="$PWD/assets" PLATINE_SONS=1
export PLATINE_SONS_JOURNAL="$TMP/sons.log"
: >"$PLATINE_SONS_JOURNAL"

Xvfb -displayfd 3 -screen 0 800x480x24 -nolisten tcp \
    3>"$TMP/display" >"$OUT/log/xvfb.log" 2>&1 &
XPID=$!
WM=
APP=
cleanup() {
    [ -n "${APP:-}" ] && kill "$APP" 2>/dev/null || true
    [ -n "${WM:-}" ] && kill "$WM" 2>/dev/null || true
    kill "$XPID" 2>/dev/null || true
    wait "$APP" 2>/dev/null || true
    wait "$WM" 2>/dev/null || true
    wait "$XPID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

i=0
while [ ! -s "$TMP/display" ]; do
    i=$((i + 1))
    [ "$i" -lt 100 ] || { echo 'Xvfb ne démarre pas' >&2; exit 1; }
    sleep 0.05
done
export DISPLAY=:$(cat "$TMP/display") HOME="$HOME_TEST"
./build/platine-wm >"$OUT/log/wm.log" 2>&1 & WM=$!
sleep 0.4
./build/platine-demo >"$OUT/log/demo.log" 2>&1 & APP=$!
xdotool search --sync --onlyvisible --name '^.*Toolbox$' >/dev/null
sleep 0.5

fail() {
    echo "ÉCHEC: $*" >&2
    echo "HOME conservé: $HOME_TEST" >&2
    exit 1
}
reset_sons() {
    : >"$PLATINE_SONS_JOURNAL"
    : >"$APLAY_LOG"
}
assert_sons() {
    : >"$TMP/attendus"
    [ "$#" -eq 0 ] || printf '%s\n' "$@" >"$TMP/attendus"
    cmp -s "$TMP/attendus" "$PLATINE_SONS_JOURNAL" || {
        echo 'Sons demandés:' >&2
        cat "$PLATINE_SONS_JOURNAL" >&2
        fail "séquence sonore inattendue (attendue: $*)"
    }
    sleep 0.2
    [ "$(wc -l <"$APLAY_LOG")" -eq "$#" ] ||
        fail "nombre d'appels au lecteur inattendu"
}

# Annuler : enfoncé, sorti, rentré puis relâché. Le dessin doit suivre le
# pointeur et seule cette séquence complète déclenche l'action.
import -window root "$OUT/avant.png"
reset_sons
xdotool mousemove 361 434 mousedown 1
sleep 0.2
import -window root "$OUT/bouton-enfonce.png"
xdotool mousemove 361 300
sleep 0.2
import -window root "$OUT/bouton-dehors.png"
xdotool mousemove 361 434
sleep 0.1
xdotool mouseup 1
sleep 0.3
assert_sons btnp btnx btne btnr
# comparaison limitée au bouton Annuler (client 250,332) : le curseur du champ
# clignote à son rythme et l'horloge tourne, le reste de l'écran peut changer
bouton() { convert "$1" -crop 100x30+311+419 +repage "$2"; }
bouton "$OUT/avant.png" "$TMP/b-avant.png"
bouton "$OUT/bouton-enfonce.png" "$TMP/b-enfonce.png"
bouton "$OUT/bouton-dehors.png" "$TMP/b-dehors.png"
N=$(compare -metric AE "$TMP/b-avant.png" "$TMP/b-enfonce.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "le bouton ne change pas de dessin au maintien"
N=$(compare -metric AE "$TMP/b-avant.png" "$TMP/b-dehors.png" null: 2>&1 || true)
[ "$N" -eq 0 ] 2>/dev/null || fail "le bouton reste enfoncé hors de sa zone"
import -window root "$OUT/apres-annuler.png"

# Relâchement dehors : aucun son de release et aucune action.
reset_sons
xdotool mousemove 361 434 mousedown 1 mousemove 361 300 mouseup 1
sleep 0.3
assert_sons btnp btnx

# Un contrôle désactivé ne doit produire ni événement sonore ni lecture.
reset_sons
xdotool mousemove 475 304 click 1
sleep 0.3
assert_sons

reset_sons
xdotool mousemove 86 250 click 1
sleep 0.3
assert_sons chkp chkr
reset_sons
xdotool mousemove 246 226 click 1
sleep 0.3
assert_sons radp radr

# Les radios Auto/Manuel appartiennent à un groupe indépendant de Icônes/Liste.
reset_sons
xdotool mousemove 410 232 click 1
sleep 0.2
assert_sons radp radr
import -window root -crop 110x20+395+220 +repage "$OUT/radio-auto.png"
reset_sons
xdotool mousemove 490 232 click 1
sleep 0.2
assert_sons radp radr
import -window root -crop 110x20+395+220 +repage "$OUT/radio-manuel.png"
N=$(compare -metric AE "$OUT/radio-auto.png" "$OUT/radio-manuel.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "le second groupe radio ne change pas de sélection"
reset_sons
xdotool mousemove 256 232 click 1
sleep 0.2
assert_sons radp radr
import -window root -crop 110x20+395+220 +repage "$OUT/radio-liste-independant.png"
N=$(compare -metric AE "$OUT/radio-manuel.png" "$OUT/radio-liste-independant.png" null: 2>&1 || true)
[ "$N" -eq 0 ] 2>/dev/null || fail "le groupe Vue décoche Auto/Manuel"
reset_sons
xdotool mousemove 90 232 click 1
sleep 0.2
assert_sons radp radr
import -window root -crop 110x20+395+220 +repage "$OUT/radio-icones-independant.png"
N=$(compare -metric AE "$OUT/radio-manuel.png" "$OUT/radio-icones-independant.png" null: 2>&1 || true)
[ "$N" -eq 0 ] 2>/dev/null || fail "Icônes décoche le second groupe radio"

# La liste prend le focus au clic, suit les touches et défile pour garder la
# sélection visible. Tab et Maj+Tab font le tour champ, liste, zone de texte.
xdotool mousemove 100 360 click 1
sleep 0.2
import -window root -crop 200x70+80+348 +repage "$OUT/liste-un.png"
xdotool key End
sleep 0.2
import -window root -crop 200x70+80+348 +repage "$OUT/liste-fin.png"
N=$(compare -metric AE "$OUT/liste-un.png" "$OUT/liste-fin.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "End ne change pas la sélection de la liste"
xdotool key Home Down Page_Down
sleep 0.2
import -window root -crop 200x70+80+348 +repage "$OUT/liste-page.png"
N=$(compare -metric AE "$OUT/liste-fin.png" "$OUT/liste-page.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "Home, Down ou Page_Down ne change pas la liste"
import -window root -crop 200x70+300+348 +repage "$OUT/texte-sans-focus.png"
xdotool key Tab
sleep 0.2
import -window root -crop 200x70+80+348 +repage "$OUT/liste-sans-focus.png"
import -window root -crop 200x70+300+348 +repage "$OUT/texte-focus.png"
N=$(compare -metric AE "$OUT/liste-page.png" "$OUT/liste-sans-focus.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "Tab ne retire pas le focus de la liste"
N=$(compare -metric AE "$OUT/texte-sans-focus.png" "$OUT/texte-focus.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "Tab ne donne pas le focus à la zone de texte"
xdotool key shift+Tab
sleep 0.2
import -window root -crop 200x70+80+348 +repage "$OUT/liste-focus-retour.png"
N=$(compare -metric AE "$OUT/liste-sans-focus.png" "$OUT/liste-focus-retour.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "Maj+Tab ne rend pas le focus à la liste"
xdotool key Tab
xdotool mousemove 350 370 click 1
sleep 0.2
import -window root -crop 200x70+300+348 +repage "$OUT/texte-avant.png"
xdotool key Up BackSpace
sleep 0.2
import -window root -crop 200x70+300+348 +repage "$OUT/texte-deu.png"
N=$(compare -metric AE "$OUT/texte-avant.png" "$OUT/texte-deu.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "Up puis BackSpace ne modifie pas la ligne précédente"
xdotool mousemove 150 112 click 1

# L'ascenseur autonome partage les zones et le pouce proportionnel des listes.
import -window root -crop 200x16+80+420 +repage "$OUT/ascenseur-avant.png"
xdotool mousemove 272 428 click 1
sleep 0.2
import -window root -crop 200x16+80+420 +repage "$OUT/ascenseur-avance.png"
N=$(compare -metric AE "$OUT/ascenseur-avant.png" "$OUT/ascenseur-avance.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "la flèche d'ascenseur ne déplace pas le pouce"
xdotool mousemove 230 428 click 1
sleep 0.2
import -window root -crop 200x16+80+420 +repage "$OUT/ascenseur-page.png"
N=$(compare -metric AE "$OUT/ascenseur-avance.png" "$OUT/ascenseur-page.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "le rail ne fait pas avancer d'une page"
xdotool mousemove 257 428 click 1
sleep 0.2
xdotool mousemove 170 428 mousedown 1 mousemove 240 428 mouseup 1
sleep 0.2
import -window root -crop 200x16+80+420 +repage "$OUT/ascenseur-glisse.png"
N=$(compare -metric AE "$OUT/ascenseur-page.png" "$OUT/ascenseur-glisse.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "le glissement du pouce ne change pas sa position"
import -window root "$OUT/ascenseur-final.png"

# Le popup passe par le WM ; depuis aucune sélection, deux Down puis Return
# choisissent son deuxième élément.
reset_sons
xdotool mousemove 158 192 click 1
sleep 0.2
xdotool key Down Down Return
sleep 0.3
assert_sons popp mnuo mnus popr
import -window root "$OUT/controles.png"

# OK ouvre une alerte : son du bouton, puis alerte choisie indépendamment du
# thème d'interface. Échap choisit le premier bouton, Annuler.
reset_sons
xdotool mousemove 463 434 click 1
sleep 0.3
import -window root "$OUT/alerte.png"
xdotool key Escape
sleep 0.3
assert_sons btnp btnr lerr
grep -Fx -- "-q $PWD/assets/alertes/simplebeep.wav" "$APLAY_LOG" >/dev/null ||
    fail "l'alerte n'utilise pas simplebeep.wav"

statut() { grep '^statut : ' "$OUT/log/demo.log" | tail -1 | sed 's/^statut : //'; }
noirs() { convert "$1" -crop "$2" +repage txt:- | grep -c '#000000' || true; }
blancs() { convert "$1" -crop "$2" +repage txt:- | grep -ci '#FFFFFF' || true; }

# Petites flèches (Apparence > Polices > Taille), client (410,100) : un clic
# en haut monte d'un cran (laup/laur), en bas descend ; maintenue, la flèche
# répète jusqu'au maximum (24).
reset_sons
xdotool mousemove 487 197 click 1
sleep 0.3
[ "$(statut)" = "Taille : 13" ] || fail "petite flèche haute : « $(statut) »"
assert_sons laup laur
xdotool mousemove 487 207 click 1
sleep 0.3
[ "$(statut)" = "Taille : 12" ] || fail "petite flèche basse : « $(statut) »"
xdotool mousemove 487 197 mousedown 1
sleep 0.4
import -window root "$OUT/fleches-enfoncees.png"
sleep 0.8
xdotool mouseup 1
sleep 0.2
[ "$(statut)" = "Taille : 24" ] || fail "la petite flèche maintenue ne répète pas (« $(statut) »)"

# Triangle de détail, client (300,158) : il se déplie (dscp/dscr).
reset_sons
xdotool mousemove 371 256 click 1
sleep 0.3
[ "$(statut)" = "Détails affichés." ] || fail "triangle : « $(statut) »"
assert_sons dscp dscr
import -window root "$OUT/triangle-deplie.png"

# Dialog Manager : sans champ qui la prend, Retour vaut le bouton par défaut
# (OK ouvre l'alerte), Échap et ⌘. valent Annuler ; le clignotement clavier
# est muet.
xdotool mousemove 546 392 click 1
sleep 0.2
reset_sons
xdotool key Return
sleep 0.6
assert_sons lerr
xdotool key Escape
sleep 0.4
[ "$(statut)" = "Alerte affichée." ] || fail "Retour n'a pas activé le bouton par défaut (« $(statut) »)"
reset_sons
xdotool key Escape
sleep 0.4
[ "$(statut)" = "Annulé." ] || fail "Échap n'a pas activé Annuler (« $(statut) »)"
[ ! -s "$PLATINE_SONS_JOURNAL" ] || fail "le clignotement clavier ne doit pas sonner"
xdotool mousemove 371 256 click 1
sleep 0.3
xdotool key alt+period
sleep 0.4
[ "$(statut)" = "Annulé." ] || fail "⌘. n'a pas activé Annuler (« $(statut) »)"

# Ascenseur vertical, client (223,254) : la flèche du bas maintenue répète
# jusqu'au bout (sbap/sbar), et les flèches pointent vers le haut et le bas
# (rangée étroite du triangle en haut pour ▲, en bas pour ▼).
reset_sons
xdotool mousemove 297 408 mousedown 1
sleep 1.0
xdotool mouseup 1
sleep 0.3
[ "$(statut)" = "Position 5 sur 5" ] || fail "la flèche d'ascenseur maintenue ne répète pas (« $(statut) »)"
grep -qx sbap "$PLATINE_SONS_JOURNAL" || fail "pas de son sbap"
import -window root "$OUT/fleches-verticales.png"
[ "$(noirs "$OUT/fleches-verticales.png" 14x1+290+392)" -eq 2 ] &&
    [ "$(noirs "$OUT/fleches-verticales.png" 14x1+290+395)" -eq 8 ] ||
    fail "la flèche du haut ne pointe pas vers le haut"
[ "$(noirs "$OUT/fleches-verticales.png" 14x1+290+410)" -eq 2 ] &&
    [ "$(noirs "$OUT/fleches-verticales.png" 14x1+290+407)" -eq 8 ] ||
    fail "la flèche du bas ne pointe pas vers le bas"

# Les catégories du panneau Son filtrent la lecture, pas la journalisation.
cat >"$HOME_TEST/.config/platine/reglages" <<'FIN'
son_controles 0
son_alerte sosumi
FIN
reset_sons
xdotool mousemove 361 434 click 1
sleep 0.3
printf 'btnp\nbtnr\n' >"$TMP/attendus"
cmp -s "$TMP/attendus" "$PLATINE_SONS_JOURNAL" || fail "les demandes coupées ne sont pas journalisées"
[ ! -s "$APLAY_LOG" ] || fail "un son de contrôle a été joué malgré son_controles 0"
reset_sons
xdotool mousemove 463 434 click 1
sleep 0.3
xdotool key Escape
sleep 0.3
printf 'btnp\nbtnr\nlerr\n' >"$TMP/attendus"
cmp -s "$TMP/attendus" "$PLATINE_SONS_JOURNAL" || fail "séquence avec contrôles coupés inattendue"
[ "$(wc -l <"$APLAY_LOG")" -eq 1 ] || fail "l'alerte devrait rester sonore"
grep -Fx -- "-q $PWD/assets/alertes/sosumi.wav" "$APLAY_LOG" >/dev/null ||
    fail "le choix sosumi n'est pas respecté"

# Fenêtre inactive : […] — petites flèches sans biseau blanc,
# relevé sur Apparence en arrière-plan.
import -window root "$OUT/active.png"
[ "$(blancs "$OUT/active.png" 22x20+476+192)" -gt 0 ] || fail "petites flèches actives sans biseau"
# le pôle de barbier et les flèches animent dans la fenêtre active
import -window root -crop 88x46+522+184 +repage "$OUT/animation-a.png"
sleep 0.4
import -window root -crop 88x46+522+184 +repage "$OUT/animation-b.png"
N=$(compare -metric AE "$OUT/animation-a.png" "$OUT/animation-b.png" null: 2>&1 || true)
[ "$N" -gt 0 ] 2>/dev/null || fail "le pôle de barbier et les flèches ne s'animent pas"
./build/platine-calculette >"$OUT/log/calculette.log" 2>&1 &
CALC=$!
sleep 1.2
import -window root "$OUT/inactive.png"
kill "$CALC" 2>/dev/null || true
[ "$(blancs "$OUT/inactive.png" 22x20+476+192)" -eq 0 ] ||
    fail "les contrôles d'une fenêtre inactive ne sont pas désactivés"
# figés dans une fenêtre inactive : la trame du barbier est la même
import -window root -crop 88x14+522+194 +repage "$OUT/barbier-inactif-a.png"
sleep 0.4
import -window root -crop 88x14+522+194 +repage "$OUT/barbier-inactif-b.png"
N=$(compare -metric AE "$OUT/barbier-inactif-a.png" "$OUT/barbier-inactif-b.png" null: 2>&1 || true)
[ "$N" -eq 0 ] 2>/dev/null || fail "le pôle de barbier anime dans une fenêtre inactive"

# Bouton enfoncé relevé sur Mac OS 9 (Verify de Disk First Aid maintenu) : face #666,
# libellé blanc. La capture a été prise pendant le maintien d'Annuler, plus haut.
F=$(convert "$OUT/bouton-enfonce.png" -crop 1x1+328+434 -format '%[hex:p{0,0}]' info:)
[ "$F" = "666666" ] || fail "face du bouton enfoncé : #$F au lieu de #666666"

# Démo ▸ Autres contrôles (⌘K) : onglets, puis la copie simulée façon Finder —
# préparation au pôle de barbier, progression, fin.
xdotool mousemove 400 90 click 1
sleep 0.4
xdotool key alt+k
timeout 5 xdotool search --sync --onlyvisible --name '^Autres' >/dev/null ||
    fail "Démo ▸ Autres contrôles n'ouvre pas sa fenêtre"
sleep 0.5
import -window root "$OUT/autres-curseurs.png"
xdotool mousemove 340 132 click 1
sleep 0.5
xdotool mousemove 490 218 click 1
sleep 0.8
import -window root "$OUT/autres-preparation.png"
grep -q '^copie : préparation' "$OUT/log/demo.log" || fail "la copie simulée ne démarre pas"
sleep 2
grep -q '^copie : progression' "$OUT/log/demo.log" || fail "la préparation ne passe pas à la progression"
sleep 4.5
import -window root "$OUT/autres-terminee.png"
grep -q '^copie : terminée' "$OUT/log/demo.log" || fail "la copie simulée ne se termine pas"

echo 'Toolbox : contrôles, sons, clavier, répétition, animations, inactivité, bouton enfoncé, autres contrôles : OK'
echo "Captures et journaux: $OUT"
