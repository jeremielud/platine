/*
 * platine-installateur — l'Installateur de Platine, à l'image d'Installer VISE
 * sous Mac OS 9 : un programme d'installation est un seul fichier exécutable
 * qui porte ce qu'il installe.
 *
 * Fichier « Installation de Théière » :
 *
 *	[ce programme][archive tar][manifeste][ligne de fin]
 *
 * La ligne de fin (64 octets, à la toute fin) : « PLATINE-INSTALLATEUR-1 off
 * lg off lg » — position et longueur de l'archive puis du manifeste. Le
 * manifeste (texte clé=valeur) :
 *
 *	nom=Théière
 *	version=1.0
 *	lieu=Applications              dossier du volume où l'on installe
 *	dossier=Théière ƒ              dossier de programme créé (vide : à plat)
 *	lisezmoi=Lisez-moi Théière     montré avant l'installation (facultatif)
 *	licence=Licence Théière        à accepter avant l'installation (facultatif)
 *	apres=Applications/Théière ƒ/Théière   lancé après l'installation (facultatif)
 *	pomme=Théière                  élément de l'archive à mettre au menu Pomme : case
 *	                               « Ajouter au menu Pomme » (cochée, décision D8)
 *	composant=Nom|Description|1|chemin:chemin[|destination]
 *	    1 : de l'installation standard ; destination : dossier du volume propre
 *	    à ce composant (une extension va dans « System/Extensions »)
 *
 * Déroulé VISE : Lisez-moi (Continuer), fenêtre d'installation (Installation
 * standard / personnalisée / Désinstallation, lieu, Quitter / Installer),
 * progression (Arrêter), puis « L'installation a réussi » (Continuer /
 * Quitter). Fabriqué par outils/faire-installateur.sh.
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

PTK_IDENTITE("Installateur", "inst", "application", PTK_VERSION, "");

#define FIN_LG 64
#define FIN_MAGIE "PLATINE-INSTALLATEUR-1"
#define COMPOSANTS_MAX 8
#define LIEUX "Applications|Applications/Utilitaires|Documents|Autre…"

typedef struct {
	char nom[64];
	char description[256];
	int standard;
	char chemins[512];	/* séparés par « : », relatifs à la racine de l'archive */
	char dest[256];		/* dossier du volume propre au composant, vide : le lieu */
	PtkControl *case_;
} Composant;

static struct {
	PtkApp app;
	PtkWindow *fen, *prog;
	PtkControl *mode, *lieu, *installer, *quitter, *barre, *etat, *arreter;
	char moi[PATH_MAX];
	long long tar_off, tar_lg;
	char nom[64], version[64], lieu_def[256], dossier[256], lisezmoi[256];
	char lieu_avant[256];	/* le lieu restitué si « Autre… » est annulé */
	char licence[256], apres[256], pomme[256];
	PtkControl *case_pomme;
	Composant comp[COMPOSANTS_MAX];
	int ncomp;
	int arret;	/* Arrêter pendant la progression */
} I;

/* --- lecture du programme d'installation --- */

static int
lire_fin(FILE *f, long long *a, long long *b, long long *c, long long *d)
{
	char fin[FIN_LG + 1];

	if (fseeko(f, -FIN_LG, SEEK_END) != 0 || fread(fin, 1, FIN_LG, f) != FIN_LG)
		return 0;
	fin[FIN_LG] = '\0';
	return strncmp(fin, FIN_MAGIE " ", sizeof(FIN_MAGIE)) == 0 &&
	    sscanf(fin + sizeof(FIN_MAGIE), "%lld %lld %lld %lld", a, b, c, d) == 4;
}

static void
champ(const char *ligne, const char *cle, char *out, size_t n)
{
	size_t l = strlen(cle);

	if (strncmp(ligne, cle, l) == 0 && ligne[l] == '=')
		snprintf(out, n, "%s", ligne + l + 1);
}

static int
charger(void)
{
	FILE *f = fopen(I.moi, "rb");
	long long man_off, man_lg;
	char *m, *l, *sv;

	if (f == NULL)
		return 0;
	if (!lire_fin(f, &I.tar_off, &I.tar_lg, &man_off, &man_lg) ||
	    man_lg <= 0 || man_lg > 65536 || fseeko(f, (off_t)man_off, SEEK_SET) != 0) {
		fclose(f);
		return 0;
	}
	m = calloc(1, (size_t)man_lg + 1);
	if (m == NULL || fread(m, 1, (size_t)man_lg, f) != (size_t)man_lg) {
		free(m);
		fclose(f);
		return 0;
	}
	fclose(f);
	snprintf(I.lieu_def, sizeof(I.lieu_def), "Applications");
	for (l = strtok_r(m, "\n", &sv); l != NULL; l = strtok_r(NULL, "\n", &sv)) {
		champ(l, "nom", I.nom, sizeof(I.nom));
		champ(l, "version", I.version, sizeof(I.version));
		champ(l, "lieu", I.lieu_def, sizeof(I.lieu_def));
		champ(l, "dossier", I.dossier, sizeof(I.dossier));
		champ(l, "lisezmoi", I.lisezmoi, sizeof(I.lisezmoi));
		champ(l, "licence", I.licence, sizeof(I.licence));
		champ(l, "apres", I.apres, sizeof(I.apres));
		champ(l, "pomme", I.pomme, sizeof(I.pomme));
		if (strncmp(l, "composant=", 10) == 0 && I.ncomp < COMPOSANTS_MAX) {
			Composant *c = &I.comp[I.ncomp++];
			char *p = l + 10, *q;
			int k = 0;

			while (p != NULL) {
				q = strchr(p, '|');
				if (q != NULL)
					*q++ = '\0';
				if (k == 0)
					snprintf(c->nom, sizeof(c->nom), "%s", p);
				else if (k == 1)
					snprintf(c->description, sizeof(c->description), "%s", p);
				else if (k == 2)
					c->standard = atoi(p);
				else if (k == 3)
					snprintf(c->chemins, sizeof(c->chemins), "%s", p);
				else if (k == 4)
					snprintf(c->dest, sizeof(c->dest), "%s", p);
				k++;
				p = q;
			}
		}
	}
	free(m);
	return I.nom[0] != '\0' && I.ncomp > 0;
}

/* l'archive dans un fichier temporaire, pour tar */
static int
extraire_archive(char *tmp, size_t n)
{
	FILE *f = fopen(I.moi, "rb"), *o;
	char buf[65536];
	long long reste = I.tar_lg;
	int fd;

	snprintf(tmp, n, "%s/platine-installateur.XXXXXX",
	    getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp");
	if (f == NULL || (fd = mkstemp(tmp)) < 0) {
		if (f != NULL)
			fclose(f);
		return 0;
	}
	o = fdopen(fd, "wb");
	if (o == NULL || fseeko(f, (off_t)I.tar_off, SEEK_SET) != 0) {
		fclose(f);
		if (o != NULL)
			fclose(o);
		unlink(tmp);
		return 0;
	}
	while (reste > 0) {
		size_t k = reste > (long long)sizeof(buf) ? sizeof(buf) : (size_t)reste;

		if (fread(buf, 1, k, f) != k || fwrite(buf, 1, k, o) != k)
			break;
		reste -= (long long)k;
	}
	fclose(f);
	fclose(o);
	if (reste != 0) {
		unlink(tmp);
		return 0;
	}
	return 1;
}

/* --- destination --- */

static void
destination(char *out, size_t n)
{
	const char *lieu = I.lieu->text[0] ? I.lieu->text : I.lieu_def;
	int k;

	/* nom montré → nom du disque (Utilitaires est Applications/Utilities) */
	if (strcmp(lieu, "Applications/Utilitaires") == 0)
		lieu = "Applications/Utilities";

	/* chemin trop long : vide, l'installation échoue proprement */
	if (lieu[0] == '/')	/* un disque monté, hors du volume */
		k = I.dossier[0] != '\0' ?
		    snprintf(out, n, "%s/%s", lieu, I.dossier) :
		    snprintf(out, n, "%s", lieu);
	else if (strcmp(lieu, "Documents") == 0) {
		/* Documents est dans le dossier personnel, pas à la racine du disque */
		char doc[PATH_MAX];

		ptk_perso("Documents", doc, sizeof(doc));
		k = I.dossier[0] != '\0' ? snprintf(out, n, "%s/%s", doc, I.dossier) :
		    snprintf(out, n, "%s", doc);
	} else if (I.dossier[0] != '\0')
		k = snprintf(out, n, "%s/%s/%s", ptk_volume(), lieu, I.dossier);
	else
		k = snprintf(out, n, "%s/%s", ptk_volume(), lieu);
	if (k < 0 || (size_t)k >= n)
		out[0] = '\0';
}

/* dossier où va un composant : le sien dans le volume, sinon le lieu choisi */
static void
comp_dest(const Composant *c, char *out, size_t n)
{
	if (c->dest[0] != '\0') {
		if ((size_t)snprintf(out, n, "%s/%s", ptk_volume(), c->dest) >= n)
			out[0] = '\0';
	} else
		destination(out, n);
}

/* « de » ou « d' » devant le nom (élision devant une voyelle ou un h muet
 * supposé) : « Installation d'Instantané », « Installation de Théière » */
static const char *
de(const char *nom)
{
	static const char *const voyelles[] = { "A", "E", "I", "O", "U", "Y", "H", "É", "È",
	    "Ê", "Î", "Ô", "a", "e", "i", "o", "u", "y", "h", "é", "è", "ê", "î", "ô" };
	size_t i;

	for (i = 0; i < sizeof(voyelles) / sizeof(voyelles[0]); i++)
		if (strncmp(nom, voyelles[i], strlen(voyelles[i])) == 0)
			return "d'";
	return "de ";
}

/* l'alias du menu Pomme : « System/Apple Menu Items/Nom » */
static int
alias_pomme(char *out, size_t n)
{
	return (size_t)snprintf(out, n, "%s/System/Apple Menu Items/%s",
	    ptk_volume(), I.nom) < n;
}

/* l'élément pomme= installé : dans le dossier de son composant */
static int
cible_pomme(char *out, size_t n)
{
	int i;

	for (i = 0; i < I.ncomp; i++) {
		const char *p = I.comp[i].chemins;
		size_t lp = strlen(I.pomme);

		while (*p != '\0') {
			size_t l = strcspn(p, ":");

			if (l == lp && strncmp(p, I.pomme, l) == 0) {
				char cd[PATH_MAX];

				comp_dest(&I.comp[i], cd, sizeof(cd));
				return cd[0] != '\0' &&
				    (size_t)snprintf(out, n, "%s/%s", cd, I.pomme) < n;
			}
			p += l;
			if (*p == ':')
				p++;
		}
	}
	return 0;
}

/* comme Mac OS 9 où un programme vit aussi au menu Pomme : alias relatif au
 * volume (le menu Pomme suit les alias jusqu'à l'original) */
static int mkdir_p(const char *d);

static void
poser_alias_pomme(void)
{
	char al[PATH_MAX], cible[PATH_MAX], lien[PATH_MAX], d[PATH_MAX];
	const char *vol = ptk_volume();
	size_t lv = strlen(vol);
	struct stat st;

	if (I.pomme[0] == '\0' || I.case_pomme == NULL || !I.case_pomme->checked ||
	    !alias_pomme(al, sizeof(al)) || !cible_pomme(cible, sizeof(cible)))
		return;
	if (lstat(al, &st) == 0 && !S_ISLNK(st.st_mode))
		return;	/* un vrai fichier de ce nom : on n'y touche pas */
	snprintf(d, sizeof(d), "%s", al);
	*strrchr(d, '/') = '\0';
	mkdir_p(d);
	if (strncmp(cible, vol, lv) == 0 && cible[lv] == '/')
		snprintf(lien, sizeof(lien), "../..%s", cible + lv);
	else
		snprintf(lien, sizeof(lien), "%s", cible);
	unlink(al);
	if (symlink(lien, al) == 0)
		printf("installateur : alias du menu Pomme %s -> %s\n", I.nom, lien);
}

static void
retirer_alias_pomme(void)
{
	char al[PATH_MAX];
	struct stat st;

	if (I.pomme[0] != '\0' && alias_pomme(al, sizeof(al)) && lstat(al, &st) == 0 &&
	    S_ISLNK(st.st_mode))
		unlink(al);
}

/* le Control Strip relit ses modules (un module posé ou retiré dans
 * « System/Control Strip Modules ») : il a laissé son pid */
static void
relire_control_strip(void)
{
	char pf[PATH_MAX];
	const char *h = getenv("HOME");
	FILE *f;
	long pid = 0;

	snprintf(pf, sizeof(pf), "%s/.cache/platine-strip.pid", h != NULL ? h : "/tmp");
	if ((f = fopen(pf, "r")) == NULL)
		return;
	if (fscanf(f, "%ld", &pid) == 1 && pid > 1)
		kill((pid_t)pid, SIGUSR1);
	fclose(f);
}

static int
mode_suppression(void)
{
	return strcmp(I.mode->text, "Désinstallation") == 0;
}

static int
mode_personnalise(void)
{
	return strcmp(I.mode->text, "Installation personnalisée") == 0;
}

static int
choisi(const Composant *c)
{
	if (mode_personnalise())
		return c->case_ != NULL && c->case_->checked;
	return mode_suppression() || c->standard;
}

/* --- exécution de tar, ligne par ligne --- */

static int
lancer(char *const argv[], FILE **sortie, pid_t *pid)
{
	int t[2];

	if (pipe(t) != 0)
		return 0;
	*pid = fork();
	if (*pid < 0)
		return 0;
	if (*pid == 0) {
		dup2(t[1], 1);
		dup2(t[1], 2);
		close(t[0]);
		close(t[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(t[1]);
	*sortie = fdopen(t[0], "r");
	return *sortie != NULL;
}

/* argv : préfixe puis les chemins des composants choisis */
static int
argv_composants(char **argv, int n, int max, char *tampon, size_t tsz)
{
	int i;
	size_t pos = 0;

	for (i = 0; i < I.ncomp; i++) {
		char *p;

		if (!choisi(&I.comp[i]))
			continue;
		for (p = I.comp[i].chemins; *p != '\0' && n < max - 1;) {
			size_t l = strcspn(p, ":");

			if (pos + l + 1 > tsz)
				break;
			memcpy(tampon + pos, p, l);
			tampon[pos + l] = '\0';
			argv[n++] = tampon + pos;
			pos += l + 1;
			p += l;
			if (*p == ':')
				p++;
		}
	}
	argv[n] = NULL;
	return n;
}

/* --- fenêtre de progression --- */

static void
arreter_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	(void)user;
	I.arret = 1;
}

static void
progression_ouvrir(const char *titre)
{
	I.prog = ptk_window_new(&I.app, titre, 190, 170, 420, 118);
	ptk_window_set_menu(I.prog, "Fichier|~Quitter\t⌘Q");
	I.etat = ptk_control_add(I.prog, PTK_STATIC, 20, 16, 380, 16, "");
	I.barre = ptk_control_add(I.prog, PTK_PROGRESS, 20, 44, 380, 14, "");
	ptk_control_set_value(I.barre, 0);
	I.arreter = ptk_control_add(I.prog, PTK_BUTTON, 318, 78, 82, 20, "Arrêter");
	ptk_control_set_action(I.arreter, arreter_action, NULL);
	ptk_window_show(I.prog);
	ptk_app_traiter(&I.app);
}

static void
progression(int fait, int total, const char *texte)
{
	char t[300];

	ptk_control_set_value(I.barre, total > 0 ? fait * 100 / total : 0);
	snprintf(t, sizeof(t), "%s", texte);
	ptk_control_set_text(I.etat, t);
	ptk_window_invalidate(I.prog);
	ptk_app_traiter(&I.app);
}

static void
progression_fermer(void)
{
	if (I.prog != NULL) {
		ptk_window_close(I.prog);
		I.prog = NULL;
	}
	/* laisser arriver l'Expose de la fenêtre découverte avant l'alerte
	 * modale qui suit, sinon la place de la progression reste blanche */
	XSync(I.app.dpy, False);
	usleep(80000);
	ptk_app_traiter(&I.app);
}

/* --- installer --- */

static int
compter(const char *tmp)
{
	char *argv[64], tampon[2048];
	FILE *f;
	pid_t pid;
	char l[PATH_MAX];
	int n = 0, st;

	argv[0] = "tar";
	argv[1] = "-tf";
	argv[2] = (char *)tmp;
	argv_composants(argv, 3, 64, tampon, sizeof(tampon));
	if (!lancer(argv, &f, &pid))
		return 0;
	while (fgets(l, sizeof(l), f) != NULL)
		n++;
	fclose(f);
	waitpid(pid, &st, 0);
	return n;
}

static int
mkdir_p(const char *d)
{
	char t[PATH_MAX], *p;

	snprintf(t, sizeof(t), "%s", d);
	for (p = t + 1; *p != '\0'; p++)
		if (*p == '/') {
			*p = '\0';
			if (mkdir(t, 0755) != 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	return mkdir(t, 0755) == 0 || errno == EEXIST ? 0 : -1;
}

static int
installer(void)
{
	char dest[PATH_MAX], tmp[PATH_MAX], titre[128], l[PATH_MAX];
	char *argv[64], tampon[2048];
	FILE *f;
	pid_t pid;
	int total, fait = 0, st, i;
	struct stat s;

	destination(dest, sizeof(dest));
	if (dest[0] == '\0')
		return 0;
	/* un élément du même nom existe déjà : comme VISE, demander */
	for (i = 0; i < I.ncomp; i++) {
		char ch[PATH_MAX], cd[PATH_MAX];

		if (!choisi(&I.comp[i]))
			continue;
		comp_dest(&I.comp[i], cd, sizeof(cd));
		if (snprintf(ch, sizeof(ch), "%s/%.*s", cd,
		    (int)strcspn(I.comp[i].chemins, ":"), I.comp[i].chemins) >=
		    (int)sizeof(ch))
			continue;
		if (lstat(ch, &s) == 0) {
			char m[300];

			snprintf(m, sizeof(m), "Un élément nommé « %s » existe déjà à cet endroit.",
			    strrchr(ch, '/') + 1);
			if (ptk_alert(I.fen, m, "Voulez-vous le remplacer par celui de l'installation ?",
			    "Annuler|Remplacer") != 1)
				return 0;
			break;
		}
	}
	for (i = 0; i < I.ncomp; i++) {
		char cd[PATH_MAX];

		if (!choisi(&I.comp[i]))
			continue;
		comp_dest(&I.comp[i], cd, sizeof(cd));
		if (cd[0] == '\0' || mkdir_p(cd) != 0) {
			ptk_alert(I.fen, "L'installation ne peut pas créer le dossier de destination.",
			    strerror(errno), "OK");
			return 0;
		}
	}
	if (!extraire_archive(tmp, sizeof(tmp))) {
		ptk_alert(I.fen, "L'installation a échoué.",
		    "Ce programme d'installation est endommagé.", "OK");
		return 0;
	}
	total = compter(tmp);
	snprintf(titre, sizeof(titre), "Installation %s%s", de(I.nom), I.nom);
	I.arret = 0;
	progression_ouvrir(titre);
	progression(0, total, "Préparation de l'installation…");
	st = 0;
	for (i = 0; i < I.ncomp && !I.arret; i++) {
		char cd[PATH_MAX], *p;
		int n = 5, s2;
		size_t pos = 0;

		if (!choisi(&I.comp[i]))
			continue;
		comp_dest(&I.comp[i], cd, sizeof(cd));
		/* -U : chaque fichier est supprimé avant d'être recréé, si bien
		 * qu'un programme en cours d'exécution se remplace aussi */
		argv[0] = "tar";
		argv[1] = "-xvUf";
		argv[2] = tmp;
		argv[3] = "-C";
		argv[4] = cd;
		for (p = I.comp[i].chemins; *p != '\0' && n < 63;) {
			size_t lc = strcspn(p, ":");

			if (pos + lc + 1 > sizeof(tampon))
				break;
			memcpy(tampon + pos, p, lc);
			tampon[pos + lc] = '\0';
			argv[n++] = tampon + pos;
			pos += lc + 1;
			p += lc;
			if (*p == ':')
				p++;
		}
		argv[n] = NULL;
		if (!lancer(argv, &f, &pid)) {
			progression_fermer();
			unlink(tmp);
			return 0;
		}
		while (fgets(l, sizeof(l), f) != NULL) {
			char t[PATH_MAX + 32], *e;

			l[strcspn(l, "\n")] = '\0';
			/* tar -v de NetBSD préfixe « x » */
			e = strncmp(l, "x ", 2) == 0 ? l + 2 : l;
			snprintf(t, sizeof(t), "Copie de « %s »", e);
			progression(++fait, total, t);
			if (I.arret) {
				kill(pid, SIGTERM);
				break;
			}
		}
		fclose(f);
		waitpid(pid, &s2, 0);
		if (!WIFEXITED(s2) || WEXITSTATUS(s2) != 0)
			st = s2 != 0 ? s2 : 1 << 8;
	}
	unlink(tmp);
	progression(total, total, "Installation terminée.");
	progression_fermer();
	if (I.arret) {
		ptk_alert(I.fen, "L'installation a été interrompue.",
		    "Les éléments déjà copiés sont restés en place.", "OK");
		return 0;
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		ptk_alert(I.fen, "L'installation a échoué.",
		    "Une erreur s'est produite pendant la copie des éléments.", "OK");
		return 0;
	}
	return 1;
}

static int
supprimer(void)
{
	char dest[PATH_MAX], titre[128];
	int i, n = 0, total = 0;

	destination(dest, sizeof(dest));
	for (i = 0; i < I.ncomp; i++)
		total++;
	snprintf(titre, sizeof(titre), "Désinstallation %s%s", de(I.nom), I.nom);
	progression_ouvrir(titre);
	for (i = 0; i < I.ncomp; i++) {
		char *p = I.comp[i].chemins, cd[PATH_MAX];

		comp_dest(&I.comp[i], cd, sizeof(cd));
		while (*p != '\0') {
			char ch[PATH_MAX], t[PATH_MAX + 32];
			size_t l = strcspn(p, ":");
			pid_t pid;
			int st;

			if (snprintf(ch, sizeof(ch), "%s/%.*s", cd, (int)l, p) >=
			    (int)sizeof(ch) || cd[0] == '\0')
				break;
			snprintf(t, sizeof(t), "Suppression de « %.*s »", (int)l, p);
			progression(n, total, t);
			pid = fork();
			if (pid == 0) {
				execlp("rm", "rm", "-rf", "--", ch, (char *)NULL);
				_exit(127);
			}
			if (pid > 0)
				waitpid(pid, &st, 0);
			p += l;
			if (*p == ':')
				p++;
		}
		n++;
	}
	if (I.dossier[0] != '\0')
		rmdir(dest);	/* le dossier de programme, s'il est vide */
	progression(total, total, "Désinstallation terminée.");
	progression_fermer();
	return 1;
}

/* --- fenêtre d'installation --- */

static void
dessin(PtkWindow *w, void *user)
{
	Ptk *p = w->p;
	char t[512], dest[PATH_MAX];
	int y = 58, i;
	const char *lieu;

	(void)user;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	/* comme la fenêtre d'Installer VISE (audit/macos9-ref/vise-3.png) : la
	 * description dans un cadre blanc, sous le menu local du mode */
	ptk_fill(p, w->surface, 16, 44, 448, I.pomme[0] ? 128 : 146, p->white);
	ptk_rect(p, w->surface, 16, 44, 448, I.pomme[0] ? 128 : 146,
	    w->active ? p->black : ptk_color(p, "#888888"));
	if (mode_personnalise()) {
		ptk_text(p, w->surface, p->font_menu, 26, y + 4,
		    "Choisissez les éléments à installer :", p->text);
	} else {
		snprintf(t, sizeof(t), mode_suppression() ?
		    "Cliquez sur Supprimer pour retirer du disque « Eee PC 701 » :" :
		    "Cliquez sur Installer pour placer sur le disque « Eee PC 701 » :");
		ptk_text(p, w->surface, p->font_menu, 26, y + 4, t, p->text);
		y += 22;
		for (i = 0; i < I.ncomp; i++)
			if (choisi(&I.comp[i])) {
				snprintf(t, sizeof(t), "•  %s", I.comp[i].nom);
				ptk_text(p, w->surface, p->font_menu, 34, y, t, p->text);
				ptk_text(p, w->surface, p->font_small, 50, y + 14,
				    I.comp[i].description, p->text);
				y += 30;
			}
	}
	/* lieu d'installation */
	lieu = I.lieu->text[0] ? I.lieu->text : I.lieu_def;
	destination(dest, sizeof(dest));
	/* dans le groupe « Lieu d'installation », sur deux lignes comme VISE */
	if (I.dossier[0] != '\0' ? snprintf(t, sizeof(t), "Le dossier « %s » sera %s",
	    I.dossier, mode_suppression() ? "retiré de" : "créé dans") >= (int)sizeof(t) :
	    snprintf(t, sizeof(t), "Les éléments seront %s",
	    mode_suppression() ? "retirés de" : "placés dans") >= (int)sizeof(t))
		t[sizeof(t) - 1] = '\0';
	ptk_text(p, w->surface, p->font_small, 30, 228, t, p->text);
	if (lieu[0] == '/')
		snprintf(t, sizeof(t), "« %s ».", lieu);
	else if (snprintf(t, sizeof(t), "« %s » du disque « Eee PC 701 ».", lieu) >=
	    (int)sizeof(t))
		t[sizeof(t) - 1] = '\0';
	ptk_text(p, w->surface, p->font_small, 30, 242, t, p->text);
}

static void
maj(PtkWindow *w)
{
	int i, perso = mode_personnalise();

	for (i = 0; i < I.ncomp; i++)
		if (I.comp[i].case_ != NULL)
			I.comp[i].case_->visible = perso;
	snprintf(I.installer->label, sizeof(I.installer->label), "%s",
	    mode_suppression() ? "Supprimer" : "Installer");
	if (I.case_pomme != NULL)
		I.case_pomme->visible = !mode_suppression();
	ptk_window_invalidate(w);
}

static void
choix_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)c;
	(void)user;
	maj(w);
}

/* le menu local du lieu : « Autre… » ouvre la zone de dialogue « Choisir un
 * dossier » de la Toolbox, comme Installer VISE ; un dossier du volume se
 * garde relatif (« System/Extensions »), un autre chemin est gardé
 * tel quel (installer sur un disque monté) */
static void
lieu_action(PtkWindow *w, PtkControl *c, void *user)
{
	char avant[256], ch[PATH_MAX];
	size_t lv;

	(void)user;
	snprintf(avant, sizeof(avant), "%s", I.lieu_avant);
	if (strcmp(c->text, "Autre…") != 0) {
		snprintf(I.lieu_avant, sizeof(I.lieu_avant), "%.*s",
		    (int)(sizeof(I.lieu_avant) - 1), c->text);
		maj(w);
		return;
	}
	if (ptk_choisir_dossier(&I.app,
	    "Choisissez le dossier où installer :", NULL, ch, sizeof(ch))) {
		lv = strlen(ptk_volume());
		if (strncmp(ch, ptk_volume(), lv) == 0)
			memmove(ch, ch + lv + (ch[lv] == '/' ? 1 : 0), strlen(ch) - lv + 1);
		if (ch[0] == '\0')
			snprintf(ch, sizeof(ch), "%s", I.lieu_def);
		snprintf(c->text, sizeof(c->text), "%s", ch);
		snprintf(I.lieu_avant, sizeof(I.lieu_avant), "%s", ch);
	} else
		ptk_control_set_text(c, avant[0] != '\0' ? avant : I.lieu_def);
	maj(w);
}

static void
quitter_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	(void)user;
	ptk_app_quit(&I.app);
}

static void
installer_action(PtkWindow *w, PtkControl *c, void *user)
{
	int ok;
	char d[600];

	(void)c;
	(void)user;
	ok = mode_suppression() ? supprimer() : installer();
	if (!ok)
		return;
	if (mode_suppression())
		retirer_alias_pomme();
	else
		poser_alias_pomme();
	relire_control_strip();
	/* là où VISE demandait de redémarrer, le programme installé démarre */
	if (!mode_suppression() && I.apres[0] != '\0') {
		char ap[PATH_MAX];

		if ((size_t)snprintf(ap, sizeof(ap), "%s/%s", ptk_volume(), I.apres) < sizeof(ap) &&
		    access(ap, X_OK) == 0 && fork() == 0) {
			setsid();
			execl(ap, ap, (char *)NULL);
			_exit(127);
		}
	}
	if (mode_suppression())
		snprintf(d, sizeof(d), "%s a été retiré du disque. Pour faire d'autres "
		    "opérations, cliquez sur Continuer.", I.nom);
	else if (I.apres[0] != '\0')
		snprintf(d, sizeof(d), "%s est installé et démarré : inutile de "
		    "redémarrer. Si vous avez terminé, cliquez sur Quitter ; pour "
		    "faire d'autres installations, cliquez sur Continuer.", I.nom);
	else
		snprintf(d, sizeof(d), "Si vous avez terminé, cliquez sur Quitter pour "
		    "fermer le programme d'installation. Pour faire d'autres "
		    "installations, cliquez sur Continuer.");
	printf("installateur : %s %s\n", mode_suppression() ? "suppression" : "installation", I.nom);
	fflush(stdout);
	if (ptk_alert(w, mode_suppression() ? "La désinstallation a réussi." :
	    "L'installation a réussi.", d, "Continuer|Quitter") == 1)
		ptk_app_quit(&I.app);
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	(void)user;
	if (strcmp(cmd, "Quitter") == 0 || strcmp(cmd, "Fermer") == 0)
		ptk_app_quit(&I.app);
	else if (strcmp(cmd, "À propos de l'Installateur") == 0)
		ptk_alert(w, "Installateur Platine",
		    "Installe les programmes dans le dossier Applications, comme "
		    "Installer VISE sous Mac OS 9.", "OK");
}

/* --- Lisez-moi --- */

static int lu;

static void
continuer_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)c;
	(void)user;
	lu = 1;
	ptk_window_close(w);
}

static void lisez_moi_texte(const char *membre, const char *titre, int licence);

static void
refuser_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	(void)user;
	ptk_app_quit(&I.app);
}

static void
lisez_moi(void)
{
	if (I.lisezmoi[0] != '\0')
		lisez_moi_texte(I.lisezmoi, I.lisezmoi, 0);
	if (I.app.running && I.licence[0] != '\0')
		lisez_moi_texte(I.licence, I.licence, 1);
}

static void
lisez_moi_texte(const char *membre, const char *titre_f, int licence)
{
	char tmp[PATH_MAX], titre[300], *texte;
	PtkWindow *w;
	PtkControl *t, *b;
	FILE *f;
	pid_t pid;
	char *argv[] = { "tar", "-xOf", tmp, (char *)membre, NULL };
	size_t n = 0;

	lu = 0;
	if (!extraire_archive(tmp, sizeof(tmp)))
		return;
	texte = calloc(1, PTK_TEXT_MAX);
	if (texte != NULL && lancer(argv, &f, &pid)) {
		n = fread(texte, 1, PTK_TEXT_MAX - 1, f);
		fclose(f);
		waitpid(pid, NULL, 0);
	}
	unlink(tmp);
	if (texte == NULL)
		return;
	texte[n] = '\0';
	snprintf(titre, sizeof(titre), "%s", titre_f);
	w = ptk_window_new(&I.app, titre, 150, 60, 500, 360);
	ptk_window_set_menu(w, "Fichier|Quitter\t⌘Q");
	ptk_window_set_command(w, commande, NULL);
	t = ptk_control_add(w, PTK_TEXTAREA, 16, 16, 468, 292, "");
	ptk_control_set_text(t, texte);
	if (licence) {
		/* licence : Refuser quitte, Accepter continue (écran de VISE) */
		b = ptk_control_add(w, PTK_BUTTON, 288, 324, 90, 20, "Refuser");
		ptk_control_set_action(b, refuser_action, NULL);
		b = ptk_control_add(w, PTK_DEFAULT_BUTTON, 392, 324, 90, 20, "Accepter");
	} else
		b = ptk_control_add(w, PTK_DEFAULT_BUTTON, 392, 324, 90, 20, "Continuer");
	ptk_control_set_action(b, continuer_action, NULL);
	free(texte);
	I.app.quit_on_last_close = 0;
	ptk_window_show(w);
	while (!lu && I.app.running) {
		ptk_app_traiter(&I.app);
		usleep(20000);
	}
}

int
main(int argc, char **argv)
{
	char titre[128];
	int i, y;
	ssize_t n;

	setvbuf(stdout, NULL, _IOLBF, 0);
	/* le programme d'installation se lit lui-même ; un argument désigne un
	 * autre programme d'installation (tests) */
	if (argc > 1)
		snprintf(I.moi, sizeof(I.moi), "%s", argv[1]);
	else if ((n = readlink("/proc/self/exe", I.moi, sizeof(I.moi) - 1)) > 0)
		I.moi[n] = '\0';
	else if (getenv("PLATINE_DOCUMENT") != NULL)
		snprintf(I.moi, sizeof(I.moi), "%s", getenv("PLATINE_DOCUMENT"));
	else
		snprintf(I.moi, sizeof(I.moi), "%s", argv[0]);
	if (ptk_app_init(&I.app, "Installateur", "application") != 0)
		return 1;
	if (!charger()) {
		PtkWindow *w = ptk_window_new(&I.app, "Installateur", 200, 150, 300, 100);

		ptk_alert(w, "Ce programme d'installation est incomplet.",
		    "Il ne contient rien à installer.", "OK");
		return 1;
	}
	lisez_moi();
	if (!I.app.running)
		return 0;
	I.app.quit_on_last_close = 1;
	snprintf(titre, sizeof(titre), "Installation %s%s", de(I.nom), I.nom);
	I.fen = ptk_window_new(&I.app, titre, 160, 70, 480, 320);
	ptk_window_set_menu(I.fen, "Fichier|Quitter\t⌘Q\nAide|À propos de l'Installateur");
	ptk_window_set_draw(I.fen, dessin, NULL);
	ptk_window_set_command(I.fen, commande, NULL);
	I.mode = ptk_control_add(I.fen, PTK_POPUP, 16, 14, 220, 20, "");
	ptk_control_set_items(I.mode,
	    "Installation standard|Installation personnalisée|Désinstallation");
	ptk_control_set_text(I.mode, "Installation standard");
	ptk_control_set_action(I.mode, choix_action, NULL);
	if (I.version[0] != '\0') {
		char v[100];

		snprintf(v, sizeof(v), "Version %s", I.version);
		ptk_control_add(I.fen, PTK_STATIC, 252, 16, 210, 16, v);
	}
	for (i = 0, y = 86; i < I.ncomp; i++, y += 24) {
		Composant *c = &I.comp[i];

		c->case_ = ptk_control_add(I.fen, PTK_CHECKBOX, 34, y, 400, 14, c->nom);
		c->case_->checked = c->standard;
		c->case_->visible = 0;
		ptk_control_set_action(c->case_, choix_action, NULL);
	}
	if (I.pomme[0] != '\0') {
		/* règle D8 : un programme vit aussi au menu Pomme */
		I.case_pomme = ptk_control_add(I.fen, PTK_CHECKBOX, 18, 180, 300, 14,
		    "Ajouter au menu Pomme");
		I.case_pomme->checked = 1;
	}
	ptk_control_add(I.fen, PTK_GROUPE, 16, 206, 330, 100, "Lieu d'installation");
	ptk_control_add(I.fen, PTK_STATIC, 30, 266, 100, 20, "Installer dans :");
	I.lieu = ptk_control_add(I.fen, PTK_POPUP, 132, 264, 200, 20, "");
	ptk_control_set_items(I.lieu, LIEUX);
	ptk_control_set_text(I.lieu, I.lieu_def);
	snprintf(I.lieu_avant, sizeof(I.lieu_avant), "%s", I.lieu_def);
	ptk_control_set_action(I.lieu, lieu_action, NULL);
	{	/* un test peut poser le lieu « Autre… » du premier coup (le pointeur
		 * ne se prête pas toujours à une popup tenue) */
		const char *t = getenv("PLATINE_INSTALL_LIEU_TEST");

		if (t != NULL && t[0] != '\0') {
			ptk_control_set_text(I.lieu, t);
			snprintf(I.lieu_avant, sizeof(I.lieu_avant), "%s", t);
		}
	}
	I.quitter = ptk_control_add(I.fen, PTK_BUTTON, 364, 222, 100, 20, "Quitter");
	ptk_control_set_action(I.quitter, quitter_action, NULL);
	I.installer = ptk_control_add(I.fen, PTK_DEFAULT_BUTTON, 364, 264, 100, 20,
	    "Installer");
	ptk_control_set_action(I.installer, installer_action, NULL);
	maj(I.fen);
	ptk_window_show(I.fen);
	ptk_app_run(&I.app);
	return 0;
}
