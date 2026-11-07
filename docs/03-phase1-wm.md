# Le gestionnaire de fenêtres — phase 1 et suites (depuis le 2026-11-02)

> Journal du WM : le premier incrément (2026-11-02), ses pièges X11, puis les
> finitions et la session. Les relevés de la VM sont dans `05-macos9-metriques.md`
> et `08-menus.md`, l'état d'ensemble dans `README.fr.md`.

Objectif : remplacer mlvwm par un gestionnaire de fenêtres Platine, avec une
barre de menus globale et une petite boîte à outils, sans toucher à la session
`:0` du 701.

## Composants

- `src/wm/wm.c` : gestionnaire de fenêtres X11.
- `toolbox/ptk.c` : dessin Platinum (couleurs, bevels, boutons, barre de
  titre, texte Xft, motif de bureau).
- `src/apps/test/test.c` : client de démonstration qui publie un modèle de menus
  et dessine des contrôles.
- `tests/run-xvfb.sh` : scénario automatisé sous Xvfb 800×480.

## Ce qui est implémenté

- Reparentage des fenêtres dans un cadre `override_redirect`.
- Barre de titre de 20 px, rayures actives, texte Charcoal, case Fermer, case
  Zoom, poignée de redimensionnement.
- Focus au clic, élévation, `WM_STATE`, `WM_DELETE_WINDOW`.
- Déplacement et redimensionnement à la souris.
- Zoom vers la zone de travail (sous la barre de menus) et restauration.
- Repli/déploiement par double-clic sur la barre de titre.
- Barre de menus globale : logo (la Pomme des assets, Tux en repli), menus de
  l'application, horloge, menu d'application.
- Menus déroulants avec séparateurs, sélection, fermeture par Échap.
- Sélecteur d'application : liste des fenêtres, focus.
- Fond de bureau en motif 8×8, couleur Mac OS 9 (remplacé ensuite par le motif réel
  de Mac OS 9, tuilé par le Finder).

## Protocole de menus

Une application Platine publie trois propriétés UTF-8 sur sa fenêtre :

| Propriété | Contenu |
|---|---|
| `_PLATINE_APP_NAME` | Nom affiché dans le menu d'application |
| `_PLATINE_MENU` | Une ligne par menu : `Titre|Item|Item|-|Item` |
| `_PLATINE_COMMAND` | Commande envoyée par le WM, relue par l'application |

Le WM lit `_PLATINE_MENU` quand la fenêtre prend le focus, dessine les titres,
puis envoie un `ClientMessage` `_PLATINE_COMMAND` avec le libellé de l'item
choisi. L'application lit la propriété et exécute la commande.

Les applications non-Platine reçoivent un menu générique `Fichier|Fermer|Quitter`
et `Aide|À propos` ; `Fermer`/`Quitter` sont traités par le WM.

## Deux pièges X11 rencontrés

### 1. `XSelectInput(ButtonPressMask)` sur la fenêtre d'un autre client

Sur Xvfb, sélectionner `ButtonPressMask` sur une fenêtre créée par un autre
client renvoie `BadAccess` (requête 2, `ChangeWindowAttributes`). Vérifié avec
un programme minimal : dès que le premier client a sélectionné
`ButtonPressMask`, le second ne peut plus le sélectionner.

Solution : grab passif + rejeu, la méthode classique des WM X11.

```c
XGrabButton(dpy, AnyButton, AnyModifier, win, False, ButtonPressMask,
    GrabModeSync, GrabModeAsync, None, None);
```

Au `ButtonPress`, le WM donne le focus puis rejoue l'événement :

```c
XAllowEvents(dpy, ReplayPointer, CurrentTime);
```

Le client reçoit le clic normalement (click-through), et le WM n'a pas besoin de
sélectionner `ButtonPress` sur sa fenêtre.

### 2. La poignée de redimensionnement est recouverte par le client

La poignée est dessinée sur le cadre, mais le client est un enfant qui couvre le
contenu. Un clic dans le coin bas-droit part donc au client, pas au cadre.

Solution : une petite fenêtre enfant `grow` de 13×13, créée après le client et
élevée au-dessus, qui reçoit les clics de redimensionnement. Elle est déplacée
avec le cadre et masquée pendant le repli.

### 3. Alt+Tab

Retiré volontairement : Mac OS 9 n'utilise pas ⌘Tab mais le **menu
d'application** à droite de la barre. Le sélecteur d'application est donc le
mécanisme principal. Un raccourci pourra être ajouté plus tard comme option.

## Mesures

| Élément | RSS |
|---|---:|
| `platine-wm` (701, Xvfb) | 4,4 Mo |
| `platine-finder` (701, Xvfb) | 4,7 Mo |
| Xvfb 800×480 (701) | 54 Mo |
| Xorg réel (701, session Platine 1) | 34,9 Mo |

Le WM est donc très léger ; l'essentiel de la mémoire est le serveur X.

## Captures

- `docs/img/phase1-701-bureau.png` : rendu sur le 701, police Charcoal.
- `docs/img/phase1-701-menu.png` : menu Fichier ouvert.
- `docs/img/phase1-701-focus.png` : focus rendu à xterm.
- `docs/img/phase1-701-redimension.png` : redimensionnement.
- `docs/img/phase1-701-zoom.png` : zoom.
- `docs/img/phase1-701-repli.png` : repli.
- `docs/img/phase1-701-fermeture.png` : fermeture, focus rendu à xterm.
- `docs/img/phase1-701-selecteur.png` : menu d'application.

## Étapes suivantes (état au 2026-11-07)

1. Métriques exactes de la VM — fait (`05-macos9-metriques.md`, `08-menus.md`).
2. Sous-menus, raccourcis, coches, navigation clavier — fait.
3. Icônes d'application réelles dans le menu d'application — fait (identité embarquée).
4. Animations de zoom et de repli des fenêtres — **à faire**.
5. Placement des nouvelles fenêtres, `WM_NORMAL_HINTS` — fait ; ICCCM/EWMH complet — **à faire**.
6. Finder — fait (`04-finder-v0.md`).
7. Menus des applications non-Platine — **à faire**.

## Phase 1b — finitions

- Raccourcis affichés à droite des items (`\t` dans `_PLATINE_MENU`), en police
  petite et grise ; la commande envoyée est le libellé sans le raccourci.
- Navigation clavier dans les menus : Haut/Bas, Début/Fin, Entrée, Échap.
- Icône d'application réelle via `_PLATINE_ICON` (`folder`, `doc`, `disk`,
  `trash`, `app`) et `ptk_icon` procédural.
- Placement des fenêtres : position du client si elle est fournie, sinon
  cascade ; tailles minimales/maximales `WM_NORMAL_HINTS`.
- `ptk_fit_text` : troncature UTF-8 sûre, avec une seule ellipse.

Reste pour plus tard : animations de zoom et de repli, ICCCM/EWMH complet.
`WM_TAKE_FOCUS` est envoyé aux clients qui l'annoncent dans `WM_PROTOCOLS` ;
les autres conservent le focus direct par `XSetInputFocus`. Le scénario
`tests/run-focus.sh` vérifie le message, le focus X11 et la réception du clavier.

## Session : arrêt et Forcer à quitter (2026-11-06)

- `_PLATINE_ARRET` (ClientMessage sur la racine) : `data.l[0]` = 1 éteindre, 2 redémarrer,
  0 annuler ; `data.l[1]` = 1 pour forcer. Le Finder l'envoie (Spécial ▸ Éteindre /
  Redémarrer, après `save_state()`), comme `platine-alerte`.
- Le WM envoie WM_DELETE_WINDOW (ou XKillClient sans ce protocole) à toutes les fenêtres
  qui ne sont pas au Finder, puis lance `SYS_PRIVILEGE arret|redemarrer` dès qu'il ne reste
  que le Finder. Au bout de 30 s (`PLATINE_ARRET_DELAI` pour les tests), l'arrêt est
  interrompu et `platine-alerte arret-bloque` propose « Annuler » ou « … quand même »
  (SIGKILL par `_NET_WM_PID` + XKillClient, puis arrêt après 3 s au plus).
- ⌘⌥Échap = Alt+Ctrl+Échap, saisi sur la racine (avec Verr. Maj/Num) : le WM rend le
  clavier aussitôt et lance `platine-alerte forcer FENÊTRE NOM`. Forcer le Finder le relance.
- `platine-alerte` exécute lui-même le choix : le WM ne lit pas de code de sortie.
  `ptk_alert` réessaie la saisie du clavier pendant 1 s.
- *2026-11-07* : le WM n'ignore plus SIGCHLD (`SIG_IGN` était hérité par tous les
  programmes lancés, dont les `waitpid` échouaient) ; il ramasse ses enfants par
  `ptk_ramasser_enfants()`, comme le Finder, le Control Strip et À propos.
