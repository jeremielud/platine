# État des lieux — Eee PC 701, 2026-11-02

> **Document historique.** Audit du 701 sous Alpine Linux avec Platine 1 (mlvwm + Tk),
> qui a motivé la réécriture. Depuis le 2026-11-03, le 701 tourne sous NetBSD 11.1 et
> Platine 1 est archivé ; l'état actuel est dans `README.fr.md` et les mesures récentes
> dans `outils/mesures-701.sh` (41,8 Mio pour toute la session le 2026-11-06, contre
> ~85 Mo ici). Les données brutes de `audit/` restent hors dépôt.

Collecte en lecture seule via `ssh eeepc` (root, 192.168.1.11), session X de
`jeremie` sur `:0`. Données brutes : `audit/2026-11-02/`.
Capture : `audit/2026-11-02/bureau.png`.

## Machine et système

| Élément | Valeur observée |
|---|---|
| Modèle | Eee PC 701 4G |
| CPU | Intel Celeron M 900 MHz (i686) |
| RAM | 477 Mo visibles (488 604 Ko), 512 Mo physiques |
| Écran | 800×480, `LVDS1`, i915GM |
| SSD | 3,6 Go, 2,9 Go utilisés, **532 Mo libres (85 %)** |
| OS | Alpine Linux 3.24.3, noyau 6.18.60-0-lts |
| Paquets | 511 |
| Réseau | `wlan0` Atheros AR2425, 192.168.1.11 ; `eth0` DOWN |
| Audio | HDA Intel ALC662, Master 69 %, actif |
| Batterie | 100 %, sur secteur |
| Veille | `freeze mem disk` disponibles (non testés) |

## Session graphique

| Processus | RSS | Rôle |
|---|---:|---|
| Xorg | 34,9 Mo | serveur X, 800×480 |
| mlvwm | 2,4 Mo | WM + barre de menus |
| `finder.tcl` | 11,4 Mo | bureau + fenêtres |
| `platine-barre` | 8,1 Mo | Control Strip |
| `platine-osd` | 5,2 Mo | volume/luminosité |
| `a-propos.tcl` | 11,4 Mo | fenêtre À propos |
| `platine-aide` | 11,4 Mo | aide |
| `platine-afterdark --veille` | 1,7 Mo | démon After Dark |

Total des processus du bureau : environ **85 Mo de RSS**, sans navigateur ni
application lourde. La mémoire n'est pas le problème principal.

Au repos (After Dark sorti) : charge 0,26, 90 % idle. Quand After Dark
« flying-toasters » tourne, il consomme ~20 % d'un cœur et la charge monte à
~1,2 : ce n'est pas le bureau, c'est l'économiseur.

## Ce que montre la capture

![Bureau Platine 1](../audit/2026-11-02/bureau.png)

Points conformes :

- barre de menus de 20 px, avec menu Tux, menus Fichier/Édition/Présentation/
  Aller/Fenêtre/Spécial/Aide, horloge et menu d'application à droite ;
- bureau avec disque « Eee PC 701 », Documents, Images, Corbeille ;
- Control Strip en bas à gauche, avec modules ;
- fenêtres spatiales, icônes, Corbeille.

Écarts visibles par rapport à Mac OS 9 :

- les barres de titre sont des cadres mlvwm plats, sans rayures Platinum ni
  boîtes Fermer/Zoom conformes ;
- la fenêtre « À propos de cet ordinateur » utilise Tux et l'identité Alpine,
  pas une fenêtre « À propos » Mac OS 9 ; la police du corps n'est pas Charcoal ;
- la fenêtre de mise à jour est coupée à droite ; la fenêtre d'aide et la fenêtre
  À propos se chevauchent ; un rectangle noir apparaît en bas à droite ;
- le fond d'écran est une image bleue, pas le motif Mac OS 9 par défaut ;
- les icônes de la Control Strip sont colorées et modernes, pas les modules
  monochromes de Mac OS 9 ;
- le libellé du menu d'application (« À propos de cet ordinateur ») est long et
  prend une part notable de la barre à 800 px.

Ces écarts ne sont pas des bugs isolés : ils viennent de mlvwm, de Tk et des
assets pré-rendus. C'est exactement ce que le nouveau Platine doit remplacer.

## Contraintes matérielles et système

- **Disque** : 532 Mo libres. `/usr` = 1,73 Go, `/home` = 895 Mo dont
  `~/.local` = 773 Mo (données Olympe). `platine` installé = 15,6 Mo.
  Il faut soit déplacer/trim Olympe, soit prévoir un budget strict ; on ne
  compile pas sur le 701.
- **Xorg** : `intel(0): failed to set backlight intel_backlight to brightness
  level 0, disabling` ; la luminosité logicielle est à revoir.
- **Touchpad** : messages libinput « event processing lagging behind by 34–56 ms,
  your system is too slow » et « touch jump detected » ; à vérifier hors After
  Dark, mais c'est un point d'usage réel.
- **CPU** : `eeepc_laptop: cpufv disabled to avoid instability` ; pas de
  changement de fréquence CPU sur ce modèle.
- **Réglementation Wi-Fi** : `regulatory.db` absent (messages cfg80211) ;
  sans impact visible sur le réseau ouvert actuel.

## Versionnement

- `archive/platine1` (ex-`../platine`) est un dépôt git local avec un commit de référence
  (`Platine 1 : état de référence au 2026-11-02`).
- `Mac-OS-9/` est déclaré comme sous-module du thème GTK B00merang.
- `platine/` est la nouvelle base, également versionnée séparément.

## Prochaines actions

1. Confirmer la fondation : WM + `libptk` en C/X11, ou poursuite de mlvwm/Tk.
2. Confirmer le périmètre v1 et la politique d'assets.
3. Écrire les tests de conformité des 12 contrats (`docs/00-essence.md`).
4. Construire le premier incrément vertical : WM + barre de menus + une fenêtre
   Platine, testé sous Xvfb puis sur le 701, sans toucher à la session courante.
