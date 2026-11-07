# Décisions — Platine

Chaque décision porte sa date ; un amendement est daté là où il s'applique.

## D1. OS de référence : NetBSD 11.1 i386 — **depuis le 2026-11-03**

Le 701 tourne sous **NetBSD 11.1 i386** depuis le 2026-11-03. Le matériel y
fonctionne sans travail noyau : i915GM accéléré, Wi-Fi Atheros AR2425 avec le
`wpa_supplicant` de la base, audio HDA, batterie et couvercle par `powerd`.
L'argumentaire est dans le `README.md` (« Why NetBSD »).

Avant cette date, la référence était **Alpine Linux 3.24 i686** (noyau 6.18
LTS) ; les mêmes sources compilent toujours pour Alpine (`../eeepc-alpine`). Ce
qui diffère d'un système à l'autre est confiné dans `toolbox/ptk_sys.h` et
`outils/construire-netbsd.sh`.

Conséquence : X11 (Xorg de la base, `/usr/X11R7`), C, aucune dépendance à
systemd ni à `pkg-config`.

## D2. Fondation : WM + `libptk` en C/X11 — **confirmé le 2026-11-02**

Confirmé par l'utilisateur. Le premier incrément est décrit dans
`03-phase1-wm.md` : cadres, barre de menus, menus, focus, zoom, repli,
sélecteur d'application, `libptk` et application de démonstration.

mlvwm + Tk (Platine 1) sont remplacés par :

- `platine-wm` : cadres, focus, pile, déplacement/redimensionnement, fermeture,
  zoom, WindowShade, barre de menus, sélecteur d'applications, session ;
- `libptk` (la Toolbox, `toolbox/`) : dessin Platinum procédural, fenêtres,
  menus, dialogues, contrôles, texte, sons ;
- `platine-finder` : bureau et fenêtres spatiales ;
- services : son, préférences, lancement, presse-papiers, alimentation.

L'option intermédiaire (garder Tk pour les applications) a été écartée :
l'incohérence visuelle restait.

## D3. Assets : usage personnel — **confirmé ; amendé le 2026-11-07**

Les ressources extraites de Mac OS 9 (applications entières, rsrc, programmes
tiers) restent dans `ressources/`, hors dépôt. Les assets que le bureau charge
(icônes, motif, polices bitmap, sons) sont préparés depuis ces ressources par
`outils/preparer-assets.sh`.

*Amendement du 2026-11-07* : ces assets **entrent dans le dépôt publié**
(`assets/`, `systeme/polices/`, `systeme/curseurs/`, `systeme/themes/`, captures
réelles de `docs/img/public/`), propriété d'Apple, hébergés pour l'usage
strictement personnel de l'auteur — voir `NOTICE.md`. `ressources/` et `audit/`
restent privés. Pour une image diffusable un jour : assets originaux ou libres
(ChicagoFLF pour Chicago, équivalents libres de Charcoal et Geneva), aucune
marque Apple ; le code tourne déjà sans les assets, avec un habillage neutre.

## D4. Périmètre v1 — **à confirmer**

État au 2026-11-07 : presque tout le « Doit » est livré ; restent l'Extensions
Manager, Mémoire, Disque de démarrage et le carillon de démarrage complet
(l'écran de démarrage existe). Du « Devrait », le pont QuickTime et StuffIt
Expander sont livrés, les dossiers à ressort et les alias aussi.

Doit :

- WM, barre de menus, app switcher, horloge ;
- Finder spatial, bureau, corbeille, Lire les informations ;
- contrôles Platinum, alertes, dialogues Ouvrir/Enregistrer ;
- son (carillon, alertes, sons d'interface), volume ;
- Control Strip + modules volume/batterie/Wi-Fi/luminosité/résolution ;
- panneaux Apparence, Son, Moniteurs, Souris, Clavier, Date et heure,
  Économiseur, Disque de démarrage, Extensions Manager, Mémoire ;
- session, démarrage, arrêt, redémarrage, force quit ;
- TextEdit, Pense-bêtes, Calculatrice, Key Caps, Profileur système.

Devrait :

- étiquettes, alias, dossiers à ressort, aide bulles, Launcher ;
- pont QuickTime (Lecteur QuickTime sur mplayer) ; AppleWorks et MacAmp abandonnés le 2026-11-07 ;
- StuffIt Expander recréé (.sit, .7z, .zip, .rar…) ;
- After Dark porté en X11 (ou laissé de côté) ;
- réseau/Wi-Fi dans un panneau.

Ne fera pas :

- compatibilité binaire Mac OS 9, HFS+, AppleTalk, ROM Apple ;
- QuickDraw 3D, ColorSync, multitâche coopératif ;
- navigateur moderne intégré.

## D5. Budgets — **à valider sur le 701**

| Ressource | Cible |
|---|---:|
| RSS session complète | ≤ 96 Mo |
| WM | ≤ 8 Mo |
| Shell + Finder | ≤ 25 Mo |
| Disque DE hors assets | ≤ 30 Mo |
| Boot jusqu'au bureau | ≤ 25 s |
| CPU au repos | ≤ 3 % |
| Clic → réaction | ≤ 50 ms |

## D6. Règles de travail

- UTF-8, pas de Latin-1.
- Pas de `sed` sur les chemins, pas de table classe → menus.
- Pas d'assets Apple dans un livrable publiable.
- Un test par contrat de `00-essence.md`.
- Compilation sur le PC, déploiement de binaires i686 sur le 701.
- Ne pas modifier la session en cours sans accord ; tester sous Xvfb d'abord.
- *Ajout du 2026-11-07* : ne jamais ignorer `SIGCHLD` (`SIG_IGN` s'hérite à travers
  `exec` : tout programme lancé ne pourrait plus attendre ses enfants) ; un processus
  qui lance des programmes appelle `ptk_ramasser_enfants()`.

## D7. Nom et archive — **décidé le 2026-11-03**

Ce projet, d'abord appelé « Platine 2 », devient **Platine**. L'ancienne version mlvwm + Tk
est archivée et ne reçoit plus de développement.

**Mise à jour 2026-11-06** : le projet quitte `~/Projets/eeepc-alpine/platine` pour
`~/Projets/platine`. Les ressources Mac OS 9 extraites sont dans `ressources/macos9` et
Platine 1 dans `archive/platine1` (son propre dépôt git) ; ces deux dossiers ne sont pas versionnés.
L'environnement Alpine i686 de compilation reste dans `~/Projets/eeepc-alpine`.

## D8. Où vit une application — **décidé le 2026-11-07**

Comme sous Mac OS 9, où une application se trouve à la fois dans le dossier Applications
et dans le menu Pomme, **toute application Platine (TextEdit, Théière, et toutes les
suivantes) doit apparaître aux deux endroits** :

1. **Menu Pomme** : un élément (le programme, ou son alias) dans `Dossier Système/Éléments
   du menu Pomme` ; le menu montre ce dossier, trié, avec l'icône de chaque programme, et un
   sous-menu par dossier. *Amendé le 2026-11-07 : la table `pomme[]` de `src/wm/wm.c` ne sert
   plus que de repli quand ce dossier n'existe pas (développement, tests).* Un programme
   installé par l'Installateur y reçoit son alias par la case **« Ajouter au menu Pomme »**,
   cochée par défaut (clé `pomme=` du manifeste) ; la désinstallation retire l'alias.
2. **Dossier Applications** : le programme lui-même y est installé — le vrai binaire,
   portant son identité, sous son nom français (D9). *Amendé le 2026-11-07 : ce n'est
   plus un lanceur `#!/bin/sh` vers `/opt/platine/bin`.*

Le binaire est compilé sous un nom ASCII (`platine-textedit`, `platine-theiere`) et sa
source vit dans `src/apps/<nom>/` ; l'installation le pose sous son nom visible. Le libellé visible (fenêtre,
barre de menus, menu Pomme) est le nom français de l'application. Une application
extérieure utile (Terminal, Album) peut figurer au menu Pomme sans être une application
Platine ; elle n'a alors pas de lanceur dans Applications.

Cette règle fait partie de la définition de « terminé » pour chaque application : une
application livrée sans son entrée de menu ni son lanceur n'est pas terminée.

## D9. L'arborescence Macintosh et l'identité des programmes — **décidé le 2026-11-07**

Le volume « Eee PC 701 » (le dossier personnel) porte l'arborescence d'un disque
Mac OS 9, et **chaque programme de l'espace utilisateur y est un fichier exécutable,
l'icône que l'on double-clique** — pas un lanceur :

- `Applications/` : les programmes (TextEdit, Théière…), posés à plat ou dans un
  dossier de programme (`Théière ƒ/` : le programme, son Lisez-moi, ses compagnons),
  sous-dossiers comme `Utilitaires/` ;
- `Dossier Système/` : `Finder`, `Système` (le WM), `Extensions/` (Control Strip, Alerte,
  Lire les informations, À propos de cet ordinateur, Affichage à l'écran, Écran de
  démarrage), `Tableaux de bord/` (un fichier par tableau, même programme sous des
  noms différents), `Éléments du menu Pomme/` (Calculette, alias Tableaux de bord),
  `Préférences` (alias de `~/.config/platine`), `Polices`, `Éléments de démarrage/`
  (ouverts par la session après le Finder).

**Identité embarquée** : comme la ressource d'une application Mac OS 9 (icône, `vers`,
`BNDL`), chaque binaire porte une section ELF `.platine` (`PTK_IDENTITE`,
`toolbox/ptk_identite.h`) : nom, **signature** de 4 caractères (code créateur), icône,
version, types de documents. Le Finder en tire l'icône, le genre « programme » et la
version (Lire les informations), et ouvre un document avec le programme qui déclare son
extension (base de bureau, `ptk_programme_pour`). Le WM, le Control Strip et la session
trouvent les programmes par chemin dans l'arborescence (`@Dossier Système/…|repli`) ou par
signature (`#ttxt|repli`, où qu'ils soient rangés), avec repli sur le PATH pour le
développement et les tests.

**Côté BSD/Linux, rien ne change** : `/opt/platine` garde la session, les privilèges,
les outils (écran rc, xclic) et les ressources ; `/etc`, `/usr` ne sont pas touchés.
L'installation (`outils/installer-netbsd.sh` → `outils/poser-arborescence.sh`) pose les
binaires et ne laisse plus rien des programmes dans `/opt/platine/bin`.

**Programmes complexes : l'Installateur**, à l'image d'Installer VISE. Un programme
d'installation est un seul fichier exécutable (`platine-installateur` + archive +
manifeste, fabriqué par `outils/faire-installateur.sh`) : Lisez-moi, Installation
standard / personnalisée / Désinstallation, lieu (Applications par défaut), progression,
« L'installation a réussi ». L'expandeur d'archives façon StuffIt est livré
(`platine-expander`).

Conséquences : un processus s'appelle désormais comme son fichier (« Système »,
« Finder », « Control Strip ») — ce qui visait un nom Unix (powerd, signal au Strip) a
été adapté ; une application est terminée quand elle est installée dans Applications
avec son identité et au menu Pomme (D8).

**Révisé par D11 (2026-11-07)** pour l'emplacement : Applications et le système sont
désormais à la racine du disque. L'identité embarquée et la base de bureau restent.

## D10. Le dossier du bureau — **décidé le 2026-11-07**

Comme le « Desktop Folder » de Mac OS 9, le bureau montre **un vrai dossier** :
`XDG_DESKTOP_DIR` (`~/.config/user-dirs.dirs`) s'il est donné, sinon `~/Desktop`
(montré « Bureau », D11 ; `~/Bureau` d'avant D11 reste reconnu). Il
est invisible dans la fenêtre de son dossier parent. Le disque de démarrage et la
Corbeille y sont fixes (ni glissés hors du bureau, ni renommés) ; la Corbeille va au
coin bas droit tant qu'on ne l'a pas déplacée.

Conséquences : tout ce qui marche dans une fenêtre marche sur le bureau (dépôts,
Nouveau dossier, Coller, Créer un alias, renommage) ; un élément lâché sur le fond du
bureau **va dans ce dossier** (il n'est plus refusé), un élément sorti de la Corbeille
vers le bureau s'y pose (la remise en place reste Fichier ▸ Ranger) ; le dossier est
relu toutes les 2 s, pour ce qu'y posent les autres programmes. À sa création, il
reçoit les alias des anciens dossiers fixes (Documents, Images, Applications) et,
par `poser-arborescence.sh`, ceux du Lecteur QuickTime et de StuffIt Expander. Un
document déposé sur un programme ou son alias est ouvert par ce programme.

## D11. L'arborescence à la racine du disque, comme Mac OS X — **décidé le 2026-11-07**

Relevé sur Mac OS X 10.6 (`docs/12-macosx-arborescence.md`), choisi par l'utilisateur :

- **Le volume « Eee PC 701 » est la racine du disque** (`ptk_volume()` : `/` quand
  `/System/Finder` existe ; `PLATINE_VOLUME` pour les tests ; le dossier personnel en
  développement). La fenêtre du disque ne montre que **Applications, Bibliothèque,
  Système, Utilisateurs** : les dossiers BSD de la racine (`bin`, `etc`, `usr`, `var`,
  noyaux…) et ceux de `/.hidden` sont masqués par le Finder et les zones de dialogue
  (`ptk_masque`) — NetBSD n'a pas le drapeau `hidden` de Mac OS X.
- **Noms anglais sur le disque, noms français à l'écran** (`ptk_nom_affiche`, l'équivalent
  des dossiers `.localized`) : `/Applications`, `/Applications/Utilities` (Utilitaires),
  `/System` (Système) avec `Extensions`, `Control Panels` (Tableaux de bord), `Apple Menu
  Items` (Éléments du menu Pomme), `Startup Items` (Éléments de démarrage), `Control Strip
  Modules`, `Fonts` (Polices) ; `/Library` (Bibliothèque), `Desktop Pictures` (Images du
  bureau) ; `/home` (Utilisateurs) ; et dans le dossier personnel `Desktop` (Bureau),
  `Downloads` (Téléchargements), `Pictures` (Images), `Music`, `Movies`, `Library`. Les
  chemins restent sûrs pour le shell ; un dossier au nom traduit ne se renomme pas. Les
  noms des programmes eux-mêmes ne changent pas (à reprendre plus tard, comme les noms de
  paquets de Mac OS X).
- **`/home` reste** (convention BSD), montré « Utilisateurs ».
- **Tout part à la racine** : `/Applications`, `/System`, `/Library` (root, groupe de
  l'utilisateur, 775, pour que l'Installateur y pose des programmes, extensions et
  modules — compromis d'un portable à un seul utilisateur). Le dossier personnel garde
  Desktop, Documents, Downloads, Pictures… et `~/Library/Preferences` (lien vers
  `~/.config/platine`). Les dossiers personnels de la Toolbox passent par `ptk_perso` et
  `ptk_bureau` (Documents, Movies, Captures, le bureau), plus par le volume.
- **Migration** (`outils/poser-arborescence.sh`) : ce que l'utilisateur avait ajouté dans
  `~/Applications` et `~/Dossier Système` est repris sans rien écraser, puis les anciens
  dossiers sont retirés ; Bureau, Téléchargements, Images, Musique, Vidéos prennent leur
  nom anglais ; les alias du bureau suivent. Test : `tests/run-arborescence.sh`.
