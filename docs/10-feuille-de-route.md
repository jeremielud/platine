# Feuille de route des incréments

Chaque grand jalon suit le même cycle : implémentation, tests sur PC, documentation, compilation
i686, déploiement sur le Eee PC 701 et vérification sur la machine cible. Les ressources Apple
restent réservées à l'usage personnel et ne sont jamais publiées.

## Chantiers ouverts (2026-11-07)

Détails dans les jalons plus bas.

1. **Peaufinage massif du comportement profond de Mac OS 9** — dynamique des icônes,
   du curseur et des fichiers ; d'abord les états enfoncés de la Toolbox (D2), relevés sur
   la VM avec `outils/vm9/presse.py`.
2. ~~**VM Mac OS X**~~ — fait le 2026-11-07 : relevé sur Snow Leopard
   (`docs/12-macosx-arborescence.md`), **décision D11** appliquée et déployée sur le 701 :
   Applications, System, Library à la racine, noms anglais montrés en français, BSD masqué,
   migration du dossier personnel. Restent : noms des programmes localisés (comme les
   paquets .app), Bibliothèque à trois niveaux pour les préférences et les polices.
3. **VM Mac OS 9 PowerBook** — Batterie, Trackpad, affichage à l'écran du son et de la
   luminosité : Platine sur les portables.
4. **Sélecteur d'applications** calqué sur Mac OS 9 ; menus réels des applications
   non-Platine.
5. **Réglages et session** (E) — suspension S3, bouton d'alimentation réel, mesure d'un
   démarrage complet.
6. **StuffIt** — `.sit` lu et écrit en interne (sans `unstuff`).
7. Test `theiere` : le lancement de TextEdit par le menu Pomme échoue sous Xvfb.
8. **Control Strip au-dessus des fenêtres** (signalé le 2026-11-07) : sous Mac OS 9, les
   fenêtres passent **sous** le Control Strip ; dans Platine elles passent dessus. À
   corriger dans le WM (pile : le Strip reste au-dessus des fenêtres ordinaires).
9. **Instantané : capture retardée** — un sélecteur de secondes (petites flèches de la
   Toolbox) dans le panneau, compte à rebours visible, puis la capture choisie (écran,
   fenêtre, sélection) ; même choix dans le menu du module Control Strip (« Capturer
   l'écran dans 5 s… » ou sous-menu de délais).

## A — Toolbox : interactions et sons — terminé

Maintien/sortie/rentrée des boutons, contrôles désactivés, sons Platinum, catégories sonores,
alertes du Finder et scénario `run-toolbox.sh`.

## B — Control Strip : actions des modules — terminé

AirPort, Diction et Résolution reliés à leurs actions ; service Diction persistant ; scénario
`run-strip.sh`.

## C — Control Strip : personnalisation et retours — terminé et déployé sur le 701

- raccourci d'affichage configurable depuis le tableau Control Strip, avec rechargement à chaud ;
- bulles de nom et d'état des modules au survol ;
- sons d'appui, de relâchement et de repli cohérents avec la Toolbox ;
- tests Xvfb et validation sur le 701.

## D — Toolbox : contrôles mutualisés — terminé et déployé sur le 701

- contrôle `PTK_SCROLLBAR` horizontal/vertical, partagé avec les listes, les deux axes du Finder
  et l'ascenseur désactivé de « À propos de cet ordinateur » ;
- groupes de boutons radio indépendants, groupe historique 0 préservé ;
- navigation clavier des listes et zones de texte, circulation Tab/Maj+Tab et défilement automatique ;
- suite complète `make test` verte sur PC, captures fonctionnelles relues ;
- validation sur le 701 NetBSD dans `Xvfb :18` : radios, clavier, édition, glissement du pouce et
  Smart Scrolling du Finder ; `platine-demo` fait désormais partie de l'installation NetBSD.

## D2 — Toolbox : peaufinage à l'image du Dialog Manager — en cours (2026-11-07)

- fait : flèches des ascenseurs verticaux remises dans le bon sens (▲ ▼) ;
- fait : contrôles désactivés en fenêtre inactive (DeactivateControl), apparence relevée
  sur Apparence en arrière-plan, état de dessin `Ptk.inactif` consulté par les primitives ;
- fait : `PTK_SEPARATEUR`, `PTK_PETITES_FLECHES` (bitmap relevé, répétition au maintien),
  `PTK_TRIANGLE` (triangle du Finder mutualisé dans `libptk`) ;
- fait : Retour/Entrée → bouton par défaut, Échap et ⌘. → Annuler, clignotement de 8 ticks ;
  boutons d'alerte suivis comme TrackControl ;
- fait : minuteries par échéances (curseur à 32 ticks même souris en mouvement, répétition
  des flèches et du rail d'ascenseur) ; `libptk` sans avertissement en `-Wall -Wextra` ;
- fait (2026-11-07) : barre de progression indéterminée — `ptk_barbier`, les rayures
  diagonales bleues qui défilent vers la droite (`PTK_BARBIER`, trame de 6 px à 90 ms,
  animée par le ptk lui-même, figée dans une fenêtre inactive), posée dans la
  fenêtre d'expansion de l'Expander et dans la Démo Toolbox (`run-toolbox.sh` vérifie
  l'animation et le gel) ;
- fait (2026-11-07) : flèches de recherche — `ptk_recherche`, les trois chevrons bleus
  qui se poursuivent dans un creux de champ (`PTK_RECHERCHE`, cycle de 45 px, chevrons 7×9),
  comme Sherlock ; même animation partagée, même fenêtre d'expansion et Démo ;
- fait (2026-11-07) : bouton biseauté `PTK_BISEAU`, utilisé par Instantané (Écran,
  Fenêtre, Sélection, Menu grisé) ;
- fait (2026-11-07) : **bouton enfoncé et bouton inactif relevés** sur Disk First Aid par
  `outils/vm9/presse.py` (recalage par `ptr.py`, rien n'est appuyé si le pointeur n'est pas
  à 1 px, sortie avant le relâchement) : face `#666`, libellé blanc non décalé, anneaux
  `#44/#55` et `#88/#77` ; coins coupés sur 2 px (`ptk_round_frame`). Animations
  allégées (seuls les contrôles animés repeints, tampon gardé, barbier pavé en une
  requête). Démo Toolbox : Autres contrôles (onglets Curseurs, Boutons, Animation avec
  une copie simulée façon Finder), Ouvrir…, Choisir un dossier… ;
- reste : case, radio et petites flèches enfoncées (il faut une fenêtre qui les porte :
  General Controls), cadence et pas du pôle de barbier (une copie du Finder dans la VM).

## E — Réglages et session — en cours

- fait (2026-11-06, déployé et vérifié sur le 701) : fuseau horaire immédiat
  (`ptk_fuseau_suivre`, `privilege fuseau` → `/etc/localtime`), mise à l'heure par le
  réseau (`privilege ntp`, ~0,8 s), état visible dans le tableau ; la RTC est réécrite
  par le noyau NetBSD à chaque réglage (pas de `hwclock`). Test `run-fuseau.sh` ;
- fait (2026-11-06, déployé sur le 701) : Économiseur réduit à ce qui marche — DPMS
  immédiat, Suspendre, couvercle et bouton d'alimentation via powerd ; Planification
  et veille automatique retirées. **Reste** : valider la suspension S3 sur le 701
  (jamais essayée) avant de proposer une veille automatique ;
- fait (2026-11-06, déployé sur le 701) : Éteindre/Redémarrer passent par le WM
  (`_PLATINE_ARRET`) qui ferme les applications par WM_DELETE_WINDOW puis appelle
  `privilege` ; une application qui reste plus de 30 s interrompt l'arrêt (alerte
  « … quand même ») ; ⌘⌥Échap (Alt+Ctrl+Échap) force l'application active à quitter
  (relance du Finder) ; bouton d'alimentation « Demander » = alerte Mac OS 9
  Redémarrer/Suspendre/Annuler/Éteindre. Nouveau `platine-alerte`, `ptk_alert` à
  4 boutons de largeur variable, Échap = « Annuler ». Test `run-session.sh` (faux doas).
  Redémarrage réel par ce chemin vérifié sur le 701 (Calculette fermée, puis
  `privilege redemarrer`). **Reste** : appui réel sur le bouton d'alimentation ;
- Finder : plus de fenêtre au démarrage par défaut (`finder_fenetre_demarrage 1` pour
  rouvrir le dossier personnel) ; Control Strip : SIGCHLD ignoré (zombies ; remplacé le 2026-11-07 par `ptk_ramasser_enfants`) ;
- fait (2026-11-06) : `outils/mesures-701.sh` relève la session réelle sans y toucher, puis
  utilise un Xvfb et un HOME jetables. Sur le 701 NetBSD : session réelle X + WM + Finder +
  Control Strip + OSD = **41,8 Mio RSS** (objectif ≤ 96 Mio) ; menu Pomme en **21–26 ms**
  (objectif clic → réaction ≤ 50 ms), bureau du Finder en **153 ms**, Calculette en **136 ms**,
  À propos en **623 ms**. **Reste** : mesurer un démarrage complet jusqu'au bureau.

## F — Applications et interopérabilité — en cours

- fait (2026-11-06) : presse-papiers X11 du Finder, publication et lecture de
  `text/uri-list`, repli UTF-8, tests avec un client externe ;
- fait (2026-11-07) : **dossier du bureau** — le bureau montre `~/Bureau`
  (`XDG_DESKTOP_DIR` s'il est donné), invisible dans la fenêtre du disque, avec le
  disque et la Corbeille fixes (Corbeille au coin bas droit) ; dépôts, Nouveau dossier,
  Coller, Corbeille, renommage comme dans une fenêtre ; relu toutes les 2 s ; dépôt
  d'un document sur un programme ou sur son alias (le programme l'ouvre). Alias de départ
  posés à la création. Test `run-finder-bureau.sh` (docs/04-finder-v0.md) ;
- menus réels des applications non-Platine ;
- fait (2026-11-07) : **StuffIt Expander compresse** (DropStuff) tout ce qui n'est pas
  une archive — document, dossier, alias suivi — en « nom.tar.gz » à côté de l'élément ;
  dépôt du bureau sur un programme du bureau réparé (les icônes glissées masquaient la
  cible), colonne de droite du bureau de nouveau atteignable au glisser ; **SIGCHLD** : le
  WM, le Finder, le Control Strip et À propos ne l'ignorent plus (SIG_IGN hérité par
  tous les programmes lancés, dont les waitpid échouaient) mais ramassent leurs enfants
  (`ptk_ramasser_enfants`) ;
- à faire : **peaufinage massif du comportement profond de Mac OS 9** — dynamique des
  icônes (zooms, retours animés, surlignages, ressorts), du curseur (formes, moments),
  des fichiers (copies, alias, types, icônes par type), relevés sur la VM ;
- à faire : **VM Mac OS 9 PowerBook** (G3/G4, Mac OS 9.2.x) pour en relever tout ce qui
  est propre aux portables : tableau de bord Batterie (Économiseur d'énergie des
  portables, état de charge), Trackpad, modules Control Strip de batterie, affichage à
  l'écran du son et de la luminosité ; but : Platine bien à l'aise sur les portables
  (le 701 d'abord) ;
- à faire : **VM Mac OS X** (10.6 au plus, la version importe peu) pour relever son
  arborescence et sa façon de marier le Macintosh à un Unix (`/Applications`, `/System`,
  `/Library`, `/Users`, dossiers BSD masqués par le Finder) ; but : Platine parfaitement
  intégré aux Unix-like, avec Applications et un dossier Système **à la racine du disque**
  (et non plus dans le dossier personnel, cf. décision D9 à réviser) ;
- à faire : **sélecteur d'applications** (menu en haut à droite de la barre de menus) à
  peaufiner en se calquant sur celui de Mac OS 9 (relevé sur la VM : icône et nom de
  l'application active, menu Masquer / Masquer les autres / Tout afficher, liste) ;
- fait (2026-11-07) : glisser-déposer XDND v5 dans les deux sens entre le Finder et les
  clients X11, copie par `text/uri-list`, prise en charge de `XdndTypeList`, conservation des
  sources et indépendance du presse-papiers ; test `run-finder-xdnd.sh` ;
- fait (2026-11-07) : **TextEdit** — documents UTF-8 de 16 Kio, ouverts par le Finder
  (double-clic d'un document texte lance `platine-textedit`), Enregistrer/Nouveau, alerte de
  fermeture « Voulez-vous enregistrer les modifications ? » (Retour = Enregistrer,
  Échap = Annuler). L'Édition des contrôles texte (Couper/Copier/Coller/Tout sélectionner)
  est mutualisée dans la Toolbox, publie et lit `CLIPBOARD` en UTF-8 et une touche de
  modification seule ne désélectionne plus ; `ptk_window_set_close` permet à une application
  de refuser la fermeture (⌘W, WM_DELETE_WINDOW, Quitter, arrêt de session). Tests
  `run-textedit.sh` et `run-toolbox.sh` ;
- fait (2026-11-07) : décision D8 — toute application Platine vit au menu Pomme
  (`pomme[]` du WM) et dans le dossier Applications (lanceur posé par
  `installer-netbsd.sh`) ; appliqué à TextEdit (qui remplace « Texte simple »/mousepad) ;
- fait (2026-11-07) : **Théière** (`platine-theiere`) — théière de l'Utah en
  OpenGL/GLX, modèle et fonctionnement de `glut_teapot.c` (Mark Kilgard/SGI :
  `teapot()` transcrit, 10 patchs Bézier réfléchis par symétrie, données de
  Newell, grille 7 de `glutSolidTeapot`, normales automatiques dS/du × dS/dv
  comme `glEvalMesh2` sous `GL_AUTO_NORMAL`), sans GLUT ni GLU ; éclairage du
  `teapots.c` du Red Book ; rotation propre à 40°/s réglée sur l'horloge,
  glisser libre à la souris, caméra fixe cadrée sur la sphère englobante (la
  taille suit la fenêtre, sans zoom en tournant). Suit la décision D8. Mesuré
  sur le 701 : **i915GM matériel, 60 i/s, 26 % du processeur, 0 % repliée** ;
- fait (2026-11-07) : **Toolbox GL** (`toolbox/ptk_gl.{h,c}`) et règles
  OpenGL de Platine tirées des mesures de la Théière (docs/11-graphisme-gl.md) :
  synchro verticale pour cadence, mouvement à l'horloge, pause hors écran,
  géométrie statique indexée en VBO ARB, budget de sommets ; `vblank_mode=2`
  posé par la session pour les jeux tiers. Test `run-theiere.sh` (GLX, i/s,
  animation, remplissage, 0 tick processeur repliée et reprise, menu Pomme) ;
- fait (2026-11-07) : **arborescence Macintosh** (décision D9) — `Applications/` et
  `Dossier Système/` (Finder, Système, Extensions, Tableaux de bord, Éléments du menu
  Pomme, Préférences, Polices, Éléments de démarrage) sur le volume ; chaque programme y
  est le vrai binaire avec son identité embarquée (section `.platine`) : icône, genre
  « programme », version dans Lire les informations, ouverture des documents par la base
  de bureau, menu Pomme par signature ; dossiers spéciaux avec leur icône ; tableaux de
  bord choisis par leur nom de fichier ; `/opt/platine` réduit au côté BSD. Tests
  `run-identite.sh` ;
- fait (2026-11-07) : **Installateur** façon Installer VISE (`platine-installateur`,
  `outils/faire-installateur.sh`) : programme d'installation autonome, Lisez-moi,
  Installation standard / personnalisée / Désinstallation, lieu, progression et Arrêter,
  alerte de réussite ; « Installation de Théière » posé dans Téléchargements sur le 701.
  Test `run-installateur.sh`. **Reste** : choix « Autre… » du lieu (dialogue de fichiers) ;
  l'expandeur d'archives est StuffIt Expander, plus bas ;
- fait (2026-11-07) : **menu Pomme alimenté par le dossier** `Éléments du menu Pomme`
  (programmes, alias, documents, dossiers en sous-menus), relu à chaque ouverture, alias qui
  ouvrent leur original ; `poser-arborescence.sh` y pose Calculette et les alias Tableaux de
  bord, TextEdit, Théière, Terminal. Test `run-pomme.sh` ;
- fait (2026-11-07) : **contour de redimensionnement sans maillage** — une fenêtre qui se
  redessinait sous le contour XOR gardait des pointillés (bureau, barre de menus, Strip
  compris) : la zone balayée est entièrement repeinte au relâchement ; **Finder** : vue liste
  qui défile horizontalement quand ses colonnes dépassent la fenêtre (l'ascenseur restait
  inactif et la colonne Date coupée), coin sous la poignée enfin peint. Tests
  `run-outline.sh` (rafraîchissement en plein glisser) ;
- fait (2026-11-07) : **Instantané** (`Dossier Système/Extensions/Instantané`), l'outil de
  capture d'écran inspiré de Snapz Pro 2 (installé dans la VM et relevé :
  `audit/macos9-ref/snapz-panneau.png`) : ⌘⇧3 fige l'écran et ouvre le panneau — Écran,
  Fenêtre (désignée sur l'image figée), Menu (grisé : un menu ouvert tient le clavier),
  Sélection (rectangle tracé, dimensions affichées) ; Envoyer vers (dossier Captures ou
  presse-papiers image/png), Couleurs, Échelle, Format (PNG, PPM), Curseur visible, Choisir
  le nom du fichier (zone de dialogue d'enregistrement de la Toolbox) ; réglages dans
  Préférences ; icône propre (appareil photo, `outils/dessiner-icones.py` → `systeme/icones/`,
  convertie par `preparer-assets.sh`) dans le Finder, le menu Pomme, la barre de titre et de
  menus, les alertes et le Control Strip ; menu du module : Capturer l'écran, la fenêtre, la
  sélection, Ouvrir Instantané, Ouvrir le dossier Captures (captures directes sans panneau) ;
  « Envoyer vers : Enregistrer sous… » (zone de dialogue, dossier choisi retenu). Test
  `run-instantane.sh` ;
- fait (2026-11-07) : Installateur remis en page d'après le vrai Installer VISE de Snapz Pro 2
  (description encadrée, groupe « Lieu d'installation », boutons à droite) ; licence
  Refuser/Accepter, destination propre à chaque composant (extensions), programme lancé à
  la fin, case « Ajouter au menu Pomme » (alias posé, retiré à la désinstallation) ;
  « Installation d'Instantané » dans Téléchargements sur le 701 ; **modules Control Strip
  externes** (`Dossier Système/Modules Control Strip`, docs/07-control-strip.md) posés par
  l'Installateur et relus à chaud — Instantané apporte le sien ;
- fait (2026-11-07) : **disposition des icônes de Mac OS 9** dans le Finder (fenêtres et
  bureau) : grille 128 × 64 recalée sur la VM, Options de présentation (⌘J) — aucune,
  toujours aligner sur la grille, toujours ranger par nom/date/taille/type —, Ctrl qui
  inverse l'alignement au dépôt, Nettoyer (case libre la plus proche), Réorganiser ▸,
  bureau rempli en colonnes depuis la droite. Test `run-grille.sh` ;
- fait (2026-11-07) : **Pense-bêtes**, **Key Caps** et **Profileur système**, rangés comme
  sous Mac OS 9.2.1 (relevé sur le disque de la VM) : vrais programmes dans `Dossier
  Système/Éléments du menu Pomme`, comme la Calculette. Pense-bêtes : une fenêtre par note,
  cadre « pense-bete » du WM (bord fin, barre de 12 px, six couleurs), retour à la ligne,
  sélection, presse-papiers, enregistrement automatique (`~/.config/platine/pense-betes`),
  alerte d'enregistrement à la fermeture. Key Caps : clavier AZERTY du 701, libellés lus
  dans la table X (Maj, AltGr, touches mortes encadrées), menu Police. Profileur système :
  six onglets (profil, périphériques et volumes, tableaux de bord, extensions, applications,
  dossiers système), rapport enregistrable. Toolbox : `ptk_copier`/`ptk_coller`,
  `PTK_EV_KEY_UP` et keycode, `PTK_EV_RESIZE` enfin envoyé, seconde colonne de liste
  réglable (`col2`), descripteur surveillé et minuterie (`fd_ext`, `tic`), `en_sortie` ;
- fait (2026-11-07) : **pont QuickTime** — Lecteur QuickTime
  (`Applications/QuickTime/Lecteur QuickTime`, comme QuickTime Player dans Mac OS 9.2.1) :
  fenêtre en métal brossé, boutons et panneau extraits de QuickTime Player 5, barre de
  temps, volume ; décodage par mplayer en mode esclave (`-wid`) ; ouvre films et sons
  depuis le Finder ; alias posé sur le bureau (2026-11-07) ;
- fait (2026-11-07) : **« Autre… » du lieu d'installation** — la Toolbox gagne la zone
  de dialogue « Choisir un dossier » (`ptk_choisir_dossier`, à l'image du Standard File
  dossier de Mac OS 9 : seule la liste des dossiers, double-clic pour entrer, Choisir
  renvoie le dossier affiché ou celui choisi) ; l'Installateur la branche sur le menu
  local du lieu, garde le dossier du volume relatif (« Dossier Système/Extensions ») et
  un chemin hors du volume tel quel (disque monté). Voir aussi la zone de dialogue
  d'ouverture `ptk_ouvrir` (StandardGetFile) posée le même jour pour le Lecteur
  QuickTime ;
- fait (2026-11-07) : **StuffIt Expander** (`platine-expander`,
  `Applications/StuffIt Expander`, recréé d'après l'original d'Aladdin : icône extraite
  du rsrc `ressources/macos9/brut/apps/Stuffit Expander:rsrc`, types reconnus,
  comportement copié) — reçoit une archive par le Finder (double-clic), Fichier ▸
  Ouvrir… ou glisser-déposer XDND sur la fenêtre ; décompresse à côté de l'archive :
  .tar/.tgz/.tar.gz/.tar.bz2/.tbz2/.tar.xz/.txz par le tar de la base, .gz/.bz2/.xz
  par gzip/bzip2/xz, .zip par unzip ou 7z, .7z/.rar par 7z ou unrar, .sit/.sitx par
  unstuff (décodeur absent : alerte « Décompresseur absent », pkgsrc
  archivers/unstuff) ; .hqx (BinHex 4) et .bin (MacBinary III) décodés en C, et
  l'archive qu'ils portent (.sit surtout) est de nouveau expandée comme l'original ;
  les éléments déjà là passent par l'alerte Remplacer | Annuler ; fenêtre
  « Expansion » avec les flèches de recherche et le pôle de barbier de la Toolbox ;
  une seule copie (ptk_instance_unique). Test `run-expander.sh` (tar.gz, MacBinary,
  BinHex ➔ tar emboîté, instance unique) ; `outils/poser-arborescence.sh` le pose
  dans Applications, au menu Pomme et (alias) sur le bureau ; une archive déposée
  sur son icône est expandée (2026-11-07). **Reste** : unstuff absent de pkgsrc à vérifier ;
  .sit en C reste à écrire si unstuff ne vient pas ; le lancement de fichiers
  d'installation par échec reste possible (tar autoportant en place).
