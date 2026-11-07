/*
 * ptk_identite.c — identité des programmes (section ELF « .platine ») et
 * arborescence du volume : où trouver un programme Platine, comment montrer
 * ses dossiers.
 *
 * Le volume « Eee PC 701 » est la racine du disque, comme sous Mac OS X
 * (décision D11) ; PLATINE_VOLUME la remplace pour les tests, et sans
 * /System/Finder (développement) c'est le dossier personnel. Il porte :
 *
 *	Applications/            programmes, chacun un fichier exécutable (le vrai
 *	                         binaire) ou un dossier de programme ; Utilities/
 *	System/                  Finder, Système (le WM), Extensions, Control Panels,
 *	                         Apple Menu Items, Startup Items, Control Strip
 *	                         Modules, Fonts
 *	Library/                 ajouts de la machine
 *	home/                    les dossiers personnels
 *
 * Noms anglais sur le disque, sûrs pour le shell ; le Finder et les zones de
 * dialogue les montrent en français (ptk_nom_affiche), et masquent les
 * dossiers BSD de la racine (ptk_masque), comme Mac OS X.
 */
#include "ptk.h"
#include "ptk_identite.h"

#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SECTION_MAX 4096

const char *
ptk_volume(void)
{
	const char *v = getenv("PLATINE_VOLUME");

	if (v != NULL && *v != '\0')
		return v;
	/* installé : la racine du disque ; sinon (développement, tests) le dossier
	 * personnel, qui porte alors System/ et Applications/ */
	if (access("/System/Finder", F_OK) == 0)
		return "/";
	v = getenv("HOME");
	return v != NULL ? v : "/";
}

/* chemin sans « / » final, pour comparer des dossiers */
static void
net(const char *in, char *out, size_t n)
{
	size_t l;

	snprintf(out, n, "%s", in);
	l = strlen(out);
	while (l > 1 && out[l - 1] == '/')
		out[--l] = '\0';
}

/* le dossier « dossier » est-il « base » suivi de « rel » (rel vide : base) ? */
static int
est_dossier(const char *dossier, const char *base, const char *rel)
{
	char a[PATH_MAX], b[PATH_MAX], c[PATH_MAX];

	net(dossier, a, sizeof(a));
	net(base, b, sizeof(b));
	if (rel[0] == '\0')
		return strcmp(a, b) == 0;
	snprintf(c, sizeof(c), "%s%s%s", strcmp(b, "/") == 0 ? "" : b, "/", rel);
	return strcmp(a, c) == 0;
}

const char *
ptk_nom_affiche(const char *dossier, const char *nom)
{
	/* noms anglais du disque → noms français à l'écran, à leur place seulement
	 * (comme les dossiers « .localized » de Mac OS X) */
	static const struct { const char *ou, *nom, *vu; char base; } t[] = {
		{ "", "Applications", "Applications", 'v' },
		{ "", "Library", "Bibliothèque", 'v' },
		{ "", "System", "Système", 'v' },
		{ "", "home", "Utilisateurs", 'v' },
		{ "", "Users", "Utilisateurs", 'v' },
		{ "Applications", "Utilities", "Utilitaires", 'v' },
		{ "System", "Control Panels", "Tableaux de bord", 'v' },
		{ "System", "Apple Menu Items", "Éléments du menu Pomme", 'v' },
		{ "System", "Startup Items", "Éléments de démarrage", 'v' },
		{ "System", "Control Strip Modules", "Modules Control Strip", 'v' },
		{ "System", "Fonts", "Polices", 'v' },
		{ "System", "Preferences", "Préférences", 'v' },
		{ "Library", "Desktop Pictures", "Images du bureau", 'v' },
		{ "Library", "Fonts", "Polices", 'v' },
		{ "Library", "Preferences", "Préférences", 'v' },
		{ "", "Desktop", "Bureau", 'h' },
		{ "", "Downloads", "Téléchargements", 'h' },
		{ "", "Pictures", "Images", 'h' },
		{ "", "Music", "Musique", 'h' },
		{ "", "Movies", "Vidéos", 'h' },
		{ "", "Library", "Bibliothèque", 'h' },
		{ "Library", "Preferences", "Préférences", 'h' },
		{ "Library", "Fonts", "Polices", 'h' },
	};
	const char *maison = getenv("HOME");
	size_t i;

	if (dossier == NULL || nom == NULL)
		return nom;
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
		const char *base = t[i].base == 'v' ? ptk_volume() : maison;

		if (base != NULL && strcmp(nom, t[i].nom) == 0 &&
		    est_dossier(dossier, base, t[i].ou))
			return t[i].vu;
	}
	return nom;
}

int
ptk_perso(const char *rel, char *out, size_t n)
{
	const char *h = getenv("HOME");

	return (size_t)snprintf(out, n, "%s/%s", h != NULL ? h : "/", rel) < n;
}

int
ptk_bureau(char *out, size_t n)
{
	char cfg[PATH_MAX], l[PATH_MAX + 32];
	const char *h = getenv("HOME");
	struct stat st;
	FILE *f;

	if (h == NULL)
		h = "/";
	/* XDG_DESKTOP_DIR, sinon ~/Desktop, sinon ~/Bureau (avant D11) — comme le Finder */
	snprintf(cfg, sizeof(cfg), "%s/.config/user-dirs.dirs", h);
	if ((f = fopen(cfg, "r")) != NULL) {
		while (fgets(l, sizeof(l), f) != NULL) {
			char *v = l + 17, *fin;

			if (strncmp(l, "XDG_DESKTOP_DIR=\"", 17) != 0 ||
			    (fin = strchr(v, '"')) == NULL)
				continue;
			*fin = '\0';
			if (strncmp(v, "$HOME/", 6) == 0)
				snprintf(out, n, "%s/%s", h, v + 6);
			else
				snprintf(out, n, "%s", v);
			fclose(f);
			return stat(out, &st) == 0;
		}
		fclose(f);
	}
	snprintf(out, n, "%s/Desktop", h);
	if (stat(out, &st) == 0)
		return 1;
	snprintf(out, n, "%s/Bureau", h);
	return stat(out, &st) == 0;
}

int
ptk_masque(const char *dossier, const char *nom)
{
	/* la racine du disque ne montre que le Macintosh : Applications, Library,
	 * System, home ; le reste de BSD est masqué, et « .hidden » en ajoute */
	static const char *const bsd[] = { "altroot", "bin", "boot", "dev", "etc",
	    "home.old", "kern", "lib", "libdata", "libexec", "lost+found", "mnt",
	    "netbsd", "netbsd.old", "netbsd.gdb", "onetbsd", "opt", "proc", "rescue",
	    "root", "sbin", "stand", "tmp", "usr", "var", "boot.cfg", "bin.old",
	    "media", "run", "srv", "sys", "snap", "cdrom", "initrd.img", "vmlinuz" };
	char f[PATH_MAX], l[512];
	FILE *h;
	size_t i;
	int masque = 0;

	if (nom[0] == '.')
		return 1;
	/* seulement à la racine d'un vrai volume : « / », ou PLATINE_VOLUME donné (tests) —
	 * pas quand le volume n'est que le dossier personnel (développement : ~/bin reste) */
	if (!est_dossier(dossier, ptk_volume(), "") ||
	    (strcmp(ptk_volume(), "/") != 0 && getenv("PLATINE_VOLUME") == NULL))
		return 0;
	for (i = 0; i < sizeof(bsd) / sizeof(bsd[0]); i++)
		if (strcmp(nom, bsd[i]) == 0)
			return 1;
	if (strncmp(nom, "netbsd", 6) == 0 || strncmp(nom, "boot", 4) == 0)
		return 1;	/* noyaux et chargeurs : netbsd-platine, boot.netbsd, boot.cfg… */
	snprintf(f, sizeof(f), "%s/.hidden", strcmp(ptk_volume(), "/") == 0 ? "" : ptk_volume());
	if ((h = fopen(f, "r")) != NULL) {
		while (!masque && fgets(l, sizeof(l), h) != NULL) {
			l[strcspn(l, "\n")] = '\0';
			masque = strcmp(l, nom) == 0;
		}
		fclose(h);
	}
	return masque;
}

/* entier de n octets (1, 2, 4 ou 8) dans l'ordre de l'ELF */
static uint64_t
lire_entier(const unsigned char *p, int n, int gros)
{
	uint64_t v = 0;
	int i;

	for (i = 0; i < n; i++)
		v |= (uint64_t)p[gros ? n - 1 - i : i] << (8 * i);
	return v;
}

static int
lire_a(FILE *f, uint64_t pos, void *buf, size_t n)
{
	if (fseeko(f, (off_t)pos, SEEK_SET) != 0)
		return -1;
	return fread(buf, 1, n, f) == n ? 0 : -1;
}

/* contenu brut de la section .platine, 0 si absente */
static int
section_platine(const char *chemin, char *out, size_t outsz)
{
	unsigned char eh[64], sh[64];
	FILE *f = fopen(chemin, "rb");
	int c64, gros, ok = 0;
	uint64_t shoff, shstr_off = 0;
	unsigned shentsize, shnum, shstrndx, i;

	if (f == NULL)
		return 0;
	if (fread(eh, 1, sizeof(eh), f) < 52 || memcmp(eh, "\177ELF", 4) != 0)
		goto fin;
	c64 = eh[4] == 2;
	gros = eh[5] == 2;
	shoff = lire_entier(eh + (c64 ? 0x28 : 0x20), c64 ? 8 : 4, gros);
	shentsize = (unsigned)lire_entier(eh + (c64 ? 0x3a : 0x2e), 2, gros);
	shnum = (unsigned)lire_entier(eh + (c64 ? 0x3c : 0x30), 2, gros);
	shstrndx = (unsigned)lire_entier(eh + (c64 ? 0x3e : 0x32), 2, gros);
	if (shoff == 0 || shentsize < (c64 ? 64u : 40u) || shentsize > sizeof(sh) ||
	    shnum == 0 || shnum > 4096 || shstrndx >= shnum)
		goto fin;
	/* table des noms de sections */
	if (lire_a(f, shoff + (uint64_t)shstrndx * shentsize, sh, shentsize) != 0)
		goto fin;
	shstr_off = lire_entier(sh + (c64 ? 0x18 : 0x10), c64 ? 8 : 4, gros);
	for (i = 0; i < shnum; i++) {
		char nom[10];
		uint64_t nom_off, off, taille;

		if (lire_a(f, shoff + (uint64_t)i * shentsize, sh, shentsize) != 0)
			goto fin;
		nom_off = lire_entier(sh, 4, gros);
		if (lire_a(f, shstr_off + nom_off, nom, sizeof(nom)) != 0)
			continue;
		if (memcmp(nom, ".platine", 9) != 0)
			continue;
		off = lire_entier(sh + (c64 ? 0x18 : 0x10), c64 ? 8 : 4, gros);
		taille = lire_entier(sh + (c64 ? 0x20 : 0x14), c64 ? 8 : 4, gros);
		if (taille == 0 || taille >= outsz)
			goto fin;
		if (lire_a(f, off, out, (size_t)taille) != 0)
			goto fin;
		out[taille] = '\0';
		ok = 1;
		break;
	}
fin:
	fclose(f);
	return ok;
}

static void
champ(const char *texte, const char *cle, char *out, size_t outsz)
{
	size_t lc = strlen(cle);
	const char *p = texte;

	out[0] = '\0';
	while (p != NULL && *p != '\0') {
		if (strncmp(p, cle, lc) == 0 && p[lc] == '=') {
			size_t n = strcspn(p + lc + 1, "\n");

			if (n >= outsz)
				n = outsz - 1;
			memcpy(out, p + lc + 1, n);
			out[n] = '\0';
			return;
		}
		p = strchr(p, '\n');
		if (p != NULL)
			p++;
	}
}

int
ptk_identite_lire(const char *chemin, PtkIdentite *id)
{
	char s[SECTION_MAX];

	memset(id, 0, sizeof(*id));
	if (!section_platine(chemin, s, sizeof(s)) ||
	    strncmp(s, PTK_IDENTITE_MAGIE "\n", sizeof(PTK_IDENTITE_MAGIE)) != 0)
		return 0;
	champ(s, "nom", id->nom, sizeof(id->nom));
	champ(s, "signature", id->signature, sizeof(id->signature));
	champ(s, "icone", id->icone, sizeof(id->icone));
	champ(s, "version", id->version, sizeof(id->version));
	champ(s, "types", id->types, sizeof(id->types));
	return 1;
}

int
ptk_identite_ouvre(const PtkIdentite *id, const char *extension)
{
	size_t le = strlen(extension);
	const char *p = id->types;

	while (le > 0 && *p != '\0') {
		size_t n = strcspn(p, " ,");

		if (n == le && strncasecmp(p, extension, le) == 0)
			return 1;
		p += n;
		p += strspn(p, " ,");
	}
	return 0;
}

/* cherche dans un dossier (et ses dossiers de programme, sur « prof »
 * niveaux) un exécutable portant la signature ou ouvrant l'extension */
static int
chercher(const char *dossier, const char *signature, const char *extension,
    int prof, char *out, size_t outsz)
{
	DIR *d = opendir(dossier);
	struct dirent *e;
	int trouve = 0;

	if (d == NULL)
		return 0;
	while (!trouve && (e = readdir(d)) != NULL) {
		char ch[PATH_MAX];
		struct stat st;
		PtkIdentite id;

		if (e->d_name[0] == '.')
			continue;
		if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", dossier,
		    e->d_name) >= sizeof(ch) || stat(ch, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (prof > 0)
				trouve = chercher(ch, signature, extension,
				    prof - 1, out, outsz);
			continue;
		}
		if (!S_ISREG(st.st_mode) || access(ch, X_OK) != 0 ||
		    !ptk_identite_lire(ch, &id))
			continue;
		if ((signature != NULL && strcmp(id.signature, signature) == 0) ||
		    (extension != NULL && ptk_identite_ouvre(&id, extension))) {
			snprintf(out, outsz, "%s", ch);
			trouve = 1;
		}
	}
	closedir(d);
	return trouve;
}

/* base de bureau : Applications puis Dossier Système, sur deux niveaux de
 * dossiers (Applications/Utilities/Programme ƒ/Programme) */
static int
base_de_bureau(const char *signature, const char *extension, char *out,
    size_t outsz)
{
	static const char *const racines[] = { "Applications", "System" };
	char r[PATH_MAX];
	size_t i;

	for (i = 0; i < sizeof(racines) / sizeof(racines[0]); i++) {
		if ((size_t)snprintf(r, sizeof(r), "%s/%s", ptk_volume(),
		    racines[i]) >= sizeof(r))
			continue;
		if (chercher(r, signature, extension, 3, out, outsz))
			return 1;
	}
	return 0;
}

int
ptk_programme(const char *signature, char *out, size_t outsz)
{
	return base_de_bureau(signature, NULL, out, outsz);
}

int
ptk_programme_pour(const char *extension, char *out, size_t outsz)
{
	return base_de_bureau(NULL, extension, out, outsz);
}

/* copie entre apostrophes pour sh -c : ' devient '\'' */
static void
citer(const char *s, char *out, size_t outsz)
{
	size_t n = 0;

	if (outsz < 3) {
		if (outsz > 0)
			out[0] = '\0';
		return;
	}
	out[n++] = '\'';
	for (; *s != '\0' && n + 5 < outsz; s++) {
		if (*s == '\'') {
			memcpy(out + n, "'\\''", 4);
			n += 4;
		} else
			out[n++] = *s;
	}
	out[n++] = '\'';
	out[n] = '\0';
}

const char *
ptk_resoudre(const char *ref, char *out, size_t outsz)
{
	const char *barre;
	char rel[PATH_MAX], ch[PATH_MAX];
	size_t n;

	if (ref != NULL && ref[0] == '#') {
		/* « #signature|repli » : où que le programme soit rangé */
		char sig[8];

		barre = strchr(ref, '|');
		n = barre != NULL ? (size_t)(barre - ref - 1) : strlen(ref + 1);
		if (n >= sizeof(sig))
			n = sizeof(sig) - 1;
		memcpy(sig, ref + 1, n);
		sig[n] = '\0';
		if (ptk_programme(sig, ch, sizeof(ch))) {
			citer(ch, out, outsz);
			return out;
		}
		snprintf(out, outsz, "%s", barre != NULL ? barre + 1 : "");
		return out;
	}
	if (ref == NULL || ref[0] != '@') {
		snprintf(out, outsz, "%s", ref != NULL ? ref : "");
		return out;
	}
	barre = strchr(ref, '|');
	n = barre != NULL ? (size_t)(barre - ref - 1) : strlen(ref + 1);
	if (n >= sizeof(rel))
		n = sizeof(rel) - 1;
	memcpy(rel, ref + 1, n);
	rel[n] = '\0';
	if ((size_t)snprintf(ch, sizeof(ch), "%s/%s", ptk_volume(), rel) <
	    sizeof(ch) && access(ch, X_OK) == 0) {
		citer(ch, out, outsz);
		return out;
	}
	snprintf(out, outsz, "%s", barre != NULL ? barre + 1 : "");
	return out;
}

int
ptk_chemin_programme(const char *rel, const char *repli, char *out, size_t outsz)
{
	if ((size_t)snprintf(out, outsz, "%s/%s", ptk_volume(), rel) < outsz &&
	    access(out, X_OK) == 0)
		return 1;
	snprintf(out, outsz, "%s", repli);
	return 0;
}

int
ptk_dans_volume(const char *rel, char *out, size_t outsz)
{
	return (size_t)snprintf(out, outsz, "%s/%s", ptk_volume(), rel) < outsz;
}
