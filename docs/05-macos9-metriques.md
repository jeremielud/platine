# Inspection Mac OS 9.2.1 — métriques et écarts (2026-11-02)

Inspection de la VM `~/Projets/eeepc-openbsd/macos9` (QEMU `mac99`, Mac OS 9.2.1,
800×600) pour corriger l'apparence de Platine. Captures de référence :
`audit/macos9-ref/`.

## Barre de menus

- Hauteur **20 px** (y=0..19) ; 1 px blanc en haut, 1 px noir en bas.
- Fond **#dddddd**.
- Logo **16×16** à x≈3 ; premier titre à x≈30. Platine dessine la Pomme des assets
  (17×17 à x = 14), Tux 11×15 à x = 18 en repli.
- Titres en **Charcoal 12 px gras** ; padding horizontal ≈5 px ; espacement
  entre titres plus large que dans notre ancienne version.
- Horloge **gras** à droite, puis séparateur gravé, puis icône d'application
  **16×16** et nom de l'application en gras.
- Le menu de l'application est celui du premier plan (Finder : File/Edit/View/
  Window/Special/Help).

## Barre de titre

- Hauteur **21 px** (et non 20).
- Fond **#cccccc**.
- **Rayures** actives : alternance 1 px **#ffffff** / 1 px **#777777**, de
  y+4 à y+h−5 ; pas de rayures sur les zones des cases ni derrière le titre.
- **Case Fermer** : 11×11 à x+4, centrée verticalement ; bord noir, biseau
  blanc/#777, intérieur gris.
- **Case Zoom** : 11×11 à x+w−30, petit carré noir à l'intérieur.
- **Case Repli** : 11×11 à x+w−15, trois traits horizontaux. Elle existe !
  Ce n'est pas seulement le double-clic sur le titre.
- **Plaque de titre** : rectangle #ccc sans rayures, largeur = icône 16×16 +
  texte + 12 px, centrée. L'icône (dossier, document, application) est à gauche
  du titre. Titre en **Charcoal 12 gras**.
- Barre inactive : pas de rayures, titre gris, cases atténuées.
- Bas de barre : 1 px #999 puis 1 px noir ; contenu blanc avec bord noir.

## Fenêtres Finder

- Contenu **blanc**, pas #eee.
- Ligne d'état centrée en haut : « N éléments, X Go disponibles ».
- Icônes **32×32** réelles (dossier lavande, document, disque, corbeille),
  étiquettes **Geneva 10**.
- Grille **128×64** (relevé définitif du 2026-11-03 ; l'ancienne estimation 84×70 était fausse).
- Sur le bureau, les étiquettes ont un **fond blanc** pour rester lisibles sur
  le motif ; dans une fenêtre, pas de fond.
- Les alias sont en **italique**.
- Sélection : étiquette sur fond bleu-violet, texte blanc, encadré autour de
  l'icône.
- **Barre de défilement 16 px** à droite : flèches en haut et en bas, cuvette
  grise biseautée, ascenseur lavande avec stries. Elle s'arrête au-dessus de la
  poignée de redimensionnement (13 px).
- **Poignée de redimensionnement** 13×13 en bas à droite, lignes diagonales.

## Menus déroulants

- Fond **#ddd**, bordure **noir/blanc/noir/blanc** (4 px).
- Hauteur d'item **16 px**, texte **Charcoal 12 gras**.
- Icônes 16×16 dans le menu Pomme ; colonne d'icône puis texte à x≈26.
- Séparateur : 1 px noir + 1 px blanc.
- Raccourcis alignés à droite, en gris ; le symbole **⌘** doit venir d'une
  police qui le contient (nous utilisons DejaVu Sans 9 px, car Charcoal
  converti mappe U+2318 sur « D »).
- Pas d'ombre portée ; la bordure épaisse suffit.

## Bureau

- Motif **`systeme-mac-os-default.png`** (128×128) répété : fond bleu-lavande
  avec les grands motifs Mac. C'est le motif réel de la VM, pas un damier.
- Icônes 32×32 réelles, étiquettes à fond blanc, grille 128×64.
- Le **Control Strip** occupe le bas de l'écran (`platine-strip`, voir
  `07-control-strip.md`).
- Sur le bureau, le disque de démarrage est en haut à droite et la **Corbeille en bas à
  droite** ; Platine fait de même tant que la Corbeille n'a pas été déplacée (D10).

## Écarts corrigés dans Platine

| Avant | Après |
|---|---|
| Damier clair | Motif Mac OS 9 réel, tuilé |
| Icônes procédurales | Icônes 32×32 extraites (dossier, document, disque, corbeille) |
| Étiquettes sans fond | Fond blanc sur le bureau, italique pour les alias |
| Contenu Finder #eee | Contenu blanc + ligne d'état |
| Barre de titre 20 px, cases 12 px, pas de repli | 21 px, cases 11 px, case Repli, plaque, icône |
| Rayures derrière le titre | Plaque #ccc sans rayures |
| Barre de menus régulière et serrée | Gras, padding Mac, Tux 15×18, icône Finder |
| Items de menu 18 px, bord 1 px | 16 px, gras, bord double, icônes Pomme, ⌘ |
| Texte anticrénelé | `antialias=false`, rendu plus proche du bitmap Mac |
| Pas de barre de défilement | Barre Platinum 16 px, ascenseur lavande |
| Pas de barre d'adresse | Barre d'adresse + Précédent/Suivant/Parent + chemins + vues |
| Pas de menus contextuels | Menus contextuels rendus par le WM, style Mac OS 9 |
| Coins carrés | Coins arrondis 8×8, masque exact relevé sur Mac OS 9 |
| Tux 15×18 décalé | Tux **11×15 à x=18, y=2**, bbox colorée identique à l'icône Apple (x=18..28, y=2..16) |
| Corbeille muette | Corbeille pleine/vide, noms d'origine, remise en place, alerte de vidage |
| Défilement clic par clic | Smart Scrolling (maintien = répétition), pouce proportionnel |
| Glisser sans retour | Survol de dossier assombri, curseur « + » en copie, `Échap` annule |

## Ce qui reste à corriger (état au 2026-11-07)

Faits depuis ce relevé : Control Strip natif, sous-menus, coches, navigation clavier,
menus contextuels, alias, type/créateur, colonnes et tri de la vue liste, sons, menu
Pomme alimenté par son dossier, horloge réglable (24 h, secondes) dans Date et heure.

Restent :

1. Animations de zoom et de repli des fenêtres ; ombres et états exacts.
2. États enfoncés de la case, du radio, des petites flèches ; bouton inactif.
3. Poignée de redimensionnement et curseurs exacts (formes et moments).
4. Étiquettes en couleur ; icônes personnalisées.
5. Dynamique des icônes (zooms, retours, surlignages) relevée en détail : voir le chantier
   de peaufinage de la feuille de route.

## Finder spatial

- Géométrie de fenêtre, vue, défilement et positions d'icônes stockés par
  dossier dans `~/.config/platine/finder2`.
- Le WM publie `_PLATINE_GEOMETRY` (x, y, largeur, hauteur) sur la fenêtre
  cliente ; le Finder l'enregistre à chaque déplacement/redimensionnement.
- Les icônes se déplacent à la souris et la position est enregistrée au
  relâchement.
- `Nettoyer` remet chaque icône dans la case libre la plus proche (la sélection
  seule s'il y en a une).

## Disposition des icônes (relevé du 2026-11-07, `audit/macos9-ref/options-presentation-*.png`)

Règles de Mac OS 9.2 reprises par le Finder :

- **Grille** de 128 × 64. En fenêtre par icônes, la première icône est centrée à
  58 px du bord de la fenêtre (57 px du contenu, case à x = 35) ; par boutons, le
  bouton est centré dans sa case (x = 10). Colonnes : tant que l'icône et 40 px
  d'étiquette tiennent (3 colonnes à 400 comme à 480 px).
- **Nouveaux éléments** : première case libre ; en fenêtre rangée par rangée de
  gauche à droite, sur le bureau colonne par colonne depuis le coin haut droit
  (disque de démarrage en tête), de haut en bas puis vers la gauche.
- **Options de présentation** (⌘J, menu Présentation ou menu contextuel du fond),
  par fenêtre et pour le bureau, zone de dialogue de 310 × 352 sur la VM (350 ici,
  libellés français) : *Disposition des icônes* — **Aucune** (l'icône reste où on
  la pose), **Toujours aligner sur la grille** (au dépôt, case libre la plus
  proche ; à l'activation, toutes les icônes s'alignent), **Toujours ranger** +
  menu local (par nom, date de modification, taille, type ; date de création et
  étiquette grisées : Platine ne les connaît pas). *Taille des icônes* : petite
  grisée.
- **Toujours ranger** : l'ordre décide de tout ; une icône déplacée reprend sa
  place, Nettoyer et Réorganiser sont grisés, un nouvel élément s'insère à son
  rang. Sur le bureau, disques d'abord et Corbeille à la fin.
- **⌘ pendant le glisser** inverse l'alignement sous Mac OS 9 ; sur Platine ⌘ et
  Option sont la même touche (qui copie), c'est donc **Ctrl**.
- **Réorganiser ▸ par …** : rangement ponctuel dans l'ordre de la clé.
- Persistance : deux champs de plus par ligne de `finder2/fenetres` (disposition,
  clé) ; le bureau a sa ligne `__bureau__`. Test `run-grille.sh`.
- `Retour` sur une icône sélectionnée ouvre le champ de renommage ; le texte
  existant est sélectionné et remplacé à la première frappe.

## Coins arrondis

Le masque 8×8 relevé sur la VM :

```
TL : #####...   TR : ....####
     ###.....        .....###
     ##......        ......##
     #.......        .......#
     #.......        .......#
BL : #.......   BR : ........
     #.......        ........
     #.......        ........
     #.......        .......#
     #.......        .......#
     ##......        ......##
     ###.....        .....###
     ########        ...#####
```

Quatre fenêtres 8×8 noires, `override_redirect`, forme `ShapeBounding` et
`ShapeInput` vide (les clics passent), relevées après chaque fenêtre/menu.

## Mise à jour 2026-11-04

- Navigation clavier des menus (flèches, Entrée, Échap) réparée : les fenêtres de menu
  sélectionnent maintenant `KeyPressMask` (voir `08-menus.md`).
- Les étiquettes italiques observées sur le 701 venaient d'un binaire installé plus ancien
  que les sources ; le Finder a été recompilé et réinstallé. Seuls les alias sont en
  italique, comme sous Mac OS 9.
- Grille d'icônes confirmée à 128×64 (fenêtre et bureau).
- Les fichiers du dossier « Tableaux de bord » utilisent les icônes 32×32 de Mac OS 9
  (Apparence, Clavier, Diction/Speech, Son, Souris, Écran, Control Strip, Date et heure,
  Économiseur d'énergie) et le dossier a l'icône `dossier-tableaux` ; les panneaux
  **Control Strip**, **Date et heure** et **Économiseur d'énergie** sont ajoutés.
- `assign_icon_positions` réserve d'abord toutes les positions enregistrées avant de
  placer un nouvel élément : plus de superposition quand un tableau est ajouté.
- Icônes de fenêtre alignées à gauche dans la colonne (cellule + 6 px), étiquette toujours
  centrée sous l'icône ; un nom long peut dépasser la colonne (largeur max = fenêtre) et
  n'est pas décalé dans la cellule. Sur le bureau, l'icône occupe le bord gauche de sa
  cellule (x = largeur − 65, soit 735 sur 800 px). Les anciennes positions enregistrées à
  largeur − 113 sont migrées au chargement. Le centrage de l'icône et le décalage des
  étiquettes étaient faux.
- Barre de défilement Finder : **16 px** (trait + 15), pouce lavande, flèches groupées
  en bas/à droite ; les panneaux Date et heure et Économiseur n'ont pas de barre de
  défilement. Dossier « Tableaux de bord » : 480×380, grille 3×3.
- Panneaux Date et heure / Économiseur : onglets et groupes Toolbox ;
  Control Strip : panneau sans curseur de largeur, position/largeur par l'onglet.

## Références

- `audit/macos9-ref/desktop.png` : bureau complet.
- `audit/macos9-ref/finder-active.png` : fenêtre Finder active, scrollbar.
- `audit/macos9-ref/titlebar.png` : barre de titre 4×.
- `audit/macos9-ref/titlebar-buttons.png` : Zoom + Repli 8×.
- `audit/macos9-ref/apple-menu.png` : menu Pomme avec icônes.

## Cadre, menus et sélection (relevés VM)

- **Cadre de fenêtre** : 2 px visibles — anneau extérieur noir en haut/gauche
  et blanc en bas/droite, anneau intérieur inverse — plus une ombre noire de
  2 px à droite et en bas. Le client commence à (2, 21).
- **Menus** : bord noir 1 px + liseré blanc 1 px, face `#ddd` ; items 16 px,
  séparateurs 8 px (`#888` + blanc), surlignage `#333399` texte blanc.
- **Déplacement et redimensionnement** : contour pointillé XOR blanc de **1 px**,
  un pixel sur deux, directement à l'écran ; la vraie fenêtre ne bouge qu'au relâchement.
  Sur le fond `(99,99,156)`, le XOR produit `(156,156,99)`. Pour une fenêtre repliée,
  le contour fait `title_h + 1` de haut et ne conserve donc que la barre de titre.
  Le contour est détruit au repli et au relâchement, sans résidu.
- **Sélection d'icône** : icône assombrie à ~55 %, étiquette fond noir texte
  blanc, pas de rectangle autour de l'icône.
- **Lasso** : rectangle pointillé en XOR (damier 50 %) pendant le glisser.
