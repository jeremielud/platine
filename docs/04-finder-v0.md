# Le Finder (`platine-finder`)

Premier Finder natif de Platine le 2026-11-02 (« Finder v0 »), devenu le Finder complet
du bureau. Il est en C/X11 avec `libptk` (`src/apps/finder/finder.c`, `fsops.c`), et
reproduit le Finder de Mac OS 9.2.1 relevé sur la VM (`05-macos9-metriques.md`).

## Architecture

- Un seul processus pour le bureau et toutes les fenêtres de dossier.
- Le bureau est une fenêtre `override_redirect` plein écran, sous les fenêtres gérées par
  le WM ; il compose le motif et les icônes dans un tampon hors écran.
- Les fenêtres de dossier sont des fenêtres X ordinaires gérées par `platine-wm` (cadre,
  focus, zoom, repli). Chacune publie ses menus (`_PLATINE_MENU`) ; le WM les affiche
  quand elle est au premier plan et renvoie les commandes (`_PLATINE_COMMAND`). Le bureau
  au premier plan (`_PLATINE_BUREAU`, posé par le WM) fait décrire aux menus le bureau et
  sa sélection.
- L'état est rangé dans `~/.config/platine/finder2/` : `fenetres` (géométrie, vue,
  défilement, tri, disposition), `icones` (positions par dossier ; le bureau sous
  `__bureau__`), `commentaires`, `types` (type/créateur).
- Le Finder ramasse ses enfants (`ptk_ramasser_enfants`) : les programmes qu'il lance
  n'héritent pas d'un SIGCHLD ignoré.

## Le bureau (décision D10)

- Il montre un vrai dossier, comme le « Desktop Folder » de Mac OS 9 : `XDG_DESKTOP_DIR`
  (`~/.config/user-dirs.dirs`) s'il est donné, sinon `~/Desktop` (D11 ; `~/Bureau` d'une
  ancienne installation reste reconnu), invisible dans la fenêtre de son dossier parent.
- Le disque « Eee PC 701 » (coin haut droit) et la Corbeille (coin bas droit tant qu'on ne
  l'a pas déplacée) sont fixes : ni glissés hors du bureau, ni renommés, ni dupliqués.
- À sa création, le dossier reçoit les alias des anciens dossiers fixes (Documents, Images,
  Applications) ; `outils/poser-arborescence.sh` ajoute ceux du Lecteur QuickTime et de
  StuffIt Expander. Un alias jeté ne revient pas.
- Tout ce qui marche dans une fenêtre marche sur le bureau : dépôt sur le fond (déplacement,
  Option = copie), sortie de la Corbeille vers le bureau, Nouveau dossier, Coller, Dupliquer,
  Créer un alias (celui du disque ou de la Corbeille va dans le dossier du bureau), Placer
  dans la Corbeille, renommage.
- Le dossier est relu toutes les 2 s (un `stat`, et à date égale tant qu'il a changé dans
  les 2 dernières secondes) : ce qu'y pose un autre programme paraît sur le bureau, la
  sélection est gardée ; jamais pendant un glisser ou un renommage.
- Motif de Mac OS 9 tuilé (choisi dans Apparence), étiquettes à fond blanc translucide,
  coins d'écran arrondis 8×8 au-dessus de tout, sans bloquer les clics.

## Le disque (décision D11)

- L'icône « Eee PC 701 » ouvre la racine du disque (`ptk_volume()`), qui ne montre que
  Applications, Bibliothèque, Système et Utilisateurs : les dossiers BSD et ceux de
  `/.hidden` sont masqués (`ptk_masque`).
- Les dossiers du Macintosh ont un nom anglais sur le disque et un nom français à l'écran
  (`ptk_nom_affiche`) : étiquettes, titres de fenêtre, tri, vue liste. Le vrai nom sert aux
  chemins et aux positions enregistrées ; un dossier au nom traduit ne se renomme pas.

## Fenêtres et présentations

- **Finder spatial** : une fenêtre par dossier (rouvrir un dossier ramène sa fenêtre),
  géométrie, présentation, défilement et positions d'icônes persistants.
- Présentations **par icônes** (icône à x + 6 de sa case, étiquette centrée qui peut
  déborder), **par boutons** (ouverture au clic) et **par liste** (colonnes Nom, Date,
  Taille, Type ; tri par en-tête, reclic = inverser ; triangles de détail ; défilement
  horizontal quand les colonnes dépassent).
- Barre d'adresse façon Platine 1 : Précédent, Suivant, Parent, champ éditable (`~`
  accepté), menu des chemins, boutons de présentation ; ligne d'état « N éléments, X Go
  disponibles ».
- Ascenseurs Platinum de 16 px sur les deux axes, Smart Scrolling (maintien = répétition).
- **Disposition des icônes de Mac OS 9** : grille de 128 × 64, Options de présentation
  (⌘J) — Aucune, Toujours aligner sur la grille, Toujours ranger par nom/date/taille/type —,
  Nettoyer, Réorganiser ▸, Ctrl qui inverse l'alignement au dépôt ; règles détaillées dans
  `05-macos9-metriques.md`.

## Sélection, glisser-déposer, Corbeille

- Clic, lasso pointillé XOR pour une sélection multiple, Tout sélectionner (⌘A) ; icône
  sélectionnée assombrie, étiquette noire.
- **Glisser-déposer** : un fantôme translucide suit le pointeur, l'original reste en place ;
  dépôt dans une fenêtre, sur un dossier (qui s'assombrit au survol) ou sur le bureau ;
  **Option** copie (curseur « + ») ; un groupe garde ses écarts ; `Échap` annule. Un dépôt
  refusé (collision, dossier dans lui-même, élément protégé) revient à sa place avec un
  zoom, avant toute modification du disque.
- **Dépôt sur un programme** ou sur l'alias d'un programme : l'icône s'éclaire, le
  programme est lancé avec les chemins déposés, qui restent en place (une archive sur
  StuffIt Expander, un film sur le Lecteur QuickTime).
- **Dossiers à ressort** : maintenu 700 ms sur un dossier pendant un glisser, il s'ouvre.
- **XDND v5** dans les deux sens avec les clients X11 (`text/uri-list`, `XdndTypeList`),
  sources conservées.
- **Corbeille** freedesktop (`~/.local/share/Trash/files` + `.trashinfo`), pleine ou vide,
  noms d'origine affichés, Fichier ▸ Ranger (⌘Y) remet en place, Spécial ▸ Vider la
  Corbeille… avec alerte. Les éléments système (le disque, Applications, System, Library,
  home, le dossier personnel et ses Documents, Desktop, Pictures…) ne peuvent pas être jetés.

## Fichiers et métadonnées

- Copier/Couper/Coller (⌘C ⌘X ⌘V) par le presse-papiers X11 (`text/uri-list`, repli
  UTF-8), conflits de noms assistés (Remplacer / Arrêter).
- Déplacement et copie récursifs (`fsops.c`) : liens symboliques copiés comme liens,
  fichiers spéciaux refusés, jamais d'écrasement, permissions et dates conservées, repli
  copie + suppression en cas de `EXDEV`.
- **Alias** (⌘M) : liens symboliques ou fichiers d'alias Platine, noms en italique, icône
  de l'original (celle du programme pour l'alias d'un programme), alerte « élément
  d'origine introuvable ».
- **Type/créateur** (`finder2/types`), icônes de documents par famille (texte, image, son,
  film, archive, web, script), dossiers spéciaux de l'arborescence avec leur icône,
  programmes avec l'icône de leur identité embarquée.
- **Lire les informations** (⌘I, `platine-info`) : nom, genre, taille, emplacement, dates,
  version d'un programme, commentaires partagés avec le Finder.
- Renommage : `Retour` ouvre le champ, texte sélectionné, `Entrée` valide, `Échap` annule.

## Ouvrir

Double-clic (ou ⌘O) : un dossier ouvre sa fenêtre avec un zoom depuis l'icône ; un
programme se lance (son nom s'annonce dans la barre de menus) ; un document s'ouvre avec
le programme installé qui déclare son extension (base de bureau, `ptk_programme_pour`),
sinon TextEdit pour un texte, `gpicview` pour une image, `mpv` pour un son ou un film,
`mupdf` pour un PDF, et `xdg-open` en dernier recours.

## Menus

Fichier, Édition, Présentation, Aller, Fenêtre, Spécial, Aide, comme le Finder de
Mac OS 9 ; menus contextuels (clic droit ou Ctrl-clic) rendus par le WM. Spécial porte
Vider la Corbeille, Suspendre, Redémarrer, Éteindre (par le WM, qui ferme d'abord les
applications). Lancé sans argument, le Finder n'ouvre que le bureau ; la clé
`finder_fenetre_demarrage 1` des réglages rouvre le dossier personnel au démarrage.

## Tests

`run-finder.sh`, `run-finder-dnd.sh`, `run-finder-trash.sh`, `run-finder-bureau.sh`,
`run-finder-scroll.sh`, `run-finder-spring.sh`, `run-finder-liste.sh`,
`run-finder-alias.sh`, `run-finder-coller.sh`, `run-finder-xdnd.sh`,
`run-finder-types.sh`, `run-grille.sh`, `run-fsops.sh` — tous dans un HOME et un Xvfb
jetables, jamais sur `:0`. Aperçu VNC du 701 sans toucher à la session réelle :
`tests/vnc-701.sh start` (Xvfb `:16`, x11vnc sur le port 5901).

## Limites

- Pas d'icônes personnalisées élément par élément, pas d'étiquettes en couleur.
- XDND limité à la copie de fichiers locaux par `text/uri-list`.
- La Corbeille est unique (pas de Corbeille par volume).
- Le dépôt sur un programme ne vérifie pas que le programme accepte le type déposé (Mac OS 9
  n'éclaire que les programmes qui l'acceptent).

## Historique des corrections

1. **Boucle infinie de troncature UTF-8** (2026-11-02) : `ptk_fit_text` coupe à une
   frontière UTF-8 et n'ajoute l'ellipse qu'une fois ; le Finder ne bloque plus à 100 %.
2. **Rejeu du clic** : `XAllowEvents(ReplayPointer)` avant le redessin de la barre de
   menus, sinon le client ne reçoit plus le `ButtonPress`.
3. **Navigation clavier des menus** (2026-11-04) : les fenêtres de menu sélectionnent
   `KeyPressMask`, sinon les flèches et Entrée n'arrivaient pas sous `XGrabKeyboard`.
4. **Icônes des tableaux de bord** (2026-11-04) : icônes 32×32 de Mac OS 9 extraites avec
   des gris opaques (Souris, Écran, Speech n'étaient plus des silhouettes noires).
5. **Alignement des icônes** (2026-11-04) : icône à gauche de sa colonne, étiquette
   centrée et non rognée à la case ; sur le bureau, image à x = largeur − 65.
6. **Placement des nouveaux éléments** (2026-11-04) : deux passes, réservation des
   positions enregistrées puis placement des nouveaux, plus de superposition.
7. **Disposition « Aucune »** (2026-11-07) : une icône reste où on la pose, sans recalage
   au rechargement.
8. **Dépôt sur une icône du même bureau** (2026-11-07) : au relâchement, les icônes
   glissées, déjà à leur nouvelle place, masquaient la cible ; elles sont maintenant
   ignorées par la recherche de la cible.
9. **Colonne de droite du bureau** (2026-11-07) : le glisser plafonnait x à largeur − 113
   (ancien repère) au lieu de largeur − 65 ; une icône ne pouvait plus y revenir.

## Captures

Les captures publiées sont dans `docs/img/public/` (prises par
`outils/captures-publiques.sh`). Les captures de travail de `docs/img/` datent des
premiers incréments : `finder-bureau.png`, `finder-toolbar.png`, `finder-list.png`,
`finder-buttons.png`, `finder-context.png`, `finder-get-info.png`,
`finder-701-bureau.png`, etc.
