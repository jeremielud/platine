# Mac OS X 10.6 — comment un Mac se pose sur un Unix (relevé du 2026-11-07)

Relevé sur la VM Snow Leopard 10.6 (10A432, français) de
`~/Projets/snowleopard-ilife-iwork` (QEMU/KVM, Chameleon ; VNC 127.0.0.1:5901,
`ssh -F ssh_config sl`). Données brutes, hors dépôt : `audit/macosx-ref/`
(`arborescence-10.6.txt`, `localisation-fr.tsv`, `finder-racine.png`).

But : préparer la révision de la décision D9, pour que Platine pose Applications et
Système **à la racine du disque** et s'intègre aux Unix-like comme Mac OS X l'a fait.

## Ce que montre le Finder

La fenêtre du disque « MacintoshHD » ne montre que **quatre éléments** :
**Applications, Bibliothèque, Système, Utilisateurs**. Tout le reste de la racine existe
mais est masqué.

```
/                         (le disque ; /Volumes/MacintoshHD -> / pour le Finder)
├── Applications/         montré « Applications »  — programmes (.app), Utilities/
├── Library/              montré « Bibliothèque »  — ressources partagées de la machine
├── System/               montré « Système »       — System/Library : le système seul
├── Users/                montré « Utilisateurs »  — un dossier par compte, Shared/
│
│   masqués par le drapeau « hidden » (chflags) :
├── bin  sbin  usr  dev  private  cores  Network  Volumes  mach_kernel
├── etc -> private/etc   tmp -> private/tmp   var -> private/var   (liens)
├── home  net             (points de montage automount)
└── .Trashes .vol .hotfiles.btree …  (noms en point, masqués d'office)
```

## Trois mécanismes à retenir

1. **Masquer par un drapeau, pas par un nom.** Les dossiers BSD portent le drapeau de
   fichier `hidden` (`chflags hidden`, `UF_HIDDEN`) ; le Finder ne les montre pas, le
   shell les voit normalement. `/.hidden` (liste de noms des 10.3–10.4) n'existe plus en
   10.6. Les noms commençant par un point sont masqués d'office.
2. **Noms anglais sur le disque, noms français à l'écran.** `Applications`, `Library`,
   `System`, `Users`, `Desktop`, `Downloads`… restent en ASCII pour les programmes et les
   scripts ; un fichier vide `.localized` dans le dossier demande au Finder d'afficher le
   nom traduit, pris dans
   `System/Library/CoreServices/SystemFolderLocalizations/French.lproj/SystemFolderLocalizations.strings`.
   Table relevée (extrait) :

   | Disque | Écran |
   |---|---|
   | Applications | Applications |
   | Library | Bibliothèque |
   | System | Système |
   | Users | Utilisateurs |
   | Utilities | Utilitaires |
   | Desktop | Bureau |
   | Documents | Documents |
   | Downloads | Téléchargements |
   | Movies / Music / Pictures | Vidéos / Musique / Images |
   | Public, Sites, Shared, Drop Box | Public, Sites, Partagé, Boîte de dépôt |
   | Favorites, Saved Searches | Favoris, Recherches enregistrées |

3. **Trois niveaux de Bibliothèque.** `System/Library` (le système, intouchable),
   `/Library` (ajouts de la machine : polices, extensions, préférences, plug-ins),
   `~/Library` (ceux de l'utilisateur : Preferences, Fonts, Application Support, Caches…).
   Un programme cherche dans l'ordre utilisateur → machine → système. C'est l'héritier
   direct du Dossier Système de Mac OS 9 éclaté en trois.

Autres détails : le dossier personnel (`/Users/reavm`) contient Desktop, Documents,
Downloads, Library, Movies, Music, Pictures, Public, Sites, tous `.localized` ;
`/Volumes` porte les disques montés et un lien vers le disque de démarrage ; les
programmes sont des **paquets** (`Calculator.app/` est un dossier que le Finder montre
comme un seul fichier).

## Ce que cela suggère pour Platine (à décider : révision de D9)

- **Racine du disque** : `/Applications` et un dossier système à la racine, le reste de
  NetBSD masqué par le Finder. Le Finder montrerait le disque comme Mac OS X montre
  MacintoshHD : Applications, Système (et Bibliothèque, Utilisateurs si on les adopte).
- **Noms ASCII + affichage français**, à la manière de `.localized` : les chemins restent
  sûrs pour le shell et les scripts (`/Applications`, `/System`), le Finder affiche
  « Système », « Bureau », « Téléchargements ». Cela règle aussi `~/Bureau` contre
  `~/Desktop` (XDG) : un seul dossier, deux noms.
- **Masquage** : NetBSD n'a pas de drapeau `UF_HIDDEN` (FreeBSD l'a) ; il faudrait une
  liste lue par le Finder (comme `/.hidden` des 10.3–10.4) pour `bin`, `etc`, `usr`,
  `var`… ; les noms en point restent masqués d'office.
- **Dossier personnel** : NetBSD a `/home` ; `Utilisateurs` peut le montrer sous ce nom.
- **Bibliothèques** : l'équivalent de `~/Library` est aujourd'hui éparpillé
  (`~/.config/platine`, `~/.cache`) ; à trancher.

**Décidé et appliqué le 2026-11-07 : décision D11** (`02-decisions.md`) — noms anglais
sur le disque montrés en français, `/home` gardé et montré « Utilisateurs », tout à la
racine, migration de l'ancienne disposition. Masquage par une liste intégrée et
`/.hidden`, faute de drapeau `UF_HIDDEN` sous NetBSD.
