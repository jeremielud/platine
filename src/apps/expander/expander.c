/*
 * platine-expander — StuffIt Expander, recréé d'après l'original d'Aladdin
 * Systems (ressources extraites : icône, types reconnus, comportement) :
 *
 * Il reçoit une archive (double-clic depuis le Finder, Fichier ▸ Ouvrir…,
 * glisser-déposer XDND sur la fenêtre) et la décompresse à côté d'elle :
 *   • .sit, .sitx, .7z, .rar, .zip — décodeur externe (unstuff, 7z, unzip) ;
 *   • .tar, .tgz, .tar.gz, .tar.bz2, .tbz2, .tar.xz, .txz — tar de la base ;
 *   • .gz, .bz2, .xz — gzip, bzip2, xz, chacun décompresse son fichier seul ;
 *   • .hqx (BinHex 4) et .bin (MacBinary III) décodés en C — et l'archive
 *     qu'ils contiennent (.sit surtout) est de nouveau expandée, comme
 *     l'Expander le fait d'origine.
 *
 * Un dossier est créé à côté de l'archive, au nom sans extensions ; les
 * fichiers seuls (gz, bz2, xz) donnent le fichier décompressé à côté. Les
 * éléments déjà là sont signalés (Remplacer | Annuler, comme Mac OS 9).
 *
 * Pendant l'expansion, une fenêtre « Expansion » montre les flèches de
 * recherche de la Toolbox (comme Sherlock) et le pôle de barbier.
 *
 * Tout ce qui n'est pas une archive (document, dossier) est compressé, comme
 * l'Expander de Mac OS 9 accompagné de DropStuff : « nom.tar.gz » à côté de
 * l'élément, par le tar de la base (que l'Expander rouvre). Un alias est
 * suivi : c'est son original qui est compressé, l'archive va à côté de l'alias.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

#define TYPES "sit sitx zip tgz tar gz bz2 xz hqx bin 7z rar"
PTK_IDENTITE("StuffIt Expander", "unsi", "app-expander", "5.5, Platine 2026", TYPES);

static struct {
	PtkApp app;
	PtkWindow *w, *prog;
	PtkControl *etat, *b_etat, *bi, *rech;
	PtkArgb *grande;
	Atom a_uri, a_finish, a_copy, a_sel, a_denter, a_dpos, a_dleave, a_ddrop,
	    a_status;
	Window source;
	int accepte, attend_sel;
} E;

/* ---- utilitaires ---- */

static void
nom_base(const char *chem, char *out, size_t n)
{
	const char *s = strrchr(chem, '/');

	snprintf(out, n, "%s", s ? s + 1 : chem);
}

static void
ext_de(const char *nom, char *out, size_t n)
{
	const char *e = strrchr(nom, '.'), *p;
	size_t l = 0;

	if (e == NULL || e == nom || e[1] == '\0') {
		out[0] = '\0';
		return;
	}
	for (p = e + 1; *p != '\0' && l + 1 < n; p++)
		out[l++] = (char)tolower((unsigned char)*p);
	out[l] = '\0';
}

static void
sans_ext(const char *nom, char *out, size_t n)
{
	const char *e = strrchr(nom, '.');
	size_t l;

	if (e == NULL || e == nom) {
		snprintf(out, n, "%s", nom);
		return;
	}
	l = (size_t)(e - nom) < n ? (size_t)(e - nom) : n - 1;
	memcpy(out, nom, l);
	out[l] = '\0';
}

static void
dossier_de(const char *chem, char *out, size_t n)
{
	const char *s = strrchr(chem, '/');
	size_t l;

	if (s == NULL) {
		snprintf(out, n, ".");
		return;
	}
	l = (size_t)(s - chem) < n - 1 ? (size_t)(s - chem) : n - 1;
	memcpy(out, chem, l);
	out[l] = '\0';
}

static int
finit_par(const char *s, const char *suffixe)
{
	size_t l = strlen(s), ls = strlen(suffixe);

	return l >= ls && strcmp(s + l - ls, suffixe) == 0;
}

/* cherche un programme dans le PATH ; /usr/pkg et /usr/local comptent aussi
 * (reçu du Lecteur QuickTime, pour pkgsrc sur le 701 NetBSD) */
static int
outil_trouve(const char *candidat, char *out, size_t n)
{
	char bases[6144];
	const char *p = getenv("PATH"), *d;
	struct stat st;

	snprintf(bases, sizeof(bases), "%s:%s:%s:%s:%s", p ? p : "",
	    "/usr/pkg/bin", "/usr/local/bin", "/usr/bin", "/bin");
	d = bases;
	while (1) {
		const char *f = strchr(d, ':');
		size_t lg = f != NULL ? (size_t)(f - d) : strlen(d);
		char essai[2048];

		if (lg > 0 && lg + strlen(candidat) + 2 < sizeof(essai)) {
			memcpy(essai, d, lg);
			essai[lg] = '\0';
			strncat(essai, "/", sizeof(essai) - strlen(essai) - 1);
			strncat(essai, candidat, sizeof(essai) - strlen(essai) - 1);
			if (stat(essai, &st) == 0 && S_ISREG(st.st_mode) &&
			    access(essai, X_OK) == 0) {
				snprintf(out, n, "%s", essai);
				return 1;
			}
		}
		if (f == NULL)
			break;
		d = f + 1;
	}
	return 0;
}

static int
chercher_outil(const char *const cand[], char *trouve, size_t n)
{
	size_t i;

	for (i = 0; cand[i] != NULL; i++)
		if (outil_trouve(cand[i], trouve, n))
			return (int)i;
	return -1;
}

static void
ecrire_fichier(const char *chem, const unsigned char *d, size_t n)
{
	int f = open(chem, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ssize_t r;

	if (f < 0)
		return;
	while (n > 0) {
		r = write(f, d, n);
		if (r <= 0)
			break;
		d += (size_t)r;
		n -= (size_t)r;
	}
	close(f);
}

static int
attendre_fils(pid_t pid)
{
	int st;

	if (pid < 0)
		return -1;
	while (waitpid(pid, &st, WNOHANG) == 0) {
		usleep(40000);
		ptk_app_traiter(&E.app);
	}
	return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* fork + exec ; l'enfant tourne dans « dossier » : les outils décompressent
 * relativement à leur répertoire (pas besoin de -C aux vieilles commandes) */
static pid_t
lancer(char *const argv[], const char *dossier)
{
	pid_t pid = fork();

	if (pid != 0)
		return pid;
	if (dossier != NULL && chdir(dossier) != 0)
		_exit(126);
	execvp(argv[0], argv);
	_exit(127);
}

static int
decompresser_seul(const char *outil, const char *src, const char *dest)
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || dup2(fd, 1) < 0)
			_exit(126);
		close(fd);
		execlp(outil, outil, "-dc", "--", src, (char *)NULL);
		_exit(127);
	}
	return attendre_fils(pid);
}

/* ---- décodeurs Mac en C : BinHex 4 (.hqx) et MacBinary III (.bin) ---- */

/* un fork Mac OS classique : nom, type, créateur, data fork, resource fork */
struct MFile {
	char nom[64];
	unsigned char type[4];
	unsigned char createur[4];
	unsigned char *data, *rsrc;
	size_t data_n, rsrc_n;
};

#define MB_ESCAPE 0x90	/* le marqueur de répétition de BinHex 4 */

static const char BX_ALPHABET[65] =
    "!\"#$%&'()*+,-0123456789@ABCDEFGHIJKLMNPQRSTUVXYZ[`abcdefhijklmpqr";

static unsigned char *
binhex_decode(const unsigned char *d, size_t n, size_t *out_n)
{
	unsigned char map[256];
	unsigned char *t, *q;
	size_t i, m = 0;
	unsigned acc = 0;
	int bits = 0, l = 0, last = -1;

	memset(map, 0xff, sizeof(map));
	for (i = 0; BX_ALPHABET[i] != '\0'; i++)
		map[(unsigned char)BX_ALPHABET[i]] = (unsigned char)i;
	/* le bloc du fichier, entre le premier et le dernier « : » */
	while (m < n && d[m] != ':')
		m++;
	if (m >= n)
		return NULL;
	m++;
	t = malloc(n + 2);	/* décodé 3 pour 4 : jamais plus que l'entrée */
	if (t == NULL)
		return NULL;
	while (m < n && d[m] != ':') {
		unsigned char c = d[m++];

		if (c == '\n' || c == '\r')
			continue;
		if (map[c] == 0xff) {
			free(t);
			return NULL;
		}
		acc = (acc << 6) | map[c];
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			t[l++] = (unsigned char)((acc >> bits) & 0xff);
		}
	}
	/* RLE de BinHex : 0x90 suivi de n — n = 0 : le 0x90 est un caractère
	 * à part entière ; n > 0 : le dernier octet revient n fois de plus */
	q = malloc((size_t)l * 2 + 4);
	if (q == NULL) {
		free(t);
		return NULL;
	}
	*out_n = 0;
	i = 0;
	while (i < (size_t)l) {
		unsigned char c = t[i++];

		if (c != MB_ESCAPE) {
			q[(*out_n)++] = c;
			last = c;
			continue;
		}
		if (i < (size_t)l && t[i] == 0) {
			q[(*out_n)++] = MB_ESCAPE;
			last = MB_ESCAPE;
			i++;
			continue;
		}
		if (i < (size_t)l && last >= 0) {
			int k = t[i++];

			while (k-- > 0)
				q[(*out_n)++] = (unsigned char)last;
		}
	}
	free(t);
	return q;
}

/* un fork rassemblé depuis une trame macfile ; 0 si la trame est muette */
static int
macfile_parse(const unsigned char *b, size_t n, struct MFile *m)
{
	size_t nom_l, off;
	unsigned l;

	memset(m, 0, sizeof(*m));
	if (n < 22 || b[0] == 0 || b[0] > 63)
		return 0;
	nom_l = b[0] < sizeof(m->nom) ? b[0] : sizeof(m->nom) - 1;
	memcpy(m->nom, b + 1, nom_l);
	m->nom[nom_l] = '\0';
	off = 1 + nom_l;
	if (off + 20 > n)
		return 0;
	memcpy(m->type, b + off, 4);
	memcpy(m->createur, b + off + 4, 4);
	l = ((unsigned)b[off + 10] << 24) | ((unsigned)b[off + 11] << 16) |
	    ((unsigned)b[off + 12] << 8) | (unsigned)b[off + 13];
	if (off + 20 + (size_t)l > n)
		return 0;
	m->data_n = l;
	if (l > 0) {
		m->data = memcpy(malloc(l), b + off + 20, l);
		if (m->data == NULL)
			m->data_n = 0;
	}
	off += 20 + (size_t)l + 2;	/* + le CRC du data fork */
	if (off + 4 > n)
		return 1;	/* la data fork seule suffit (fin de trame) */
	l = ((unsigned)b[off] << 24) | ((unsigned)b[off + 1] << 16) |
	    ((unsigned)b[off + 2] << 8) | (unsigned)b[off + 3];
	if (off + 4 + (size_t)l > n)
		return 1;
	m->rsrc_n = l;
	if (l > 0 && (m->rsrc = memcpy(malloc(l), b + off + 4, l)) == NULL)
		m->rsrc_n = 0;
	return 1;
}

/* MacBinary III : en-tête unique de 128 octets, puis data fork, puis resource */
static int
macbinary_parse(const unsigned char *b, size_t n, struct MFile *m)
{
	size_t nom_l;

	memset(m, 0, sizeof(*m));
	if (n < 130 || b[0] != 0 || b[1] == 0 || b[82] != 0)
		return 0;
	nom_l = b[1] > 31 ? 31 : b[1];
	memcpy(m->nom, b + 2, nom_l);
	m->nom[nom_l] = '\0';
	memcpy(m->type, b + 65, 4);
	memcpy(m->createur, b + 69, 4);
	m->data_n = ((size_t)b[83] << 24) | ((size_t)b[84] << 16) |
	    ((size_t)b[85] << 8) | (size_t)b[86];
	m->rsrc_n = ((size_t)b[87] << 24) | ((size_t)b[88] << 16) |
	    ((size_t)b[89] << 8) | (size_t)b[90];
	if (128 + m->data_n > n ||
	    (m->rsrc_n != 0 && 128 + m->data_n + m->rsrc_n > n))
		return 0;
	if (m->data_n > 0) {
		m->data = memcpy(malloc(m->data_n), b + 128, m->data_n);
		if (m->data == NULL)
			m->data_n = 0;
	}
	if (m->rsrc_n > 0) {
		m->rsrc = memcpy(malloc(m->rsrc_n), b + 128 + m->data_n, m->rsrc_n);
		if (m->rsrc == NULL)
			m->rsrc_n = 0;
	}
	return 1;
}

static void
mfile_libere(struct MFile *m)
{
	free(m->data);
	free(m->rsrc);
}

/* cette extension est-elle une archive de l'Expander ? (le .bin porte le
 * .sit, le .hqx porte le .bin…) */
static int
est_archive(const char *nom)
{
	char ext[32];

	ext_de(nom, ext, sizeof(ext));
	return strcmp(ext, "sit") == 0 || strcmp(ext, "sitx") == 0 ||
	    strcmp(ext, "zip") == 0 || strcmp(ext, "tar") == 0 ||
	    strcmp(ext, "gz") == 0 || strcmp(ext, "bz2") == 0 ||
	    strcmp(ext, "xz") == 0 || strcmp(ext, "tgz") == 0 ||
	    strcmp(ext, "tbz2") == 0 || strcmp(ext, "txz") == 0 ||
	    strcmp(ext, "hqx") == 0 || strcmp(ext, "bin") == 0 ||
	    strcmp(ext, "7z") == 0 || strcmp(ext, "rar") == 0;
}

/* pose le fork décodé ; retour : le chemin de ce qui a été écrit (0 sinon) */
static void
poser_fork(const char *dossier, struct MFile *m, char *produit, size_t np)
{
	char nom[64], dest[PATH_MAX + 256], apart[PATH_MAX + 264];
	size_t i;

	produit[0] = '\0';
	for (i = 0; m->nom[i] != '\0' && i < sizeof(nom) - 1; i++)
		nom[i] = (unsigned char)m->nom[i] < 32 ? '_' : m->nom[i];
	nom[i] = '\0';
	if (nom[0] == '\0') {
		mfile_libere(m);
		return;
	}
	snprintf(dest, sizeof(dest), "%s/%s", dossier, nom);
	if (m->data_n > 0) {
		ecrire_fichier(dest, m->data, m->data_n);
		snprintf(produit, np, "%s", dest);
	} else if (m->rsrc_n > 0) {
		snprintf(apart, sizeof(apart), "%s.rsrc", dest);
		ecrire_fichier(apart, m->rsrc, m->rsrc_n);
	} else
		ecrire_fichier(dest, (const unsigned char *)"", 0);
	mfile_libere(m);
}

/* ---- la fenêtre de progression ---- */

static void
progression_ouvrir_titre(const char *titre, const char *nom)
{
	if (E.prog != NULL)
		return;
	E.prog = ptk_window_new(&E.app, titre, 210, 150, 372, 106);
	ptk_window_set_menu(E.prog, "Fichier|Quitter\t⌘Q");
	E.b_etat = ptk_control_add(E.prog, PTK_STATIC, 20, 14, 332, 16, "");
	E.rech = ptk_control_add(E.prog, PTK_RECHERCHE, 20, 40, 92, 16, "");
	E.bi = ptk_control_add(E.prog, PTK_BARBIER, 124, 40, 228, 16, "");
	ptk_control_set_text(E.b_etat, nom);
	ptk_window_show(E.prog);
	ptk_app_traiter(&E.app);
}

static void
progression_ouvrir(const char *nom)
{
	progression_ouvrir_titre("Expansion", nom);
}

static void
progression_etat(const char *t)
{
	if (E.prog != NULL) {
		ptk_control_set_text(E.b_etat, t);
		ptk_window_invalidate(E.prog);
	}
	ptk_app_traiter(&E.app);
}

static void
progression_fermer(void)
{
	if (E.prog != NULL) {
		ptk_window_close(E.prog);
		E.prog = NULL;
	}
	/* laisse arriver l'Expose de la zone découverte, comme l'Installateur */
	XSync(E.app.dpy, False);
	usleep(80000);
	ptk_app_traiter(&E.app);
}

/* ---- l'expansion ---- */

static int expande(const char *chem, int profondeur);

/* Remplacer | Annuler quand le réceptacle existe déjà (Mac OS 9) */
static int
confirmer(const char *dest)
{
	struct stat st;

	if (stat(dest, &st) != 0)
		return 1;
	return ptk_alert(E.w, "Un élément du même nom existe déjà.", dest,
	    "Annuler|Remplacer") == 1;
}

/* « tar.gz », « tgz »… : la famille tar, base décloisonnée (« x.tar » → « x ») */
static void
famille_tar(char *base, char *ext, size_t ne)
{
	size_t l;

	if (strcmp(ext, "gz") == 0 && finit_par(base, ".tar")) {
		l = strlen(base);
		base[l - 4] = '\0';
		snprintf(ext, ne, "tar");
	} else if (strcmp(ext, "bz2") == 0 && finit_par(base, ".tar")) {
		l = strlen(base);
		base[l - 4] = '\0';
		snprintf(ext, ne, "tar");
	} else if (strcmp(ext, "xz") == 0 && finit_par(base, ".tar")) {
		l = strlen(base);
		base[l - 4] = '\0';
		snprintf(ext, ne, "tar");
	} else if (strcmp(ext, "tgz") == 0 || strcmp(ext, "tbz2") == 0 ||
	    strcmp(ext, "txz") == 0) {
		snprintf(ext, ne, "tar");
	}
}

/* Compression (DropStuff) : « nom.tar.gz » à côté de l'élément déposé. Retour 1
 * si l'archive est faite. */
static int
compresse(const char *chem)
{
	char reel[PATH_MAX], parent[PATH_MAX], base[256], dossier[PATH_MAX];
	char nom[256], dest[PATH_MAX + 300], t[400];
	char *sl;
	int rc;

	if (realpath(chem, reel) == NULL) {
		ptk_alert(E.w, "Cet élément est introuvable.", chem, "OK");
		return 0;
	}
	/* l'original (un alias est suivi) : tar part de son dossier parent */
	snprintf(parent, sizeof(parent), "%s", reel);
	sl = strrchr(parent, '/');
	if (sl == NULL || sl[1] == '\0')
		return 0;
	snprintf(base, sizeof(base), "%s", sl + 1);
	if (sl == parent)
		sl[1] = '\0';
	else
		*sl = '\0';
	/* l'archive : à côté de l'élément déposé, sous son nom */
	nom_base(chem, nom, sizeof(nom));
	dossier_de(chem, dossier, sizeof(dossier));
	snprintf(dest, sizeof(dest), "%s/%s.tar.gz", dossier, nom);
	if (!confirmer(dest))
		return 0;
	progression_ouvrir_titre("Compression", nom);
	snprintf(t, sizeof(t), "Compression de « %s »…", nom);
	progression_etat(t);
	{
		char *argv[] = { "tar", "-czf", dest, base, NULL };

		rc = attendre_fils(lancer(argv, parent)) == 0;
	}
	if (!rc) {
		unlink(dest);
		progression_fermer();
		ptk_alert(E.w, "La compression a échoué.", chem, "OK");
		return 0;
	}
	snprintf(t, sizeof(t), "Compression de « %s » terminée.", nom);
	ptk_control_set_text(E.etat, t);
	ptk_window_invalidate(E.w);
	progression_fermer();
	return 1;
}

/* une archive reconnue ; retour 1 (expansion faite), 0 (rien à faire) */
static int
expande(const char *chem, int profondeur)
{
	char nom[256], ext[64], base[256], dossier[PATH_MAX];
	char dest[PATH_MAX + 256], outil[512], produit[PATH_MAX + 256];
	struct stat st;
	int rc = 0;

	if (profondeur > 4 || stat(chem, &st) != 0)
		return 0;
	/* déposé par l'utilisateur, un dossier ou un document qui n'est pas une
	 * archive est compressé (DropStuff) ; une archive est expandée */
	if (S_ISDIR(st.st_mode) || !est_archive(chem))
		return profondeur == 0 ? compresse(chem) : 0;
	nom_base(chem, nom, sizeof(nom));
	ext_de(nom, ext, sizeof(ext));
	sans_ext(nom, base, sizeof(base));
	dossier_de(chem, dossier, sizeof(dossier));
	if (ext[0] == '\0' || base[0] == '\0') {
		ptk_alert(E.w, "Ce fichier n'est pas une archive reconnue.", chem,
		    "OK");
		return 0;
	}
	famille_tar(base, ext, sizeof(ext));
	progression_ouvrir(nom);
	progression_etat(nom);
	produit[0] = '\0';

	if (strcmp(ext, "tar") == 0) {
		snprintf(dest, sizeof(dest), "%s/%s", dossier, base);
		if (confirmer(dest)) {
			if (mkdir(dest, 0755) != 0 && errno != EEXIST) {
				progression_etat("Le dossier ne peut pas être créé.");
				usleep(400000);
			} else {
				char *argv[] = { "tar", "-xf", (char *)chem, NULL };

				attendre_fils(lancer(argv, dest));
				rc = 1;
			}
		}
	} else if (strcmp(ext, "zip") == 0) {
		const char *const cand[] = { "unzip", "7z", "7zz", "7za", NULL };

		snprintf(dest, sizeof(dest), "%s/%s", dossier, base);
		if (confirmer(dest)) {
			mkdir(dest, 0755);
			if (chercher_outil(cand, outil, sizeof(outil)) < 0) {
				ptk_alert(E.w, "Décompresseur absent.",
				    "Les archives « zip » passent par unzip ou 7z.",
				    "OK");
			} else {
				char *argv[] = { outil, "-o", (char *)chem, NULL };

				attendre_fils(lancer(argv, dest));
				rc = 1;
			}
		}
	} else if (strcmp(ext, "7z") == 0 || strcmp(ext, "rar") == 0) {
		const char *const cand[] = { "7z", "7zz", "7za", "unrar", NULL };

		snprintf(dest, sizeof(dest), "%s/%s", dossier, base);
		if (confirmer(dest)) {
			mkdir(dest, 0755);
			if (chercher_outil(cand, outil, sizeof(outil)) < 0) {
				ptk_alert(E.w, "Décompresseur absent.",
				    "Les archives « 7z » et « rar » passent par "
				    "7z ou unrar.", "OK");
			} else {
				char *argv[] = { outil, "x", "-y", (char *)chem, NULL };

				attendre_fils(lancer(argv, dest));
				rc = 1;
			}
		}
	} else if (strcmp(ext, "sit") == 0 || strcmp(ext, "sitx") == 0) {
		const char *const cand[] = { "unstuff", "unsit", NULL };

		snprintf(dest, sizeof(dest), "%s/%s", dossier, base);
		if (confirmer(dest)) {
			mkdir(dest, 0755);
			if (chercher_outil(cand, outil, sizeof(outil)) < 0) {
				ptk_alert(E.w, "Décompresseur absent.",
				    "Les archives StuffIt passent par unstuff "
				    "(pkgsrc : archivers/unstuff).", "OK");
			} else {
				char *argv[3];

				argv[0] = outil;
				argv[1] = (char *)chem;
				argv[2] = NULL;
				attendre_fils(lancer(argv, dest));
				rc = 1;
			}
		}
	} else if (strcmp(ext, "gz") == 0 || strcmp(ext, "bz2") == 0 ||
	    strcmp(ext, "xz") == 0) {
		const char *o = strcmp(ext, "gz") == 0 ? "gzip" :
		    strcmp(ext, "bz2") == 0 ? "bzip2" : "xz";

		snprintf(dest, sizeof(dest), "%s/%s", dossier, base);
		if (confirmer(dest)) {
			if (decompresser_seul(o, chem, dest) == 0) {
				/* « x.tar.gz » : le fichier obtenu est un tar,
				 * expansé aussitôt puis retiré, comme celui du
				 * vrai Expander */
				if (finit_par(base, ".tar")) {
					expande(dest, profondeur + 1);
					unlink(dest);
				}
				rc = 1;
			}
		}
	} else if (strcmp(ext, "bin") == 0 || strcmp(ext, "hqx") == 0) {
		FILE *f = fopen(chem, "rb");
		unsigned char *d, *dec = NULL;
		size_t n;
		long lg;
		struct MFile m;

		if (f == NULL)
			goto fin;
		fseek(f, 0, SEEK_END);
		lg = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (lg <= 0 || lg > 64 * 1024 * 1024) {
			fclose(f);
			goto fin;
		}
		d = malloc((size_t)lg);
		if (d == NULL) {
			fclose(f);
			goto fin;
		}
		n = fread(d, 1, (size_t)lg, f);
		fclose(f);
		if (strcmp(ext, "hqx") == 0) {
			dec = binhex_decode(d, n, &n);
			if (dec == NULL || !macfile_parse(dec, n, &m)) {
				free(d);
				free(dec);
				ptk_alert(E.w, "Ce fichier n'est pas un BinHex.",
				    chem, "OK");
				goto fin;
			}
			free(dec);
		} else {
			if (!macbinary_parse(d, n, &m)) {
				free(d);
				ptk_alert(E.w,
				    "Ce fichier n'est pas un MacBinary.", chem,
				    "OK");
				goto fin;
			}
		}
		free(d);
		poser_fork(dossier, &m, produit, sizeof(produit));
		/* le fork posé : l'archive qu'il porte est expandée à son tour */
		if (produit[0] != '\0' && est_archive(produit))
			expande(produit, profondeur + 1);
		rc = 1;
	} else {
		ptk_alert(E.w, "Ce format n'est pas reconnu par l'Expander.", chem,
		    "OK");
	}

fin:
	if (rc) {
		char t[400];

		snprintf(t, sizeof(t), "Expansion de « %s » terminée.", nom);
		ptk_control_set_text(E.etat, t);
		ptk_window_invalidate(E.w);
	}
	progression_fermer();
	return rc;
}

/* ---- la fenêtre ---- */

static void
dessin(PtkWindow *w, void *user)
{
	Ptk *p = w->p;

	(void)user;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	ptk_text(p, w->surface, p->font_title, 74, 26, "StuffIt Expander", p->text);
	if (E.grande != NULL)
		ptk_draw_argb(p, w->surface, E.grande, (w->w - 32) / 2, 44, 0);
	ptk_text(p, w->surface, p->font_small, 52, 128,
	    "Archives expansées, autres éléments compressés.", p->text);
	p->inactif = !w->active;
	ptk_separateur(p, w->surface, 24, 140, w->w - 48, 2);
	p->inactif = 0;
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	(void)user;
	if (strncmp(cmd, "Ouvrir le document\t", 19) == 0)
		expande(cmd + 19, 0);
	else if (strcmp(cmd, "Ouvrir…") == 0) {
		char ch[1024], dossier[1024];

		ptk_perso("Documents", dossier, sizeof(dossier));
		if (ptk_ouvrir(&E.app, TYPES, dossier, ch, sizeof(ch)))
			expande(ch, 0);
	} else if (strcmp(cmd, "À propos de StuffIt Expander") == 0) {
		ptk_alert(w, "StuffIt Expander 5.5",
		    "Recréé pour Platine d'après l'original d'Aladdin Systems : "
		    "les archives se décompressent à côté d'elles-mêmes, les autres "
		    "éléments y sont compressés (DropStuff).", "OK");
	}
	ptk_window_invalidate(w);
}

/* ---- XDND : la fenêtre accepte les dépôts d'archives ---- */

static void
aware(Window win)
{
	long v = 5;

	XChangeProperty(E.app.dpy, win, XInternAtom(E.app.dpy, "XdndAware", False),
	    XA_ATOM, 32, PropModeReplace, (unsigned char *)&v, 1);
}

static void
message(Atom type, long l1, long l2, long l3, long l4)
{
	XEvent e;

	memset(&e, 0, sizeof(e));
	e.xclient.window = E.source;
	e.xclient.type = ClientMessage;
	e.xclient.message_type = type;
	e.xclient.format = 32;
	e.xclient.data.l[0] = (long)E.w->win;
	e.xclient.data.l[1] = l1;
	e.xclient.data.l[2] = l2;
	e.xclient.data.l[3] = l3;
	e.xclient.data.l[4] = l4;
	XSendEvent(E.app.dpy, E.source, False, 0, &e);
	XFlush(E.app.dpy);
}

/* les URI de la sélection XDND (« file://…%20… ») : une par ligne */
static int
uri_liste(unsigned char *d, unsigned long n, char liste[][PATH_MAX])
{
	unsigned long i = 0;
	int nb = 0;

	while (i < n && nb < 16) {
		char brut[PATH_MAX + 64];
		size_t l = 0;

		while (i < n && d[i] != '\n' && d[i] != '\r' &&
		    l < sizeof(brut) - 1)
			brut[l++] = (char)d[i++];
		brut[l] = '\0';
		while (i < n && (d[i] == '\n' || d[i] == '\r'))
			i++;
		if (l == 0)
			continue;
		if (strncmp(brut, "file://", 7) == 0) {
			/* décode %xx et retire file://[host] */
			char dec[PATH_MAX];
			size_t j = 7, o = 0;

			if (brut[j] != '/')
				while (j < l && brut[j] != '/')
					j++;
			while (j < l && o + 1 < PATH_MAX) {
				unsigned char c = (unsigned char)brut[j++];
				int h;

				if (c == '%' && sscanf(brut + j, "%2x", &h) == 1 &&
				    j + 2 <= l) {
					j += 2;
					dec[o++] = (char)h;
				} else
					dec[o++] = (char)c;
			}
			dec[o] = '\0';
			if (o > 0 && dec[0] == '/') {
				memcpy(liste[nb], dec, (size_t)o + 1);
				nb++;
			}
		} else if (brut[0] == '/') {
			size_t cp = l < PATH_MAX - 1 ? l : PATH_MAX - 1;

			memcpy(liste[nb], brut, cp);
			liste[nb][cp] = '\0';
			nb++;
		}
	}
	return nb;
}

static int
xdnd(PtkApp *app, XEvent *e, void *user)
{
	(void)app;
	(void)user;
	if (e->type == SelectionNotify) {
		unsigned long n, ap;
		Atom t;
		int f;
		unsigned char *d = NULL;

		if (!E.attend_sel || e->xselection.selection != E.a_sel)
			return 0;
		E.attend_sel = 0;
		if (XGetWindowProperty(app->dpy, E.w->win, E.a_uri, 0, 0x10000,
		    True, AnyPropertyType, &t, &f, &n, &ap, &d) == Success &&
		    d != NULL) {
			char liste[16][PATH_MAX];
			int nb = uri_liste(d, n, liste), i;

			for (i = 0; i < nb; i++)
				expande(liste[i], 0);
			XFree(d);
		}
		message(E.a_finish, 0, 0, 0, 0);
		E.source = None;
		return 1;
	}
	if (e->type != ClientMessage)
		return 0;
	{
		XClientMessageEvent *m = &e->xclient;

		if (E.w == NULL || m->window != E.w->win)
			return 0;
		if (m->message_type == E.a_denter) {
			E.source = (Window)m->data.l[0];
			E.accepte = 0;
			if ((m->data.l[1] & 1) != 0) {
				Atom ty;
				int fm;
				unsigned long nn, after;
				unsigned char *dd = NULL;

				if (XGetWindowProperty(app->dpy, E.source,
				    XInternAtom(app->dpy, "XdndTypeList", False),
				    0, 64, False, XA_ATOM, &ty, &fm, &nn, &after,
				    &dd) == Success && dd != NULL && fm == 32) {
					unsigned long j;

					for (j = 0; j < nn; j++)
						if ((Atom)((long *)dd)[j] == E.a_uri)
							E.accepte = 1;
				}
				if (dd != NULL)
					XFree(dd);
			} else {
				int j;

				for (j = 2; j < 5; j++)
					if ((Atom)m->data.l[j] == E.a_uri)
						E.accepte = 1;
			}
			if (!E.accepte)
				E.source = None;
		} else if (m->message_type == E.a_dleave) {
			E.source = None;
			E.accepte = 0;
		} else if (E.source != None && m->message_type == E.a_dpos) {
			message(E.a_status, E.accepte, 0, 0,
			    E.accepte ? (long)E.a_copy : 0);
		} else if (E.source != None && m->message_type == E.a_ddrop) {
			if (E.accepte) {
				E.attend_sel = 1;
				XConvertSelection(app->dpy, E.a_sel, E.a_uri,
				    E.a_uri, E.w->win, (Time)m->data.l[2]);
				XFlush(app->dpy);
			} else {
				message(E.a_finish, 0, 0, 0, 0);
				E.source = None;
			}
		} else
			return 0;
	}
	return 1;
}

int
main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	memset(&E, 0, sizeof(E));
	if (ptk_app_init(&E.app, "StuffIt Expander", "app-expander") != 0)
		return 1;
	E.grande = ptk_load_argb(&E.app.ptk, "app-expander-32");
	E.a_uri = XInternAtom(E.app.dpy, "text/uri-list", False);
	E.a_finish = XInternAtom(E.app.dpy, "XdndFinished", False);
	E.a_copy = XInternAtom(E.app.dpy, "XdndActionCopy", False);
	E.a_sel = XInternAtom(E.app.dpy, "XdndSelection", False);
	E.a_denter = XInternAtom(E.app.dpy, "XdndEnter", False);
	E.a_dpos = XInternAtom(E.app.dpy, "XdndPosition", False);
	E.a_dleave = XInternAtom(E.app.dpy, "XdndLeave", False);
	E.a_ddrop = XInternAtom(E.app.dpy, "XdndDrop", False);
	E.a_status = XInternAtom(E.app.dpy, "XdndStatus", False);
	E.w = ptk_window_new(&E.app, "StuffIt Expander", 180, 110, 396, 190);
	if (E.w == NULL)
		return 1;
	ptk_window_set_document(E.w, NULL);
	ptk_window_set_menu(E.w,
	    "Fichier|Ouvrir…\t⌘O|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Aide|À propos de StuffIt Expander");
	ptk_window_set_draw(E.w, dessin, NULL);
	ptk_window_set_command(E.w, commande, NULL);
	E.etat = ptk_control_add(E.w, PTK_STATIC, 24, 152, 348, 16, "Prêt.");
	E.app.xdnd = xdnd;
	aware(E.w->win);
	/* déjà ouvert : l'autre copie reçoit les archives et repasse devant */
	if (ptk_instance_unique(&E.app, E.w, "unsi",
	    argc > 1 ? argv[1] : E.app.document[0] ? E.app.document : NULL))
		return 0;
	ptk_window_show(E.w);
	XSync(E.app.dpy, False);
	if (argc > 1) {
		int i;

		for (i = 1; i < argc; i++)
			expande(argv[i], 0);
	} else if (E.app.document[0] != '\0')
		expande(E.app.document, 0);
	ptk_app_run(&E.app);
	return 0;
}
