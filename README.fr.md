# Platine — un environnement de bureau à l'image de Mac OS 9

**Fabriqué en France.** Platine est un environnement de bureau complet pour un petit
portable ancien : gestionnaire de fenêtres, Finder et applications, écrits en C sur X11
nu, dont l'apparence et le comportement reproduisent Mac OS 9 au pixel près. Tout tourne
en espace utilisateur, sur l'arborescence d'un disque Macintosh. Banc d'essai et cible :
un **ASUS Eee PC 701 4G** (Celeron M 900 MHz, 512 Mo, écran 800×480, SSD 4 Go) sous
**NetBSD 11.1 i386** ; le développement se fait sur un PC Linux.

Cette version française est la documentation de référence ; [`README.md`](README.md) en
est la version anglaise publiée. Lire d'abord [`NOTICE.md`](NOTICE.md) : les assets tirés
de Mac OS 9 (icônes, motif, polices, sons) sont la propriété d'Apple, hébergés ici pour
l'usage strictement personnel de l'auteur.

## Ce que contient Platine

Platine n'est pas un thème, c'est une plate-forme :

| Composant | Rôle |
|---|---|
| `platine-wm` (« Système ») | cadres Platinum (rayures, cases Fermer/Zoom/Repli, contour XOR), focus, pile, zoom, repli ; barre de menus globale de 20 px (menu Pomme alimenté par son dossier, menus de l'application, horloge, sélecteur d'applications) ; menus et menus contextuels pour toutes les applications ; arrêt, redémarrage, Forcer à quitter (⌘⌥Échap) |
| `libptk`, la **Toolbox** (`toolbox/`) | dessin et événements communs : boutons, cases, radios, champs, listes, ascenseurs (Smart Scrolling), curseurs, onglets, menus locaux, petites flèches, triangle, pôle de barbier, flèches de recherche, boutons biseautés ; alertes ; zones de dialogue Ouvrir / Enregistrer / Choisir un dossier (Standard File) ; presse-papiers ; sons Platinum ; Toolbox GL ; identité embarquée des programmes — voir [`toolbox/README.md`](toolbox/README.md) |
| `platine-finder` (« Finder ») | bureau iconique sur un vrai dossier (`~/Bureau`), fenêtres spatiales, vues icônes/boutons/liste, grille et Options de présentation de Mac OS 9, sélection et lasso, glisser-déposer (y compris XDND avec les clients X11) et dépôt d'un document sur un programme, dossiers à ressort, Corbeille freedesktop, alias, Copier/Couper/Coller, type/créateur, Lire les informations |
| `platine-strip` (« Control Strip ») | la barre de Mac OS 9 : modules AirPort, Son, Batterie, Résolution, Diction…, repli, onglet, modules externes posés par l'Installateur |
| `platine-tableau` | les tableaux de bord : Apparence, Clavier, Diction, Son, Souris, Écran, Control Strip, Date et heure, Économiseur d'énergie |

Applications, rangées comme sous Mac OS 9.2.1 :

- **TextEdit** — documents UTF-8, Enregistrer, alerte « Voulez-vous enregistrer les modifications ? » ;
- **Calculette**, **Pense-bêtes**, **Key Caps**, **Profileur système** — au menu Pomme ;
- **Instantané** — capture d'écran façon Snapz Pro 2 (⌘⇧3 : écran, fenêtre, sélection ; dossier Captures ou presse-papiers ; PNG/PPM), avec son module Control Strip ;
- **Lecteur QuickTime** — fenêtre en métal brossé de QuickTime Player 5, décodage par mplayer ;
- **StuffIt Expander** — expanse une archive à côté d'elle-même (tar, gzip, bzip2, xz par la base ; zip, 7z, rar, sit par les décodeurs externes ; BinHex et MacBinary en C) et **compresse tout le reste** comme avec DropStuff (`nom.tar.gz`) ;
- **Théière** — théière de l'Utah en OpenGL sans GLUT ni GLU, 60 i/s pour 26 % du processeur sur l'i915GM ;
- **Installateur** — programmes d'installation autonomes façon Installer VISE (Lisez-moi, licence, installation standard/personnalisée/désinstallation, lieu, « Ajouter au menu Pomme ») ;
- **Lire les informations**, **À propos de cet ordinateur**, **Démo Toolbox**.

## L'arborescence, comme Mac OS X

Le disque « Eee PC 701 » est la racine du système de fichiers, et Platine s'y pose comme
Mac OS X (décision D11, relevé : `docs/12-macosx-arborescence.md`) : **noms anglais sur
le disque, sûrs pour le shell, que le Finder montre en français**, et les dossiers BSD de
la racine masqués. La fenêtre du disque ne montre que quatre dossiers. Chaque programme
y est **le vrai binaire**, avec une identité embarquée (section ELF `.platine` : nom,
signature, icône, version, types de documents) que le Finder lit comme les ressources
d'une application Macintosh (D9).

```
/                                 « Eee PC 701 »
├── Applications/                 Applications : TextEdit, Théière, StuffIt Expander,
│   └── Utilities/                  QuickTime/ ; Utilitaires : Démo Toolbox
├── System/                       Système : Finder, Système (le WM)
│   ├── Extensions/               Control Strip, Instantané, Lire les informations…
│   ├── Control Panels/           Tableaux de bord : un fichier par tableau
│   ├── Apple Menu Items/         Éléments du menu Pomme : le menu Pomme montre ce dossier
│   ├── Startup Items/            Éléments de démarrage : ouverts après le Finder
│   ├── Control Strip Modules/    Modules Control Strip : ajoutés par l'Installateur
│   └── Fonts                     Polices : alias vers les polices de X
├── Library/                      Bibliothèque : Desktop Pictures (Images du bureau)
├── home/                         Utilisateurs
│   └── jeremie/                  Desktop (Bureau, D10), Documents, Downloads
│                                 (Téléchargements), Pictures (Images), Music, Movies,
│                                 Library (Bibliothèque : Preferences)
└── bin, etc, usr, var…           masqués par le Finder
```

Le côté BSD reste dans `/opt/platine` (session, assistant de privilèges, outils,
assets) ; `/etc` et `/usr` ne sont pas touchés, hormis la configuration des polices et
les scripts powerd. Les actions privilégiées (heure, fuseau, arrêt, énergie) passent par
un seul assistant setuid qui n'accepte qu'une liste fermée d'actions.

## Compiler et tester sur le PC

Dépendances : Xlib, Xft, Xpm, fontconfig, zlib ; pour les tests Xvfb, xdotool,
ImageMagick (`import`, `convert`) ; Xephyr pour l'aperçu.

```sh
make                      # tous les programmes dans build/ (make clean le vide)
make test                 # tous les scénarios, dans un Xvfb et un HOME jetables
./tests/run-finder-bureau.sh      # un scénario seul
./tests/apercu.sh start   # le bureau dans une fenêtre Xephyr 800×480 (relancer : apercu.sh relancer)
```

Les assets Mac OS 9 font partie du dépôt (`assets/`, préparés par
`outils/preparer-assets.sh` depuis les ressources extraites) ; sans eux, le bureau
fonctionne avec un habillage neutre.

### Les scénarios de `make test`

| Scénario | Ce qu'il vérifie |
|---|---|
| `fsops`, `fuseau`, `identite` | copie/déplacement récursifs, fuseau à chaud, identité embarquée et base de bureau |
| `xvfb`, `focus`, `outline` | WM et client de test, `WM_TAKE_FOCUS`, contour XOR sans résidu |
| `finder`, `finder-dnd`, `finder-trash` | bureau et nouveau dossier ; glisser-déposer, Option-copie, collisions ; Corbeille, remise en place, vidage |
| `finder-bureau` | dossier du bureau : alias de départ, relecture, dépôt sur un programme, disque immobile, Nouveau dossier |
| `finder-scroll`, `finder-spring`, `finder-liste` | Smart Scrolling, dossiers à ressort, tri de la vue liste |
| `finder-alias`, `finder-coller`, `finder-xdnd`, `finder-types` | alias, Copier/Couper/Coller, XDND avec un client X11, type/créateur |
| `grille`, `pomme` | disposition des icônes de Mac OS 9, menu Pomme alimenté par son dossier |
| `arborescence` | racine du disque : quatre dossiers, BSD masqué, noms anglais montrés en français |
| `reglages`, `toolbox`, `enregistrer` | Apparence et sons, contrôles et clavier du Dialog Manager, Standard File |
| `strip`, `session` | modules du Control Strip ; arrêt, application têtue, Forcer à quitter |
| `textedit`, `theiere`, `installateur`, `instantane`, `expander` | les applications |

`theiere` échoue actuellement sur le lancement de TextEdit par le menu Pomme (écart connu,
antérieur au dossier du bureau).

## Déployer sur le 701

Le 701 n'est pas un environnement de compilation. `outils/installer-netbsd.sh` compile
dans une petite VM NetBSD (`build.img`, `ssh vm-netbsd`), installe le côté BSD dans
`/opt/platine` et pose les programmes dans l'arborescence du volume
(`outils/poser-arborescence.sh`). Une capture de la session réelle se prend avec
`xwd` (le 701 n'a pas ImageMagick).

```sh
./outils/installer-netbsd.sh   # compilation dans vm-netbsd, installation sur le 701
./outils/mesures-701.sh        # mémoire et réactivité, sans toucher à la session :0
./tests/vnc-701.sh start       # aperçu VNC séparé (Xvfb :16), vncviewer 192.168.1.11:5901
```

Mesures du 2026-11-06 sur le 701 : **41,8 Mio de RSS** pour X + WM + Finder + Control
Strip + OSD (budget : 96 Mio), menu Pomme en **21–26 ms** (budget clic → réaction :
50 ms), bureau du Finder en 153 ms, Calculette en 136 ms. Un arbre Alpine Linux i686
reste pris en charge par les mêmes sources (`../eeepc-alpine`).

## Le dépôt

```
platine/
├── Makefile          make → build/, make test, make clean
├── toolbox/          la Toolbox (libptk) : ptk (dessin), ptk_app (contrôles, dialogues),
│                     ptk_gl (OpenGL), ptk_identite (identité, volume), ptk_sys.h (système)
├── src/wm/           le gestionnaire de fenêtres et la barre de menus
├── src/apps/         about, alerte, calculette, demarrage, demo, expander, finder, info,
│                     installateur, instantane, keycaps, osd, pensebetes, profileur,
│                     quicktime, strip, tableau, test, textedit, theiere
├── systeme/          session, Diction, privilèges (netbsd/), polices, curseurs, thèmes,
│                     powerd, contenu et manifestes des programmes d'installation
├── outils/           installation (NetBSD, Alpine), poser-arborescence, faire-installateur,
│                     preparer-assets, publier, mesures, vm9/ (oracle Mac OS 9)
├── tests/            scénarios Xvfb (run-*.sh), aperçu Xephyr, aperçu VNC du 701
├── docs/             contrats, décisions, relevés, feuille de route ; img/public/ : captures publiées
└── assets/           assets tirés de Mac OS 9 — propriété d'Apple, usage personnel (NOTICE.md)
```

Hors dépôt, sur la machine de l'auteur : `ressources/` (extraction de Mac OS 9),
`audit/` (relevés du 701 et captures de la VM de référence), `archive/` (Platine 1,
mlvwm + Tk, archivé), `cd-live/`. La publication passe par `outils/publier.sh` : une
branche orpheline `publication`, recréée depuis `master` à chaque fois, poussée sur
GitHub comme `main`.

## Règles de travail

- UTF-8 partout ; noms français à l'écran, noms ASCII pour les binaires (`platine-*`).
- Un contrat de [`docs/00-essence.md`](docs/00-essence.md) = un scénario de test.
- Toute application est au menu Pomme et dans Applications (D8), en vrai binaire avec son
  identité (D9), à la racine du disque (D11).
- Un dossier du Macintosh a un nom anglais sur le disque et un nom français à l'écran
  (`ptk_nom_affiche`) ; un dossier personnel se trouve par `ptk_perso`, pas par le volume.
- On compile sur le PC, on teste sous Xvfb, puis on déploie et on vérifie sur le 701 ; la
  session réelle n'est touchée qu'au déploiement.
- La VM Mac OS 9.2.1 est l'oracle : un relevé avant chaque imitation (`outils/vm9/`).
- Pas de `SIGCHLD` ignoré : `ptk_ramasser_enfants()` (un `SIG_IGN` serait hérité par les
  programmes lancés).

## Limites actuelles

- Les applications non-Platine reçoivent un menu générique Fichier/Aide.
- ICCCM/EWMH partiels ; pas de multi-écran ; zoom et repli des fenêtres sans animation.
- Le format `.sit` n'est ni lu ni écrit en interne : l'Expander dépend d'`unstuff` pour
  l'expansion et compresse en `.tar.gz`.
- Suspension S3 jamais validée sur le 701 ; pas de veille automatique.
- États enfoncés de la case, du radio et des petites flèches non relevés (dessinés comme
  relâchés).

## Documentation

| Document | Contenu |
|---|---|
| [`docs/00-essence.md`](docs/00-essence.md) | les douze contrats de Mac OS 9 et leur état |
| [`docs/01-etat-des-lieux-701.md`](docs/01-etat-des-lieux-701.md) | audit initial du 701 (2026-11-02, époque Alpine) |
| [`docs/02-decisions.md`](docs/02-decisions.md) | décisions D1 à D11 |
| [`docs/03-phase1-wm.md`](docs/03-phase1-wm.md) | le gestionnaire de fenêtres : protocole de menus, pièges X11, session |
| [`docs/04-finder-v0.md`](docs/04-finder-v0.md) | le Finder |
| [`docs/05-macos9-metriques.md`](docs/05-macos9-metriques.md) | relevés de la VM : barres, menus, fenêtres, grille, coins |
| [`docs/07-control-strip.md`](docs/07-control-strip.md) | le Control Strip et ses modules |
| [`docs/08-menus.md`](docs/08-menus.md) | les menus et les sons |
| [`docs/09-tableaux.md`](docs/09-tableaux.md) | les tableaux de bord et les réglages |
| [`docs/10-feuille-de-route.md`](docs/10-feuille-de-route.md) | la feuille de route |
| [`docs/11-graphisme-gl.md`](docs/11-graphisme-gl.md) | OpenGL sur l'i915GM : mesures et règles |
| [`docs/12-macosx-arborescence.md`](docs/12-macosx-arborescence.md) | relevé de Mac OS X 10.6 : la racine du disque, le masquage, les noms localisés |
| [`toolbox/README.md`](toolbox/README.md) | la Toolbox |
| [`outils/vm9/README.md`](outils/vm9/README.md) | la VM Mac OS 9 de référence |

---

© 2026 Jeremie Lud. Fabriqué en France. « Apple », « Macintosh » et « Mac OS » sont des
marques d'Apple Inc. ; Platine est un projet indépendant, sans lien avec Apple.
