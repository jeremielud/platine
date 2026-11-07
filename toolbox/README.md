# Toolbox de Platine

La Toolbox est la bibliothèque commune de tous les programmes Platine (le WM, le
Finder, les tableaux de bord, TextEdit, Instantané, l'Installateur…), à l'image de la
Toolbox de Mac OS 9 : on y trouve ce qu'un programme Macintosh demandait au système.

| Fichier | Rôle |
| --- | --- |
| `ptk.h`, `ptk.c` | briques de dessin (couleurs, biseaux, boutons, cadres, texte Xft, motifs, icônes, sons Platinum, curseurs, zooms d'ouverture) |
| `ptk_app.h`, `ptk_app.c` | Toolbox applicative : fenêtres, contrôles, menus, événements, alertes, zone de dialogue d'enregistrement, presse-papiers |
| `ptk_gl.h`, `ptk_gl.c` | Toolbox GL pour les programmes OpenGL (fenêtre GLX, cadence, pause hors écran, géométrie) — voir [docs/11-graphisme-gl.md](../docs/11-graphisme-gl.md) |
| `ptk_identite.h`, `ptk_identite.c` | identité embarquée des programmes (section `.platine`) et arborescence du volume : base de bureau, `@chemin|repli`, `#signature|repli` (décision D9) |
| `ptk_sys.h` | commandes propres au système (NetBSD ou Linux), en un seul endroit |

Un programme inclut `ptk_app.h` (ou `ptk.h` seul pour du dessin, `ptk_gl.h` pour OpenGL)
et se lie avec `build/ptk_app.o build/ptk.o build/ptk_identite.o` — `make` les construit
(`PTK`, `PTK_APP`, `PTK_GL` dans le `Makefile`) ; sous NetBSD, `outils/construire-netbsd.sh`.
Chaque programme déclare son identité par `PTK_IDENTITE(...)` (`ptk_identite.h`).

## Toolbox applicative

`toolbox/ptk_app.{h,c}` fournit une API de haut niveau, calquée sur la
Toolbox de Mac OS 9 : une application décrit ses fenêtres, ses contrôles,
ses menus et ses alertes ; le WM garde les cadres, la barre de menus, les
menus et le focus.

## API

```c
PtkApp app;
ptk_app_init(&app, "Mon app", "app");
PtkWindow *w = ptk_window_new(&app, "Titre", x, y, l, h);
ptk_window_set_menu(w, "Fichier|Fermer\t⌘W\nAide|À propos");
ptk_window_set_draw(w, draw, user);
ptk_window_set_event(w, event, user);
ptk_window_set_command(w, command, user);

PtkControl *b = ptk_control_add(w, PTK_BUTTON, 20, 20, 90, 20, "OK");
ptk_control_set_action(b, action, user);
ptk_window_show(w);
ptk_app_run(&app);
```

Contrôles : `PTK_BUTTON`, `PTK_DEFAULT_BUTTON`, `PTK_CHECKBOX`,
`PTK_RADIO`, `PTK_TEXTFIELD`, `PTK_TEXTAREA`, `PTK_STATIC`, `PTK_PROGRESS`,
`PTK_BARBIER` (progression indéterminée, rayures animées), `PTK_RECHERCHE`
(flèches qui se poursuivent, Sherlock), `PTK_POPUP`, `PTK_GROUPE`,
`PTK_CURSEUR`, `PTK_ONGLETS`, `PTK_LISTE`, `PTK_SCROLLBAR`,
`PTK_SEPARATEUR`, `PTK_PETITES_FLECHES`, `PTK_TRIANGLE` et `PTK_BISEAU`.

Relevés sur Mac OS 9.2, pour toutes les applications :

- `PTK_GROUPE` : cadre gravé et titre (`label`), sans titre c'est un filet ;
- `PTK_CURSEUR` : `min`, `max`, `value`, avec une graduation par cran ; `checked` donne le curseur
  plat sans graduations des volumes ; l'action part à chaque cran ;
- `PTK_ONGLETS` : `ptk_control_set_items(c, "A|B|C")`, `value` est l'onglet ouvert. Chaque contrôle
  d'un onglet se déclare par `ptk_control_set_page(c, n)`, `ptk_onglets_afficher(w, c)` met à jour ;
- `PTK_LISTE` : `items` = `"Nom\tType|…"`, `value` = ligne choisie, `defil` = première ligne
  visible ; un `label` `"Nom\tType"` ajoute les en-têtes et les colonnes de la vue liste ;
- `PTK_SCROLLBAR` : `min`/`max` bornent `value`, `defil` est la taille de la page visible et
  `checked = 1` choisit l'horizontale. Flèches, rail et glissement du pouce appellent `action`
  quand la valeur change ; la valeur par défaut de `defil` est 10 ;
- radios : `ptk_control_set_group(c, n)` crée des groupes indépendants dans une même fenêtre.
  Le groupe 0 conserve le comportement historique commun à toute la fenêtre ;
- clavier : Tab et Maj+Tab circulent entre champs texte, listes et zones multilignes. Les listes
  acceptent les flèches, Début/Fin et Page précédente/suivante, avec défilement automatique ;
  les zones multilignes acceptent aussi les flèches verticales en conservant la colonne ;
- `police` choisit la police d'un menu local ou d'une liste ; `draw_apres` dessine par-dessus
  les contrôles, pour les textes posés dans un panneau d'onglets ;
- dans `ptk.h` : `PtkScrollbar`, `ptk_scrollbar_draw`, `ptk_scrollbar_zone`,
  `ptk_scrollbar_thumb` et `ptk_scrollbar_value` partagent le dessin et la géométrie des
  ascenseurs avec les applications qui n'utilisent pas `PtkControl` ;
- dans `ptk.h` : `ptk_zoom_rects` (ouverture et fermeture), `ptk_zoom_retour` (dépôt refusé),
  `PtkFantome` (glisser translucide), `ptk_draw_argb` (icône avec alpha).

- `ptk_control_set_text` / `ptk_control_value` ;
- `ptk_control_set_items` pour un popup (`"A|B|C"`) ;
- `ptk_window_invalidate` redessine le tampon hors écran ;
- `ptk_alert(parent, message, détail, "Annuler|OK")` ;
- `ptk_beep` ;
- zones de dialogue Standard File : `ptk_enregistrer`, `ptk_ouvrir` (StandardGetFile,
  filtrée par types), `ptk_choisir_dossier` ;
- `ptk_instance_unique(app, w, signature, document)` : une seule copie du programme ;
  une seconde copie lui passe son document et la fait passer devant ;
- `ptk_volume`, `ptk_chemin_programme`, `ptk_programme_pour` : le volume (la racine du
  disque, D11), un programme de l'arborescence, le programme qui ouvre une extension ;
- `ptk_nom_affiche(dossier, nom)` : le nom montré d'un dossier du Macintosh (System →
  Système, Desktop → Bureau…) ; `ptk_masque(dossier, nom)` : masqué au Finder (noms en
  point, dossiers BSD de la racine, `/.hidden`) ; `ptk_perso(rel)` et `ptk_bureau()` :
  les dossiers personnels et le bureau. Les zones de dialogue Standard File s'en servent
  (liste, menu des chemins jusqu'au disque, bouton Bureau) ;
- `ptk_fuseau_suivre` : le fuseau horaire suit les réglages à chaud ;
- `ptk_ramasser_enfants()` : SIGCHLD ramassé sans zombie, pour tout processus qui lance
  des programmes (jamais `SIG_IGN`, hérité par les programmes lancés) ;
- les commandes `Fermer` et `Quitter` sont traitées par la Toolbox ;
- le curseur des champs clignote (500 ms) via `select()` dans `ptk_app_run`.

Les menus sont envoyés au WM via `_PLATINE_MENU` ; les popups et menus
contextuels passent par `_PLATINE_CONTEXT` ; les commandes reviennent par
`_PLATINE_COMMAND`. Le WM prend les raccourcis `⌘` déclarés dans le menu de
la fenêtre active et renvoie la commande correspondante (⌘W ferme, ⌘Q
quitte, ⌘N nouveau dossier, ⌘I informations, etc.).

## Métriques relevées sur la VM Mac OS 9.2

| Élément | Mesure |
| --- | --- |
| Bouton | bord noir 1 px, coins coupés en diagonale sur 2 px (point en (1, 1)), face `#ddd`, liseré blanc à 2 px en haut/gauche, `#aaa` + `#777` en bas/droite, `#bbb` aux coins intérieurs, libellé Charcoal 12 gras centré |
| Bouton enfoncé | face `#666`, anneaux `#44` puis `#55` en haut/gauche, `#88` puis `#77` en bas/droite, **libellé blanc, non décalé** (`audit/macos9-ref/presse/verify-enfonce.png`) |
| Bouton inactif | cadre `#888` aux coins coupés, face `#ddd` unie, libellé `#777` (Repair et Stop de Disk First Aid) |
| Bouton par défaut | anneau **noir 1 px**, coins arrondis rayon 3, à 3 px du bouton ; corridor de 2 px : `#ddd` puis `#aaa` en haut/gauche, `#aaa` puis `#777` en bas/droite (relevé sur `restart-alert.png`) |
| Champ texte | ombre `#888` extérieure en haut/gauche, bord noir 1 px, liseré blanc intérieur, fond blanc ; focus = anneau `#6666cc` 2 px |
| Case à cocher | bitmap 12×12 exact de l'Appearance Manager (`find-dialog.png`), rendu par la palette de gris `aa[16]` : bord noir, liseré blanc, face `#ddd`, ombre `#888`, coche anti-aliasée |
| Bouton radio | bitmap 12×12 exact : anneau noir, anneau gris anti-aliasé, pastille 6×6, croissant blanc en bas/droite |
| Cases de fenêtre | bitmaps 11×11 exacts (`finder-active.png`) : bord `#222`, dégradé diagonal `#999`→`#fff`, ombre `#888` ; zoom = petit carré 6×6, repli = deux barres |
| Popup | bord noir, liseré blanc, face `#ddd`, pomme de commande 14 px : deux triangles bases face à face |
| Progression | bord noir, liseré blanc, remplissage `#6666cc` avec ligne claire en haut |
| Alerte | fond `#ddd`, bord noir, icône 32×32 à (20,24), texte gras à x=64, boutons 82×22 en bas à droite |

Références : `audit/macos9-ref/find-dialog.png` (Sherlock 2 : boutons,
radios, case, champ, popup), `audit/macos9-ref/get-info.png` (champ, popup,
libellés), `audit/macos9-ref/about6.png` (barre de progression),
`audit/macos9-ref/restart-alert.png` (anneau du bouton par défaut),
`audit/macos9-ref/finder-active.png` (cases de barre de titre).

## Applications

- `platine-demo` (`src/apps/demo/demo.c`) : tous les contrôles, popup,
  édition de texte, menus, alerte.
- `platine-info` (`src/apps/info/info.c`) : fenêtre « Lire les
  informations » 342×378, icône 32×32, champ Nom, popup Générales,
  libellés/valeurs, popup Étiquette, zone Commentaires multiligne.
  Le Finder la lance par `⌘I` ou le menu contextuel ; les commentaires et
  étiquettes sont partagés avec `finder2/`.
- `platine-textedit` (`src/apps/textedit/textedit.c`) : éditeur de texte
  simple, documents UTF-8 jusqu'à 16 Kio, ouverts par le Finder ou en
  argument ; Enregistrer, Nouveau, alerte de fermeture. L'Édition vient de
  la Toolbox (voir plus bas).

## Polices

Les polices tirées de Mac OS 9 (propriété d'Apple, usage personnel : `NOTICE.md`) sont
de deux sortes : les bitmaps BDF de Charcoal et Geneva aux tailles du système
(`assets/polices/*.bdf`), utilisées par défaut, et les TTF de `systeme/polices/`
(Charcoal, Chicago, Geneva, Monaco, Capitals, Gadget, Sand, Techno, Textile), que
l'installation pose dans `/usr/X11R7/lib/X11/fonts/macos9` avec
`systeme/90-platine-polices.conf` et que choisissent Apparence et le lissage. Sans elles,
Xft retombe sur une sans-serif et les métriques ne sont plus celles de Mac OS 9.

## Démonstration

```sh
make
./tests/apercu.sh start          # bureau complet dans Xephyr, Démo Toolbox au menu Pomme
# ou, dans un Xvfb ou un Xephyr déjà lancé :
DISPLAY=:1 build/platine-wm & DISPLAY=:1 build/platine-demo &
```

Captures : `docs/img/toolbox-demo.png`, `docs/img/toolbox-alert.png`,
`docs/img/toolbox-701.png`, `docs/img/info-701.png`.

## Limites

- les popups utilisent le menu du WM (pas de sous-menus) ;
- états non relevés : case et radio enfoncées (dessinées comme relâchées), petites
  flèches enfoncées (reprises de la zone des flèches du menu local ouvert) ; barre de
  progression et sélection de liste inchangées en fenêtre inactive ; vitesse et pas du
  pôle de barbier (non mesurés : 1 px toutes les 90 ms). Le bouton enfoncé et le bouton
  inactif sont relevés depuis le 2026-11-07 avec `outils/vm9/presse.py` ;
- l'icône d'alerte vient de `icones/<app>-32.xpm` si elle existe ;
- la répétition au maintien spécifique au Smart Scrolling reste pilotée par le Finder.

## Mise à jour 2026-11-04

- `ptk_button` prend une couleur de texte (`PtkColor text`) : un bouton désactivé
  (`enabled = 0`) est dessiné avec `p->text_dim` au lieu d'un texte noir trompeur.
- `PTK_POPUP` ouvre un menu via le WM (`_PLATINE_CONTEXT`) : il n'a **pas** de barre
  de défilement et reste un menu, pas une liste.
- Date et heure : `PTK_ONGLETS` (3 pages) + `PTK_GROUPE`, `PTK_TEXTFIELD`,
  `PTK_BUTTON`, `PTK_CHECKBOX`, `PTK_POPUP`.
- Économiseur : `PTK_ONGLETS` (Veille / Planification / Options) + groupes et
  popups ; les onglets 2 et 3 sont des emplacements réservés.
- Control Strip : groupes, radios et popups ; le curseur de largeur a été retiré
  du panneau (la largeur se règle en glissant l'onglet de la barre).

## Contrôles mutualisés — jalon D (2026-11-05)

- `PTK_SCROLLBAR` fournit les deux orientations, un pouce proportionnel, les flèches groupées,
  les sauts de page et le glissement. La primitive `PtkScrollbar` de `libptk` dessine aussi les
  deux axes du Finder et l'ascenseur désactivé de `platine-about` ; `PTK_LISTE` l'utilise pour
  son ascenseur vertical. Largeur commune : **16 px** (trait + 15).
- Les radios ont des groupes indépendants et les listes/zones de texte sont navigables au clavier.
- `tests/run-toolbox.sh` vérifie les sélections radio, le focus, les touches de liste, l'édition
  multiligne, les flèches, le rail et le glissement d'un ascenseur horizontal. Les captures sont
  relues, pas seulement comparées. `run-finder-scroll.sh` protège le Smart Scrolling existant.
- La démo est compilée et installée par `outils/construire-netbsd.sh` et
  `outils/installer-netbsd.sh` pour permettre la même validation sur le 701.
- Validation effective sur le 701 NetBSD, dans `Xvfb :18` : deux groupes radio, focus et clavier,
  `Deux` → `Deu`, pouce à la valeur 5 et Smart Scrolling du Finder. Le 701 ne fournit pas
  ImageMagick : les captures de contrôle sont prises avec `xwd` puis converties sur le PC.

## Interactions et sons (2026-11-05)

- Les boutons, cases et radios suivent le pointeur pendant le maintien : sortie = dessin relâché,
  rentrée = dessin enfoncé, relâchement dehors = aucune action. Les contrôles désactivés ignorent
  les clics ; `platine-demo` en contient un pour le vérifier.
- Les contrôles, popups, curseurs et onglets émettent leurs codes Platinum (`btnp`, `btnr`, `popr`,
  `sltp`, `tabp`, etc.). `ptk_beep` et les erreurs du Finder utilisent l'alerte choisie dans le
  panneau Son, indépendamment du thème sonore d'interface.
- Les réglages `son_menus`, `son_fenetres`, `son_controles` et `son_finder` filtrent la lecture sans
  empêcher la journalisation de test. Un dépôt réussi joue `fdrp` ; un dépôt refusé ou annulé ne le
  joue pas.
- `tests/run-toolbox.sh` couvre ces interactions et la sélection d'un popup au clavier ;
  `run-finder-dnd.sh` vérifie aussi les sons de réussite, de collision et l'annulation silencieuse.

## Édition et fermeture confirmée (2026-11-07)

- Les commandes **Couper, Copier, Coller et Tout sélectionner** sont gérées par la Toolbox
  quand le focus est un champ ou une zone de texte, avant d'atteindre l'application — comme
  Mac OS 9 les gère dans le système. Copier publie `CLIPBOARD` en `UTF8_STRING` (et `STRING`
  avec les mêmes octets, comme le Finder) ; Coller demande `UTF8_STRING` et insère au caret,
  ou remplace la pseudo-sélection. Un collage sans propriétaire appelle `ptk_beep`.
  Le Finder copié/collé reste compatible : un chemin copié dans le Finder se colle comme
  texte du chemin.
- Une touche de modification seule (⌘ maintenue avant un raccourci) ne désélectionne plus le
  texte : la sélection ne part qu'avec une vraie frappe, comme sous Mac OS 9.
- `ptk_window_set_close` installe une confirmation de fermeture : retour 0 et la fenêtre
  reste ouverte. Elle est consultée par ⌘W, par `WM_DELETE_WINDOW` (donc par l'arrêt de la
  session du WM) et par `ptk_app_quit`. `platine-textedit` l'utilise pour l'alerte
  « Voulez-vous enregistrer les modifications ? » avec **Ne pas enregistrer | Annuler |
  Enregistrer** — Retour = Enregistrer, Échap = Annuler, comme l'original.
- `tests/run-textedit.sh` vérifie le chargement, la publication vers un client X11, le
  collage externe, l'enregistrement, les trois réponses de l'alerte, l'ouverture par
  double-clic depuis le Finder (le Finder lance `platine-textedit` pour les documents
  texte, plus besoin de `mousepad`) et la commande Nouveau.

## Toolbox GL (2026-11-07)

Les programmes OpenGL (Théière, jeux) ont leur propre Toolbox,
`toolbox/ptk_gl.{h,c}`, sans Xft : fenêtre GLX avec nom, icône et menus
dans la barre du WM, synchro verticale, boucle `while (ptk_gl_image(&g))` qui
fournit `t` et `dt` et se met en pause hors écran, `ptk_gl_echanger` (i/s au
titre), géométrie statique `ptk_gl_maillage`/`ptk_gl_dessiner`. Règles et
mesures : [11-graphisme-gl.md](../docs/11-graphisme-gl.md).

## Peaufinage, à l'image du Dialog Manager (2026-11-07)

Relevés sur la VM Mac OS 9.2.1, tableau Apparence > Polices, au premier plan
puis en arrière-plan.

- **Ascenseurs verticaux** : les flèches étaient couchées (▶ au lieu de ▼) —
  `sb_rect` échangeait déjà les axes, la flèche les échangeait une seconde fois.
  Le même triangle sert maintenant aux deux orientations, comme sur
  `finder-active.png` (▲ puis ▼ groupées en bas).
- **Fenêtre inactive** (DeactivateControl) : tous les contrôles d'une fenêtre en
  arrière-plan, et tout contrôle désactivé, prennent l'apparence inactive. La
  Toolbox positionne `Ptk.inactif` avant de dessiner chaque contrôle, comme un
  état du GrafPort, et les primitives le consultent :

  | Élément | Actif | Inactif |
  | --- | --- | --- |
  | libellés (cases, radios, statiques, groupes, triangle) | `#000` | `#777` (`text_inactif`) |
  | séparateur, cadre de groupe | `#888` / `#fff` | `#bbb` / `#ddd` |
  | champ | ombre `#888`, bord noir, anneau de focus | ombre `#ddd`, bord `#555`, ni anneau ni curseur ; texte noir |
  | menu local | cadre noir, biseaux, texte noir | cadre `#888` aux coins coupés, face `#ddd` unie, séparateur `#aaa`, flèches `#888`, texte `#777` |
  | case, radio | bitmaps relevés | noir → `#888`, reste → `#ddd`, sans anticrénelage |
  | petites flèches | biseau blanc / `#aaa` / `#777` | contour et triangles `#888`, face `#ddd` |
  | bouton, bouton par défaut | cadre et anneau noirs | cadre et anneau `#888`, face unie, texte `#777` |
  | liste, ascenseurs | cadre noir, ascenseur actif | cadre `#888`, ascenseur vide |

  FocusIn et FocusOut redessinent la fenêtre ; une répétition en cours s'arrête
  quand la fenêtre passe derrière.
- **Nouveaux contrôles** : `PTK_SEPARATEUR` (trait gravé, `ptk_separateur`) ;
  `PTK_PETITES_FLECHES` (22×20, bitmap relevé pixel par pixel, `value` entre
  `min` et `max`, moitié haute/basse, sons `laup`/`laur`/`ladp`/`ladr`, répétition
  au maintien) ; `PTK_TRIANGLE` (triangle de détail du Finder remonté dans
  `libptk` sous le nom `ptk_triangle`, `checked` = déplié, sons `dscp`/`dscr`).
  Le Finder dessine sa vue liste avec le même `ptk_triangle`.
- **Clavier du Dialog Manager** : Retour et Entrée valent le bouton par défaut
  (sauf dans une zone de texte, ou dans un champ qui a sa propre action), Échap
  et ⌘. valent le bouton Annuler. Le bouton s'enfonce 8 ticks (133 ms) puis agit,
  sans son — les sons Platinum accompagnent le suivi à la souris. Les alertes
  prennent aussi ⌘. et clignotent de la même façon.
- **Alertes** : leurs boutons s'enfoncent, suivent le pointeur et n'agissent que
  relâchés dessus, comme TrackControl (sons `btnp`/`btnr`).
- **Minuteries de `ptk_app_run`** : échéances au lieu d'un délai de `select`
  remis à zéro à chaque événement. Le curseur texte clignote à 32 ticks
  (GetCaretTime), même quand la souris bouge, et seulement dans la fenêtre
  active. Flèches et rail d'ascenseur, petites flèches répètent au maintien
  (0,3 s puis toutes les 50 ms) ; le rail s'arrête quand le pouce arrive sous le
  pointeur. Sons `sbap`/`sbar`/`sbtp` des ascenseurs.
- Compilation de `libptk` sans avertissement en `-Wall -Wextra` : tables mortes
  des cases de titre retirées, tampons des onglets et en-têtes de liste à la
  taille de leur source, chemins d'assets trop longs refusés au lieu de tronqués.
- `tests/run-toolbox.sh` vérifie les petites flèches (cran, sons, répétition
  jusqu'au maximum), le triangle, Retour/Échap/⌘., la répétition de la flèche
  d'ascenseur, l'orientation des flèches verticales et la désactivation des
  contrôles quand une autre application passe devant. La démo journalise son
  statut sur la sortie standard pour ces vérifications.

## Zone de dialogue d'enregistrement et nouveaux contrôles (2026-11-07)

- **`ptk_enregistrer(app, invite, nom, dossier, chemin, n)`** : la zone de dialogue
  d'enregistrement de Mac OS 9 (Standard File), relevée sur la VM (SimpleText, ⌘S ;
  `audit/macos9-ref/enregistrer-standard-file.png`). Boîte modale de 356×250 sans barre de
  titre (fenêtre hors gestion qui dessine son cadre : trait noir, liseré `#bbb`, `#999`/`#555`,
  ombre de 1 px), fond blanc ; menu local du dossier pour remonter jusqu'au volume, disque
  « Eee PC 701 » ; liste du dossier (dossiers ouvrables, documents et programmes grisés) ;
  Éjecter (inactif), Bureau, Nouveau… (boîte « Nom du nouveau dossier »), Annuler,
  Enregistrer — qui devient Ouvrir sur une rangée de dossier ; invite et nom sélectionné.
  Double-clic sur un dossier, Tab entre le nom et la liste, « Remplacer « x » existant ? ».
  Essayé par la démo (Fichier > Enregistrer…) et par Instantané ; test `run-enregistrer.sh`.
- **`ptk_app_run_modal(app, &fini)`** : boucle d'une fenêtre modale, minuteries comprises.
- **`PtkApp.hors`** : rappel des événements qui ne visent aucune fenêtre de l'application
  (touche saisie sur la racine, fenêtre X propre, demandes de presse-papiers).
- **Listes** : `« \x02icône\x02Nom »` (icône 16×16 des menus) et `« ~Nom »` (grisé, non
  sélectionnable) ; `double_clic` vrai pendant l'action d'un double-clic ; sélection par
  frappe du début du nom ; jusqu'à 8 Kio d'éléments.
- **`PTK_BISEAU`** : bouton biseauté de l'Appearance Manager (cadre noir arrondi, lumière
  blanche, ombre `#aaa`/`#777`, enfoncé inversé, sons `bevp`/`bevr`), contenu dessiné par
  `c->contenu`.

## Bouton relevé, animations allégées, Démo Toolbox (2026-11-07)

- **Bouton** relevé sur Disk First Aid (Mac OS 9.2.1) : enfoncé (`presse.py` maintient
  le clic sur Verify puis sort avant de relâcher), inactif (Repair, Stop) ; voir le
  tableau des métriques. `ptk_round_frame` coupe désormais ses coins en diagonale sur
  2 px, comme tous ces boutons.
- **Animations** : une image de pôle de barbier ou de flèches de recherche ne repeint plus
  la fenêtre entière, seulement les contrôles animés, recopiés seuls à l'écran ; le tampon
  hors écran d'une fenêtre est gardé tant qu'elle ne change pas de taille (il était recréé
  à chaque dessin, onze fois par seconde) ; le barbier est peint en une requête X, une
  trame pavée 6 × 6 décalée de la phase (au lieu d'un rectangle par pixel). Aspect et
  cadence inchangés.
- **`ptk_control_set_text`** change aussi le libellé d'un contrôle qui le dessine
  (bouton, case, radio, triangle, groupe) : « Démarrer » peut devenir « Arrêter ».
- **Démo Toolbox** : Fichier ▸ Ouvrir… et Choisir un dossier… (Standard File) ; Démo ▸
  Autres contrôles… (⌘K), une fenêtre à onglets — *Curseurs* (curseur plat des volumes
  et curseur gradué dans leurs groupes), *Boutons* (boutons biseautés, puis chaque
  contrôle actif à côté de son état inactif), *Animation* (une copie simulée comme celle
  du Finder : « Préparation de la copie… » au pôle de barbier, puis la progression et
  les éléments restants ; flèches de recherche ; « Figer les animations »). La fenêtre
  principale ne change pas (ses coordonnées sont celles de `run-toolbox.sh`).
- `tests/run-toolbox.sh` vérifie la face `#666` du bouton enfoncé et la copie simulée
  (préparation, progression, fin).
