#include <fontconfig/fontconfig.h>
#include <glob.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include "ptk_sys.h"
#include "ptk.h"

#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <X11/xpm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Enfants terminés ramassés sans zombie. Un SIGCHLD ignoré (SIG_IGN) s'hérite à
 * travers exec : tout programme lancé par le WM, le Finder ou le Control Strip ne
 * pouvait plus attendre ses propres enfants (waitpid échoue, l'Expander croyait
 * tar en échec). Un gestionnaire, lui, est remis par défaut à l'exec.
 */
static void
ptk_ramasse(int sig)
{
	int e = errno;

	(void)sig;
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
	errno = e;
}

void
ptk_ramasser_enfants(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = ptk_ramasse;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &sa, NULL);
}

PtkColor
ptk_color(Ptk *p, const char *name)
{
	/* XAllocColor est un aller-retour vers le serveur : mémoire des couleurs déjà vues */
	static struct { char n[24]; PtkColor c; } cache[256];
	static int nb;
	XColor c;
	PtkColor out;

	memset(&out, 0, sizeof(out));
	int i;

	for (i = 0; i < nb; i++)
		if (strcmp(cache[i].n, name) == 0)
			return cache[i].c;
	if (XParseColor(p->dpy, p->cmap, name, &c) &&
	    XAllocColor(p->dpy, p->cmap, &c)) {
		out.pixel = c.pixel;
		out.r = c.red;
		out.g = c.green;
		out.b = c.blue;
		if (nb < 256 && strlen(name) < sizeof(cache[0].n)) {
			strcpy(cache[nb].n, name);
			cache[nb].c = out;
			nb++;
		}
	} else {
		fprintf(stderr, "ptk: couleur introuvable: %s\n", name);
	}
	return out;
}

/*
 * Police bitmap de Mac OS 9 livrée avec Platine, chargée par son fichier : la règle système
 * « 70-no-bitmaps » de fontconfig écarte ces polices dès qu'on passe par la sélection.
 */
XftFont *
ptk_police_bitmap(Ptk *p, const char *fichier, int px)
{
	const char *rep = getenv("PLATINE_ASSETS");
	char c[PATH_MAX];
	FcPattern *pat;
	XftFont *f;

	snprintf(c, sizeof(c), "%s/polices/%s", rep != NULL ? rep : "/usr/local/share/platine/assets", fichier);
	if (access(c, R_OK) != 0)
		return NULL;
	pat = FcPatternCreate();
	FcPatternAddString(pat, FC_FILE, (const FcChar8 *)c);
	FcPatternAddInteger(pat, FC_INDEX, 0);
	FcPatternAddDouble(pat, FC_PIXEL_SIZE, px);
	FcPatternAddBool(pat, FC_ANTIALIAS, FcFalse);
	FcPatternAddBool(pat, FC_HINTING, FcFalse);
	f = XftFontOpenPattern(p->dpy, pat);
	if (f == NULL)
		FcPatternDestroy(pat);
	return f;
}

XftFont *
ptk_font(Ptk *p, const char *name, const char *fallback)
{
	XftFont *f = XftFontOpenName(p->dpy, p->screen, name);
	if (f == NULL && fallback != NULL)
		f = XftFontOpenName(p->dpy, p->screen, fallback);
	if (f == NULL)
		f = XftFontOpenName(p->dpy, p->screen, "fixed");
	return f;
}

static int
reglage_lire(const char *cle, char *out, size_t outsz, const char *def)
{
	const char *h = getenv("HOME");
	char path[PATH_MAX], line[256];
	size_t lc = strlen(cle);
	FILE *f;

	snprintf(out, outsz, "%s", def);
	snprintf(path, sizeof(path), "%s/.config/platine/reglages",
	    h != NULL ? h : ".");
	f = fopen(path, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof(line), f) != NULL)
		if (strncmp(line, cle, lc) == 0 && line[lc] == ' ') {
			line[strcspn(line, "\r\n")] = '\0';
			snprintf(out, outsz, "%s", line + lc + 1);
			fclose(f);
			return 1;
		}
	fclose(f);
	return 0;
}

static int
reglage_int(const char *cle, int def)
{
	char v[64];

	reglage_lire(cle, v, sizeof(v), "");
	return v[0] != '\0' ? atoi(v) : def;
}

/*
 * Fuseau horaire : la clé « fuseau » des réglages s'applique à chaud. Appelée avant chaque
 * localtime() affiché ; ne relit le fichier que si sa date de modification a changé.
 */
int
ptk_fuseau_valide(const char *z)
{
	char path[PATH_MAX];
	const char *p;

	if (z == NULL || *z == '\0' || *z == '/' || strstr(z, "..") != NULL)
		return 0;
	for (p = z; *p != '\0'; p++)
		if (!(isalnum((unsigned char)*p) || *p == '/' || *p == '_' || *p == '-' || *p == '+'))
			return 0;
	snprintf(path, sizeof(path), "/usr/share/zoneinfo/%s", z);
	return access(path, R_OK) == 0;
}

void
ptk_fuseau_suivre(void)
{
	static struct timespec vu;
	static ino_t vu_ino;
	static off_t vu_taille;
	static int deja;
	const char *h = getenv("HOME"), *tz;
	char path[PATH_MAX], z[128];
	struct stat st;

	snprintf(path, sizeof(path), "%s/.config/platine/reglages", h != NULL ? h : ".");
	if (stat(path, &st) != 0)
		return;
	/* le tableau réécrit par rename() : l'inode change même dans la même milliseconde */
	if (deja && st.st_ino == vu_ino && st.st_size == vu_taille &&
	    st.st_mtim.tv_sec == vu.tv_sec && st.st_mtim.tv_nsec == vu.tv_nsec)
		return;
	deja = 1;
	vu = st.st_mtim;
	vu_ino = st.st_ino;
	vu_taille = st.st_size;
	if (!reglage_lire("fuseau", z, sizeof(z), "") || !ptk_fuseau_valide(z))
		return;
	tz = getenv("TZ");
	if (tz != NULL && strcmp(tz, z) == 0)
		return;
	setenv("TZ", z, 1);
	tzset();
}

/*
 * Charge du processeur (%, depuis l'appel précédent ; -1 au premier) et mémoire occupée
 * par les programmes (pages anonymes, code, mémoire câblée ; le cache disque ne compte pas).
 */
#ifdef __NetBSD__
#include <sys/sysctl.h>
#include <sys/sched.h>
#include <uvm/uvm_extern.h>
#endif

int
ptk_cpu_charge(void)
{
	static unsigned long long av_total, av_idle;
	unsigned long long total = 0, idle = 0;
	int r = -1;
#ifdef __NetBSD__
	uint64_t cp[CPUSTATES];
	size_t l = sizeof(cp);
	int i;

	if (sysctlbyname("kern.cp_time", cp, &l, NULL, 0) != 0)
		return -1;
	for (i = 0; i < CPUSTATES; i++)
		total += cp[i];
	idle = cp[CP_IDLE];
#else
	unsigned long long v[8] = { 0 };
	FILE *f = fopen("/proc/stat", "r");
	int i;

	if (f == NULL)
		return -1;
	if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3],
	    &v[4], &v[5], &v[6], &v[7]) < 4) {
		fclose(f);
		return -1;
	}
	fclose(f);
	for (i = 0; i < 8; i++)
		total += v[i];
	idle = v[3] + v[4];
#endif
	if (av_total != 0 && total > av_total)
		r = (int)(100 - (idle - av_idle) * 100 / (total - av_total));
	av_total = total;
	av_idle = idle;
	return r < 0 ? (av_total ? 0 : -1) : r > 100 ? 100 : r;
}

void
ptk_memoire(long *utilise_ko, long *total_ko)
{
	*utilise_ko = *total_ko = 0;
#ifdef __NetBSD__
	{
		struct uvmexp_sysctl u;
		uint64_t phys = 0;
		size_t l = sizeof(u), lp = sizeof(phys);

		if (sysctlbyname("hw.physmem64", &phys, &lp, NULL, 0) == 0)
			*total_ko = (long)(phys / 1024);
		if (sysctlbyname("vm.uvmexp2", &u, &l, NULL, 0) == 0)
			*utilise_ko = (long)((u.anonpages + u.execpages + u.wired) * u.pagesize / 1024);
	}
#else
	{
		FILE *f = fopen("/proc/meminfo", "r");
		char ligne[128];
		long dispo = 0;

		if (f == NULL)
			return;
		while (fgets(ligne, sizeof(ligne), f) != NULL) {
			if (strncmp(ligne, "MemTotal:", 9) == 0)
				*total_ko = atol(ligne + 9);
			else if (strncmp(ligne, "MemAvailable:", 13) == 0)
				dispo = atol(ligne + 13);
		}
		fclose(f);
		*utilise_ko = *total_ko - dispo;
	}
#endif
}

/* mémoire installée arrondie comme sur la boîte (503 Mo visibles → 512 Mo) */
long
ptk_memoire_installee_mo(long total_ko)
{
	return (total_ko / 1024 + 63) / 64 * 64;
}

int
ptk_reglage_int(const char *cle, int def)
{
	return reglage_int(cle, def);
}

static XftFont *
police_ttf(Ptk *p, const char *famille, int px, int lissage, int italique)
{
	const char *a = getenv("PLATINE_ASSETS");
	char path[PATH_MAX], spec[128];
	FcPattern *pat;
	XftFont *f;

	snprintf(path, sizeof(path), "%s/polices/%s.ttf",
	    a != NULL ? a : "assets", famille);
	if (access(path, R_OK) != 0) {
		snprintf(spec, sizeof(spec), "%s:pixelsize=%d:antialias=%s",
		    famille, px, lissage ? "true" : "false");
		return ptk_font(p, spec, "sans");
	}
	pat = FcPatternCreate();
	if (pat == NULL)
		return NULL;
	FcPatternAddString(pat, FC_FILE, (const FcChar8 *)path);
	FcPatternAddInteger(pat, FC_INDEX, 0);
	FcPatternAddDouble(pat, FC_PIXEL_SIZE, px);
	FcPatternAddBool(pat, FC_ANTIALIAS, lissage ? FcTrue : FcFalse);
	FcPatternAddBool(pat, FC_HINTING, lissage ? FcTrue : FcFalse);
	if (italique) {
		FcMatrix m;

		FcMatrixInit(&m);
		FcMatrixShear(&m, 0, 0.22);
		FcPatternAddMatrix(pat, FC_MATRIX, &m);
	}
	f = XftFontOpenPattern(p->dpy, pat);
	if (f == NULL)
		FcPatternDestroy(pat);
	return f;
}

static XftFont *
police_choisie(Ptk *p, const char *famille, int px, const char *bdf,
    int lissage, int italique)
{
	XftFont *f = NULL;

	if (!lissage && bdf != NULL)
		f = ptk_police_bitmap(p, bdf, px);
	return f != NULL ? f : police_ttf(p, famille, px, lissage, italique);
}

static void
appliquer_couleurs(Ptk *p)
{
	static const struct { const char *nom, *couleur; } surb[] = {
		{ "Pourpre", "#333399" }, { "Lavande", "#6666cc" },
		{ "Bleu", "#0066cc" }, { "Vert", "#006633" },
		{ "Rose", "#cc3366" }, { "Orange", "#cc6633" },
		{ "Or", "#cc9933" }, { "Gris", "#666666" },
		{ NULL, NULL }
	};
	static const struct { const char *nom, *face, *accent; } var[] = {
		{ "Azul", "#d0d0e0", "#6666cc" },
		{ "Bondi", "#d0e0e0", "#339999" },
		{ "Cuivre", "#e0d0c0", "#996633" },
		{ "Cramoisi", "#e0c0c0", "#993333" },
		{ "Émeraude", "#d0e0d0", "#339966" },
		{ "Bleu de France", "#d0d8e8", "#3366cc" },
		{ "Or", "#e0dcc0", "#999933" },
		{ "Lierre", "#d0e0d0", "#669933" },
		{ "Prune", "#e0d0e0", "#996699" },
		{ "Rose", "#e0d0d8", "#cc6699" },
		{ "Saphir", "#d0d8e0", "#336699" },
		{ "Argent", "#dddddd", "#888888" },
		{ "Sarcelle", "#d0e0e0", "#338888" },
		{ "Turquoise", "#d0e8e0", "#339999" },
		{ NULL, NULL, NULL }
	};
	char nom[40];
	int i;

	reglage_lire("surbrillance", nom, sizeof(nom), "Pourpre");
	for (i = 0; surb[i].nom != NULL; i++)
		if (strcmp(nom, surb[i].nom) == 0) {
			p->selection = ptk_color(p, surb[i].couleur);
			break;
		}
	reglage_lire("variante", nom, sizeof(nom), "Lavande");
	for (i = 0; var[i].nom != NULL; i++)
		if (strcmp(nom, var[i].nom) == 0) {
			p->menu = ptk_color(p, var[i].face);
			p->titlebar = ptk_color(p, var[i].face);
			p->face = ptk_color(p, var[i].face);
			p->focus = ptk_color(p, var[i].accent);
			p->field_select = ptk_color(p, var[i].accent);
			break;
		}
}

int
ptk_init(Ptk *p, Display *dpy, int screen)
{
	memset(p, 0, sizeof(*p));
	p->dpy = dpy;
	p->screen = screen;
	p->root = RootWindow(dpy, screen);
	p->visual = DefaultVisual(dpy, screen);
	p->cmap = DefaultColormap(dpy, screen);
	p->gc = XCreateGC(dpy, p->root, 0, NULL);
	if (p->gc == None)
		return -1;

	p->window = ptk_color(p, "#eeeeee");
	p->menu = ptk_color(p, "#dddddd");
	p->titlebar = ptk_color(p, "#cccccc");
	p->text = ptk_color(p, "#000000");
	p->text_dim = ptk_color(p, "#808080");
	p->text_inactif = ptk_color(p, "#777777");
	p->light = ptk_color(p, "#ffffff");
	p->shadow = ptk_color(p, "#777777");
	p->dark_shadow = ptk_color(p, "#555555");
	p->selection = ptk_color(p, "#333399");
	p->selection_text = ptk_color(p, "#ffffff");
	p->face = ptk_color(p, "#dddddd");
	p->focus = ptk_color(p, "#6666cc");
	p->field_shadow = ptk_color(p, "#888888");
	p->field_select = ptk_color(p, "#ccccff");
	p->desktop = ptk_color(p, "#cfcfe1");
	p->desktop2 = ptk_color(p, "#b8b8d0");
	p->desktop_light = ptk_color(p, "#e4e4f0");
	p->desktop_dot = ptk_color(p, "#a8a8c8");
	p->folder = ptk_color(p, "#e8c878");
	p->folder_dark = ptk_color(p, "#b89040");
	p->paper = ptk_color(p, "#ffffff");
	p->paper_shadow = ptk_color(p, "#b0b0b0");
	p->disk = ptk_color(p, "#b8c8d8");
	p->trash = ptk_color(p, "#b8b8b8");
	p->trash_dark = ptk_color(p, "#707070");
	p->black = ptk_color(p, "#000000");
	p->white = ptk_color(p, "#ffffff");
	{
		static const char *g[9] = {
			"#222222", "#888888", "#999999", "#aaaaaa",
			"#bbbbbb", "#cccccc", "#dddddd", "#eeeeee",
			"#ffffff"
		};
		int i;

		for (i = 0; i < 9; i++)
			p->gray[i] = ptk_color(p, g[i]);
	}
	{
		int i;

		for (i = 0; i < 16; i++) {
			char hex[8];

			snprintf(hex, sizeof(hex), "#%02x%02x%02x",
			    i * 0x11, i * 0x11, i * 0x11);
			p->aa[i] = ptk_color(p, hex);
		}
	}

	appliquer_couleurs(p);
	{
		char grande[32], petite[32], vues[32], spec[96];
		int lissage = reglage_int("lissage", 0);

		reglage_lire("police_grande", grande, sizeof(grande), "Charcoal");
		reglage_lire("police_petite", petite, sizeof(petite), "Geneva");
		reglage_lire("police_vues", vues, sizeof(vues), "Geneva");
		p->font_small = police_choisie(p, petite, 10,
		    strcmp(petite, "Geneva") == 0 ? "geneva-10.bdf" : NULL,
		    lissage, 0);
		p->font_small_italic = police_choisie(p, petite, 10,
		    strcmp(petite, "Geneva") == 0 ? "geneva-10-italique.bdf" : NULL,
		    lissage, 1);
		p->font_views = police_choisie(p, vues, 10,
		    strcmp(vues, "Geneva") == 0 ? "geneva-10.bdf" : NULL,
		    lissage, 0);
		p->font_views_italic = police_choisie(p, vues, 10,
		    strcmp(vues, "Geneva") == 0 ? "geneva-10-italique.bdf" : NULL,
		    lissage, 1);
		p->font_menu = police_choisie(p, grande, 12,
		    strcmp(grande, "Charcoal") == 0 ? "charcoal-12.bdf" : NULL,
		    lissage, 0);
		p->font_menu_bold = police_choisie(p, grande, 12,
		    strcmp(grande, "Charcoal") == 0 ? "charcoal-12.bdf" : NULL,
		    lissage, 0);
		snprintf(spec, sizeof(spec),
		    "DejaVu Sans:pixelsize=9:antialias=%s",
		    lissage ? "true" : "false");
		p->font_shortcut = ptk_font(p, spec, "sans:pixelsize=9");
		p->font_title = police_choisie(p, grande, 12,
		    strcmp(grande, "Charcoal") == 0 ? "charcoal-12.bdf" : NULL,
		    lissage, 0);
	}
	if (p->font_small == NULL || p->font_views == NULL ||
	    p->font_menu == NULL || p->font_title == NULL)
		return -1;

	{
		const char *a = getenv("PLATINE_ASSETS");
		if (a == NULL || *a == '\0')
			a = "assets";
		snprintf(p->assets_dir, sizeof(p->assets_dir), "%s", a);
	}
	p->menubar_h = 20;
	p->titlebar_h = 22;
	return 0;
}

void
ptk_fini(Ptk *p)
{
	if (p->font_small)
		XftFontClose(p->dpy, p->font_small);
	if (p->font_small_italic)
		XftFontClose(p->dpy, p->font_small_italic);
	if (p->font_views)
		XftFontClose(p->dpy, p->font_views);
	if (p->font_views_italic)
		XftFontClose(p->dpy, p->font_views_italic);
	if (p->font_menu)
		XftFontClose(p->dpy, p->font_menu);
	if (p->font_menu_bold)
		XftFontClose(p->dpy, p->font_menu_bold);
	if (p->font_shortcut)
		XftFontClose(p->dpy, p->font_shortcut);
	if (p->font_title)
		XftFontClose(p->dpy, p->font_title);
	if (p->root_pattern)
		XFreePixmap(p->dpy, p->root_pattern);
	if (p->gc)
		XFreeGC(p->dpy, p->gc);
	memset(p, 0, sizeof(*p));
}

void
ptk_fill(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c)
{
	if (w <= 0 || h <= 0)
		return;
	XSetForeground(p->dpy, p->gc, c.pixel);
	XFillRectangle(p->dpy, d, p->gc, x, y, (unsigned)w, (unsigned)h);
}

void
ptk_rect(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c)
{
	if (w <= 0 || h <= 0)
		return;
	XSetForeground(p->dpy, p->gc, c.pixel);
	XDrawRectangle(p->dpy, d, p->gc, x, y, (unsigned)(w - 1),
	    (unsigned)(h - 1));
}

void
ptk_line(Ptk *p, Drawable d, int x1, int y1, int x2, int y2, PtkColor c)
{
	XSetForeground(p->dpy, p->gc, c.pixel);
	XDrawLine(p->dpy, d, p->gc, x1, y1, x2, y2);
}

void
ptk_clip(Ptk *p, int x, int y, int w, int h)
{
	p->clip.x = (short)x;
	p->clip.y = (short)y;
	p->clip.width = (unsigned short)(w > 0 ? w : 0);
	p->clip.height = (unsigned short)(h > 0 ? h : 0);
	p->clip_on = (w > 0 && h > 0);
	XSetClipRectangles(p->dpy, p->gc, 0, 0, &p->clip, 1, Unsorted);
}

void
ptk_unclip(Ptk *p)
{
	p->clip_on = 0;
	XSetClipMask(p->dpy, p->gc, None);
}

void
ptk_bevel(Ptk *p, Drawable d, int x, int y, int w, int h, int sunken)
{
	PtkColor tl = sunken ? p->shadow : p->light;
	PtkColor br = sunken ? p->light : p->shadow;
	PtkColor tl2 = sunken ? p->dark_shadow : p->white;
	PtkColor br2 = sunken ? p->white : p->dark_shadow;

	ptk_rect(p, d, x, y, w, h, p->black);
	ptk_line(p, d, x + 1, y + 1, x + w - 2, y + 1, tl);
	ptk_line(p, d, x + 1, y + 1, x + 1, y + h - 2, tl);
	ptk_line(p, d, x + 1, y + h - 2, x + w - 2, y + h - 2, br);
	ptk_line(p, d, x + w - 2, y + 1, x + w - 2, y + h - 2, br);
	if (w > 4 && h > 4) {
		ptk_line(p, d, x + 2, y + 2, x + w - 3, y + 2, tl2);
		ptk_line(p, d, x + 2, y + 2, x + 2, y + h - 3, tl2);
		ptk_line(p, d, x + 2, y + h - 3, x + w - 3, y + h - 3, br2);
		ptk_line(p, d, x + w - 3, y + 2, x + w - 3, y + h - 3, br2);
	}
}

/*
 * Cadre arrondi 1 px : les quatre pixels de coin sont coupés, comme les
 * boutons et cases de Mac OS 9.
 */
void
ptk_round_frame(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c)
{
	if (w <= 0 || h <= 0)
		return;
	/* coin coupé en diagonale sur 2 px, relevé sur les boutons de Disk First Aid
	 * (actif, enfoncé, inactif) : le bord commence à 2 px du coin, un point en
	 * (1, 1) ; les points (1, 0) et (0, 1) restent au fond */
	ptk_line(p, d, x + 2, y, x + w - 3, y, c);
	ptk_line(p, d, x + 2, y + h - 1, x + w - 3, y + h - 1, c);
	ptk_line(p, d, x, y + 2, x, y + h - 3, c);
	ptk_line(p, d, x + w - 1, y + 2, x + w - 1, y + h - 3, c);
	if (w > 2 && h > 2) {
		ptk_line(p, d, x + 1, y + 1, x + 1, y + 1, c);
		ptk_line(p, d, x + w - 2, y + 1, x + w - 2, y + 1, c);
		ptk_line(p, d, x + 1, y + h - 2, x + 1, y + h - 2, c);
		ptk_line(p, d, x + w - 2, y + h - 2, x + w - 2, y + h - 2, c);
	}
}

/*
 * Bouton poussoir Mac OS 9.2 (Appearance) :
 * face #ddd, bord noir arrondi, liseré blanc à 2 px en haut/gauche,
 * ombre #aaa + #777 à 2 px en bas/droite. Le bouton par défaut est
 * entouré d'un anneau noir de 3 px séparé par 1 px blanc.
 */
void
ptk_button(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *label, int pressed, int is_default, PtkColor text)
{
	int baseline;
	int tw;
	int i;

	/*
	 * Anneau du bouton par défaut, mesuré sur l'alerte Mac OS 9 :
	 * 1 px noir à 3 px du bouton, corridor de 2 px (#ddd puis #aaa en
	 * haut/gauche, #aaa puis #777 en bas/droite).
	 */
	PtkColor bord = p->inactif ? ptk_color(p, "#888888") : p->black;

	/* inactif : cadre et anneau #888, face unie sans biseau, texte #777,
	 * comme le menu local d'une fenêtre en arrière-plan */
	if (p->inactif) {
		text = p->text_inactif;
		pressed = 0;
	}
	if (is_default) {
		int rx = x - 3, ry = y - 3, rw = w + 6, rh = h + 6;

		/* corridor de 2 px entre l'anneau et le bouton */
		ptk_line(p, d, rx + 1, ry + 1, rx + rw - 2, ry + 1,
		    p->face);
		ptk_line(p, d, rx + 1, ry + 2, rx + rw - 2, ry + 2,
		    p->gray[3]);
		ptk_line(p, d, rx + 1, ry + 1, rx + 1, ry + rh - 2,
		    p->face);
		ptk_line(p, d, rx + 2, ry + 1, rx + 2, ry + rh - 2,
		    p->gray[3]);
		ptk_line(p, d, rx + 1, ry + rh - 3, rx + rw - 2,
		    ry + rh - 3, p->gray[3]);
		ptk_line(p, d, rx + 1, ry + rh - 2, rx + rw - 2,
		    ry + rh - 2, p->shadow);
		ptk_line(p, d, rx + rw - 3, ry + 1, rx + rw - 3,
		    ry + rh - 2, p->gray[3]);
		ptk_line(p, d, rx + rw - 2, ry + 1, rx + rw - 2,
		    ry + rh - 2, p->shadow);

		if (p->inactif)
			for (i = 1; i <= 2; i++)
				ptk_rect(p, d, rx + i, ry + i, rw - 2 * i, rh - 2 * i,
				    p->face);
		/* anneau noir 1 px, coins arrondis rayon 3 */
		ptk_line(p, d, rx + 3, ry, rx + rw - 4, ry, bord);
		ptk_line(p, d, rx + 3, ry + rh - 1, rx + rw - 4,
		    ry + rh - 1, bord);
		ptk_line(p, d, rx, ry + 3, rx, ry + rh - 4, bord);
		ptk_line(p, d, rx + rw - 1, ry + 3, rx + rw - 1,
		    ry + rh - 4, bord);
		ptk_line(p, d, rx + 2, ry + 1, rx + 2, ry + 1, bord);
		ptk_line(p, d, rx + 1, ry + 2, rx + 1, ry + 2, bord);
		ptk_line(p, d, rx + rw - 3, ry + 1, rx + rw - 3, ry + 1,
		    bord);
		ptk_line(p, d, rx + rw - 2, ry + 2, rx + rw - 2, ry + 2,
		    bord);
		ptk_line(p, d, rx + 2, ry + rh - 2, rx + 2, ry + rh - 2,
		    bord);
		ptk_line(p, d, rx + 1, ry + rh - 3, rx + 1, ry + rh - 3,
		    bord);
		ptk_line(p, d, rx + rw - 3, ry + rh - 2, rx + rw - 3,
		    ry + rh - 2, bord);
		ptk_line(p, d, rx + rw - 2, ry + rh - 3, rx + rw - 2,
		    ry + rh - 3, bord);
	}
	/* face : #ddd au repos, #666 enfoncé (relevé : Verify de Disk First Aid
	 * maintenu, audit/macos9-ref/presse/verify-enfonce.png) */
	ptk_fill(p, d, x + 1, y + 1, w - 2, h - 2,
	    pressed ? ptk_color(p, "#666666") : p->face);
	ptk_round_frame(p, d, x, y, w, h, bord);

	if (p->inactif) {
		/* face unie */
	} else if (!pressed) {
		/* premier anneau : face en haut/gauche, #777 en bas/droite ;
		 * second : blanc en haut/gauche, #aaa en bas/droite ; #bbb aux coins */
		ptk_line(p, d, x + 2, y + 2, x + w - 4, y + 2, p->white);
		ptk_line(p, d, x + 2, y + 2, x + 2, y + h - 4, p->white);
		ptk_line(p, d, x + 2, y + h - 3, x + w - 3, y + h - 3,
		    p->gray[3]);
		ptk_line(p, d, x + w - 3, y + 2, x + w - 3, y + h - 3,
		    p->gray[3]);
		ptk_line(p, d, x + 2, y + h - 2, x + w - 3, y + h - 2,
		    p->shadow);
		ptk_line(p, d, x + w - 2, y + 2, x + w - 2, y + h - 3,
		    p->shadow);
		ptk_fill(p, d, x + 2, y + 1, 1, 1, ptk_color(p, "#bbbbbb"));
		ptk_fill(p, d, x + 1, y + 2, 1, 1, ptk_color(p, "#bbbbbb"));
	} else {
		/* enfoncé : #44 puis #55 en haut/gauche, #88 puis #77 en bas/droite */
		PtkColor c44 = ptk_color(p, "#444444"), c55 = ptk_color(p, "#555555");
		PtkColor c77 = p->shadow, c88 = ptk_color(p, "#888888");

		ptk_line(p, d, x + 2, y + 1, x + w - 3, y + 1, c44);
		ptk_line(p, d, x + 1, y + 2, x + 1, y + h - 3, c44);
		ptk_line(p, d, x + 2, y + h - 2, x + w - 3, y + h - 2, c88);
		ptk_line(p, d, x + w - 2, y + 2, x + w - 2, y + h - 3, c88);
		ptk_line(p, d, x + 2, y + 2, x + w - 4, y + 2, c55);
		ptk_line(p, d, x + 2, y + 2, x + 2, y + h - 4, c55);
		ptk_line(p, d, x + 3, y + h - 3, x + w - 3, y + h - 3, c77);
		ptk_line(p, d, x + w - 3, y + 3, x + w - 3, y + h - 3, c77);
		text = p->white;	/* libellé blanc sur la face sombre */
	}

	/* le libellé ne bouge pas quand le bouton s'enfonce (relevé) */
	tw = ptk_text_width(p, p->font_menu, label);
	baseline = y + (h + p->font_menu->ascent -
	    p->font_menu->descent) / 2;
	ptk_text(p, d, p->font_menu, x + (w - tw) / 2, baseline, label, text);
}

/*
 * Champ texte : ombre #888 extérieure en haut/gauche, bord noir,
 * liseré blanc intérieur, fond blanc. Le focus ajoute un anneau
 * pervenche de 2 px à l'extérieur (couleur #6666cc).
 */
void
ptk_field(Ptk *p, Drawable d, int x, int y, int w, int h, int focused)
{
	if (p->inactif) {
		/* inactif : pas d'anneau, ombre #ddd et bord #555 (relevé) */
		ptk_line(p, d, x, y, x + w - 1, y, p->face);
		ptk_line(p, d, x, y, x, y + h - 1, p->face);
		ptk_line(p, d, x, y + h - 1, x + w - 1, y + h - 1, p->white);
		ptk_line(p, d, x + w - 1, y, x + w - 1, y + h - 1, p->white);
		ptk_rect(p, d, x + 1, y + 1, w - 2, h - 2, ptk_color(p, "#555555"));
		ptk_line(p, d, x + 2, y + 2, x + w - 3, y + 2, p->white);
		ptk_line(p, d, x + 2, y + 2, x + 2, y + h - 3, p->white);
		ptk_fill(p, d, x + 3, y + 3, w - 6, h - 6, p->white);
		return;
	}
	if (focused) {
		/* anneau de focus 2 px, à l'extérieur du champ */
		ptk_rect(p, d, x - 2, y - 2, w + 4, h + 4, p->focus);
		ptk_rect(p, d, x - 1, y - 1, w + 2, h + 2, p->focus);
	} else {
		/* biseau extérieur : ombre en haut/gauche, lumière en bas */
		ptk_line(p, d, x, y, x + w - 1, y, p->field_shadow);
		ptk_line(p, d, x, y, x, y + h - 1, p->field_shadow);
		ptk_line(p, d, x, y + h - 1, x + w - 1, y + h - 1,
		    p->white);
		ptk_line(p, d, x + w - 1, y, x + w - 1, y + h - 1,
		    p->white);
	}
	ptk_rect(p, d, x + 1, y + 1, w - 2, h - 2, p->black);
	ptk_line(p, d, x + 2, y + 2, x + w - 3, y + 2, p->white);
	ptk_line(p, d, x + 2, y + 2, x + 2, y + h - 3, p->white);
	ptk_fill(p, d, x + 3, y + 3, w - 6, h - 6, p->white);
}

/*
 * Cases à cocher et boutons radio : bitmaps 12×12 relevés au pixel sur la
 * VM (find-dialog.png, Sherlock 2). Les valeurs sont des niveaux de gris
 * 0x00..0xff, rendus par la palette aa[].
 */
static const unsigned char cb_off[12][12] = {
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  8,  0 },
	{  0, 13,  8,  8,  8,  8,  8,  8,  8,  8,  8,  0 },
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
};

static const unsigned char cb_on[12][12] = {
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 13,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13, 13,  0,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13, 13,  0,  0,  0 },
	{  0, 15, 13, 13, 13, 13, 13, 13,  0,  0,  5,  0 },
	{  0, 15,  0,  0, 13, 13, 13,  0,  0,  7,  7,  0 },
	{  0, 15, 13,  0,  0, 13,  0,  0,  7, 10,  8,  0 },
	{  0, 15, 13, 10,  0,  0,  0,  7, 10, 13,  8,  0 },
	{  0, 15, 13, 13, 10,  0,  7, 10, 13, 13,  8,  0 },
	{  0, 15, 13, 13, 13,  7, 10, 13, 13, 13,  8,  0 },
	{  0, 13,  8,  8,  8,  8,  8,  8,  8,  8,  8,  0 },
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
};

static const unsigned char radio_off[12][12] = {
	{ 12, 12, 12, 12,  4,  0,  0,  4, 12, 12, 12, 12 },
	{ 12, 12,  0,  5, 13, 13, 13, 11,  4,  0, 11, 12 },
	{ 12,  0, 11, 13, 14, 15, 15, 15, 13,  8,  0, 12 },
	{ 12,  5, 13, 14, 15, 15, 14, 14, 13, 11,  4, 12 },
	{  4, 13, 14, 15, 15, 14, 14, 13, 13, 11,  8,  4 },
	{  0, 13, 15, 15, 14, 14, 13, 13, 11, 11,  8,  0 },
	{  0, 13, 15, 14, 14, 13, 13, 11, 11, 10,  8,  0 },
	{  4, 11, 15, 14, 13, 13, 11, 11, 10, 10,  8,  4 },
	{ 12,  5, 13, 13, 13, 11, 11, 10, 10,  8,  4, 12 },
	{ 12,  0,  8, 11, 11, 11, 10, 10,  8,  8,  0, 12 },
	{ 12, 12,  0,  5,  8,  8,  8,  8,  4,  0, 11, 11 },
	{ 11, 12, 12, 12,  4,  0,  0,  4, 11, 12, 11, 12 },
};

static const unsigned char radio_on[12][12] = {
	{ 12, 12, 13, 12,  2,  0,  0,  2, 13, 12, 12, 12 },
	{ 12, 12,  2,  0,  4,  5,  5,  5,  4,  0, 12, 12 },
	{ 12,  2,  4,  5,  7,  7,  7,  8,  8,  9,  0, 12 },
	{ 12,  0,  5,  7,  0,  0,  0,  0,  9,  9,  4, 12 },
	{  2,  4,  7,  0,  0,  0,  0,  0,  0,  9, 11,  4 },
	{  0,  5,  7,  0,  0,  0,  0,  0,  0, 11, 11,  0 },
	{  0,  5,  7,  0,  0,  0,  0,  0,  0, 11, 13,  0 },
	{  2,  5,  8,  0,  0,  0,  0,  0,  0, 13, 15,  4 },
	{ 12,  2,  8,  9,  0,  0,  0,  0, 13, 15,  4, 12 },
	{ 12,  2,  9,  9,  9, 11, 11, 13, 15, 15,  0, 12 },
	{ 12, 12,  2,  4, 11, 11, 13, 15,  4,  0, 12, 12 },
	{ 12, 13, 12, 13,  4,  0,  0,  4, 12, 13, 12, 13 },
};

static void
draw_bitmap12(Ptk *p, Drawable d, int x, int y,
    const unsigned char bmp[12][12], int dim)
{
	int i, j;

	for (i = 0; i < 12; i++) {
		for (j = 0; j < 12; j++) {
			int k = bmp[i][j];

			/* inactif, relevé sur la case d'Apparence en
			 * arrière-plan : le noir (bord, coche, pastille) passe
			 * à #888, tout le reste à #ddd, sans anticrénelage ;
			 * le fond autour d'une radio (11, 12) reste le fond */
			if (dim == 1)
				k = k == 0 ? 8 : 13;
			else if (dim == 2)
				k = k <= 5 ? 8 : k >= 11 && (i < 2 || i > 9 ||
				    j < 2 || j > 9) ? k : 13;
			ptk_fill(p, d, x + j, y + i, 1, 1, p->aa[k]);
		}
	}
}

void
ptk_checkbox(Ptk *p, Drawable d, int x, int y, int checked)
{
	draw_bitmap12(p, d, x, y, checked ? cb_on : cb_off, p->inactif ? 1 : 0);
}

void
ptk_radio(Ptk *p, Drawable d, int x, int y, int checked)
{
	draw_bitmap12(p, d, x, y, checked ? radio_on : radio_off, p->inactif ? 2 : 0);
}

/* Menu déroulant : bord noir, liseré blanc, face #ddd, pomme de
 * commande à droite. */
void
ptk_popup(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *text, int open)
{
	ptk_popup_police(p, d, x, y, w, h, text, open, NULL);
}

/* Menu local Platinum, relevé pixel par pixel sur Apparence > Polices de Mac OS 9.2.1 (bouton de
 * 161×20) : cadre noir aux coins coupés (points #222 aux angles), partie texte biseautée blanc / #aa,
 * séparateur #aa à R-21, puis zone des flèches biseautée à part (blanc, #aa, #77). Menu ouvert :
 * le menu recouvre la partie texte et la zone des flèches s'enfonce (#44/#55, fond #66, flèches
 * blanches). Les flèches sont deux triangles de 4 lignes (base 7) centrés à R-11. */
void
ptk_popup_police(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *text, int open, XftFont *police)
{
	XftFont *f = police != NULL ? police : p->font_menu;
	int R = w - 1, B = h - 1, cy = y + h / 2, ax = x + R - 11, i;
	int baseline = y + (h + f->ascent - f->descent) / 2;
	PtkColor g22 = ptk_color(p, "#222222"), g44 = ptk_color(p, "#444444");
	PtkColor g55 = ptk_color(p, "#555555"), g66 = ptk_color(p, "#666666");
	PtkColor g77 = ptk_color(p, "#777777"), g88 = ptk_color(p, "#888888");
	PtkColor gaa = ptk_color(p, "#aaaaaa"), gbb = ptk_color(p, "#bbbbbb");
	PtkColor fl = open ? p->white : p->black;
	char vu[256];

	if (p->inactif) {
		/* relevé sur Apparence en arrière-plan : cadre #888 aux coins
		 * coupés, face #ddd unie, séparateur #aaa, flèches #888, texte
		 * #777 */
		ptk_fill(p, d, x + 1, y + 1, w - 2, h - 2, p->face);
		ptk_line(p, d, x + 2, y, x + R - 2, y, g88);
		ptk_line(p, d, x + 2, y + B, x + R - 2, y + B, g88);
		ptk_line(p, d, x, y + 2, x, y + B - 2, g88);
		ptk_line(p, d, x + R, y + 2, x + R, y + B - 2, g88);
		ptk_line(p, d, x + 1, y + 1, x + 1, y + 1, g88);
		ptk_line(p, d, x + 1, y + B - 1, x + 1, y + B - 1, g88);
		ptk_line(p, d, x + R - 1, y + 1, x + R - 1, y + 1, g88);
		ptk_line(p, d, x + R - 1, y + B - 1, x + R - 1, y + B - 1, g88);
		ptk_line(p, d, x + R - 21, y + 1, x + R - 21, y + B - 1, gaa);
		for (i = 0; i < 4; i++) {
			ptk_line(p, d, ax - i, cy - 5 + i, ax + i, cy - 5 + i, g88);
			ptk_line(p, d, ax - i, cy + 4 - i, ax + i, cy + 4 - i, g88);
		}
		ptk_fit_text(p, f, text, vu, sizeof(vu), R - 21 - 10);
		ptk_text(p, d, f, x + 7, baseline, vu, p->text_inactif);
		return;
	}
	ptk_fill(p, d, x + 1, y + 1, w - 2, h - 2, p->face);
	/* cadre */
	ptk_line(p, d, x + 3, y, x + R - 3, y, p->black);
	ptk_line(p, d, x + 3, y + B, x + R - 3, y + B, p->black);
	ptk_line(p, d, x, y + 3, x, y + B - 3, p->black);
	ptk_line(p, d, x + R, y + 3, x + R, y + B - 3, p->black);
	ptk_line(p, d, x + 1, y + 1, x + 1, y + 1, p->black);
	ptk_line(p, d, x + 1, y + B - 1, x + 1, y + B - 1, p->black);
	ptk_line(p, d, x + R - 1, y + 1, x + R - 1, y + 1, p->black);
	ptk_line(p, d, x + R - 1, y + B - 1, x + R - 1, y + B - 1, p->black);
	ptk_line(p, d, x + 2, y, x + 2, y, g22);
	ptk_line(p, d, x, y + 2, x, y + 2, g22);
	ptk_line(p, d, x + R - 2, y, x + R - 2, y, g22);
	ptk_line(p, d, x + R, y + 2, x + R, y + 2, g22);
	ptk_line(p, d, x + 2, y + B, x + 2, y + B, g22);
	ptk_line(p, d, x, y + B - 2, x, y + B - 2, g22);
	ptk_line(p, d, x + R - 2, y + B, x + R - 2, y + B, g22);
	ptk_line(p, d, x + R, y + B - 2, x + R, y + B - 2, g22);
	/* partie texte */
	ptk_line(p, d, x + 2, y + 1, x + R - 22, y + 1, p->white);
	ptk_line(p, d, x + 1, y + 2, x + 1, y + B - 2, p->white);
	ptk_line(p, d, x + 2, y + B - 1, x + R - 21, y + B - 1, gaa);
	ptk_line(p, d, x + R - 21, y + 2, x + R - 21, y + B - 2, gaa);
	ptk_line(p, d, x + R - 20, y + B - 1, x + R - 20, y + B - 1, gbb);
	/* zone des flèches */
	if (!open) {
		ptk_line(p, d, x + R - 2, y + 1, x + R - 2, y + 1, gbb);
		ptk_line(p, d, x + R - 19, y + 2, x + R - 3, y + 2, p->white);
		ptk_line(p, d, x + R - 19, y + 3, x + R - 19, y + B - 3, p->white);
		ptk_line(p, d, x + R - 18, y + B - 2, x + R - 3, y + B - 2, gaa);
		ptk_line(p, d, x + R - 19, y + B - 1, x + R - 2, y + B - 1, g77);
		ptk_line(p, d, x + R - 2, y + 3, x + R - 2, y + B - 3, gaa);
		ptk_line(p, d, x + R - 2, y + 2, x + R - 2, y + 2, p->face);
		ptk_line(p, d, x + R - 1, y + 3, x + R - 1, y + B - 2, g77);
		ptk_line(p, d, x + R - 2, y + B - 2, x + R - 2, y + B - 2, g77);
		ptk_line(p, d, x + R - 1, y + 2, x + R - 1, y + 2, gaa);
	} else {
		ptk_fill(p, d, x + R - 19, y + 1, 18, B - 1, g66);
		ptk_line(p, d, x + R - 19, y + 1, x + R - 2, y + 1, g44);
		ptk_line(p, d, x + R - 19, y + 2, x + R - 19, y + B - 2, g44);
		ptk_line(p, d, x + R - 18, y + 2, x + R - 3, y + 2, g55);
		ptk_line(p, d, x + R - 2, y + 2, x + R - 2, y + 2, g66);
		ptk_line(p, d, x + R - 2, y + 3, x + R - 2, y + B - 3, g77);
		ptk_line(p, d, x + R - 18, y + B - 2, x + R - 3, y + B - 2, g77);
		ptk_line(p, d, x + R - 2, y + B - 2, x + R - 2, y + B - 2, g88);
		ptk_line(p, d, x + R - 1, y + 2, x + R - 1, y + B - 2, g88);
		ptk_line(p, d, x + R - 19, y + B - 1, x + R - 2, y + B - 1, g88);
	}
	for (i = 0; i < 4; i++) {
		ptk_line(p, d, ax - i, cy - 5 + i, ax + i, cy - 5 + i, fl);
		ptk_line(p, d, ax - i, cy + 4 - i, ax + i, cy + 4 - i, fl);
	}
	/* comme OS 9 : la largeur du bouton est fixe, le texte trop long finit par « … » */
	ptk_fit_text(p, f, text, vu, sizeof(vu), R - 21 - 10);
	ptk_text(p, d, f, x + 7, baseline, vu, p->text);
}

static void
zoom_point(PtkZoom *z, int x, int y)
{
	if (z->n == z->cap) {
		int c = z->cap ? z->cap * 2 : 256;
		XPoint *n = realloc(z->pts, (size_t)c * sizeof(XPoint));

		if (n == NULL)
			return;
		z->pts = n;
		z->cap = c;
	}
	z->pts[z->n].x = (short)x;
	z->pts[z->n].y = (short)y;
	z->n++;
}

/* contour de l'icône, un pixel sur deux : les pixels opaques qui touchent le vide */
void
ptk_zoom_icone(PtkZoom *z, const PtkArgb *a, int x, int y)
{
	int i, j;

	if (a == NULL || a->alpha == NULL) {
		ptk_zoom_rect(z, x, y, a != NULL ? a->w : 16, a != NULL ? a->h : 16);
		return;
	}
#define OPAQUE(c, l) ((c) >= 0 && (l) >= 0 && (c) < a->w && (l) < a->h && a->alpha[(l) * a->w + (c)] >= 200)
	for (j = 0; j < a->h; j++)
		for (i = 0; i < a->w; i++)
			if (OPAQUE(i, j) && !(OPAQUE(i - 1, j) && OPAQUE(i + 1, j) && OPAQUE(i, j - 1) && OPAQUE(i, j + 1)) &&
			    ((i + j) & 1) == 0)
				zoom_point(z, x + i, y + j);
#undef OPAQUE
}

void
ptk_zoom_rect(PtkZoom *z, int x, int y, int w, int h)
{
	int i;

	for (i = 0; i < w; i += 2) {
		zoom_point(z, x + i, y);
		zoom_point(z, x + i, y + h - 1);
	}
	for (i = 2; i < h - 1; i += 2) {
		zoom_point(z, x, y + i);
		zoom_point(z, x + w - 1, y + i);
	}
}

/*
 * Relevé image par image sur Mac OS 9 : la distance restante est multipliée par 0,71 à chaque pas (12 pas),
 * cinq contours visibles à la fois, 270 ms en tout. Serveur saisi le temps de l'animation : aucun client
 * ne peut se redessiner sous les contours, donc aucun reste à l'écran.
 */
#define ZOOM_PAS 13
#define ZOOM_TRAINE 5
void
ptk_zoom_retour(Ptk *p, PtkZoom *z, int dx, int dy)
{
	XGCValues v;
	GC g;
	XPoint *t;
	int ox[ZOOM_PAS], oy[ZOOM_PAS], i, k;
	double f = 1.0;

	if (z->n == 0 || (dx == 0 && dy == 0)) {
		free(z->pts);
		z->pts = NULL;
		z->n = z->cap = 0;
		return;
	}
	if ((t = malloc((size_t)z->n * sizeof(XPoint))) == NULL)
		return;
	for (i = 0; i < ZOOM_PAS - 1; i++, f *= 0.71) {
		ox[i] = (int)(dx * f);
		oy[i] = (int)(dy * f);
	}
	ox[ZOOM_PAS - 1] = oy[ZOOM_PAS - 1] = 0;
	v.function = GXinvert;
	v.subwindow_mode = IncludeInferiors;
	v.plane_mask = 0x00ffffff;
	g = XCreateGC(p->dpy, p->root, GCFunction | GCSubwindowMode | GCPlaneMask, &v);
	XGrabServer(p->dpy);
	for (i = 0; i < ZOOM_PAS + ZOOM_TRAINE; i++) {
		/* poser le contour du pas i, retirer celui qui sort de la traînée ; deux passages XOR s'annulent */
		int pose[2], np = 0, j;

		if (i < ZOOM_PAS)
			pose[np++] = i;
		if (i >= ZOOM_TRAINE)
			pose[np++] = i - ZOOM_TRAINE;
		for (j = 0; j < np; j++) {
			for (k = 0; k < z->n; k++) {
				t[k].x = (short)(z->pts[k].x + ox[pose[j]]);
				t[k].y = (short)(z->pts[k].y + oy[pose[j]]);
			}
			XDrawPoints(p->dpy, p->root, g, t, z->n, CoordModeOrigin);
		}
		XSync(p->dpy, False);
		usleep(15000);
	}
	XUngrabServer(p->dpy);
	XFreeGC(p->dpy, g);
	XFlush(p->dpy);
	free(t);
	free(z->pts);
	z->pts = NULL;
	z->n = z->cap = 0;
}

/*
 * Relevé sur Mac OS 9 : 7 rectangles dont la distance au départ croît de 1,4 fois à chaque pas, posés un
 * à un et gardés jusqu'à l'arrivée, puis tous retirés. Trait de 2 px tramé, en inversion.
 */
#define RECTS 7
void
ptk_zoom_rects(Ptk *p, int x0, int y0, int w0, int h0, int x1, int y1, int w1, int h1)
{
	static const double frac[RECTS] = { 0.133, 0.186, 0.26, 0.364, 0.51, 0.714, 1.0 };
	PtkZoom z[RECTS];
	XGCValues v;
	GC g;
	int i, k, passe;

	v.function = GXinvert;
	v.subwindow_mode = IncludeInferiors;
	v.plane_mask = 0x00ffffff;
	g = XCreateGC(p->dpy, p->root, GCFunction | GCSubwindowMode | GCPlaneMask, &v);
	for (i = 0; i < RECTS; i++) {
		double f = frac[i];
		int x = x0 + (int)((x1 - x0) * f), y = y0 + (int)((y1 - y0) * f);
		int w = w0 + (int)((w1 - w0) * f), h = h0 + (int)((h1 - h0) * f);

		memset(&z[i], 0, sizeof(z[i]));
		for (k = 0; k < w; k++) {
			if (((x + k + y) & 1) == 0) {
				zoom_point(&z[i], x + k, y);
				zoom_point(&z[i], x + k, y + h - 1);
			}
			if (((x + k + y + 1) & 1) == 0) {
				zoom_point(&z[i], x + k, y + 1);
				zoom_point(&z[i], x + k, y + h - 2);
			}
		}
		for (k = 2; k < h - 2; k++) {
			if (((x + y + k) & 1) == 0) {
				zoom_point(&z[i], x, y + k);
				zoom_point(&z[i], x + w - 1, y + k);
			}
			if (((x + 1 + y + k) & 1) == 0) {
				zoom_point(&z[i], x + 1, y + k);
				zoom_point(&z[i], x + w - 2, y + k);
			}
		}
	}
	XGrabServer(p->dpy);
	for (passe = 0; passe < 2; passe++) {
		for (i = 0; i < RECTS; i++) {
			XDrawPoints(p->dpy, p->root, g, z[i].pts, z[i].n, CoordModeOrigin);
			if (passe == 0) {
				XSync(p->dpy, False);
				usleep(14000);
			}
		}
		XSync(p->dpy, False);
	}
	XUngrabServer(p->dpy);
	XFlush(p->dpy);
	XFreeGC(p->dpy, g);
	for (i = 0; i < RECTS; i++)
		free(z[i].pts);
}

/* relevé : bord noir de la silhouette, intérieur #ccccff, point noir là où (x + 2y) est multiple de 4 */
void
ptk_draw_argb_ouverte(Ptk *p, Drawable d, const PtkArgb *a, int x, int y, int assombri)
{
	PtkZoom noir = { NULL, 0, 0 }, fond = { NULL, 0, 0 };
	GC g;
	int i, j;

	if (a == NULL || a->alpha == NULL)
		return;
#define PLEIN(c, l) ((c) >= 0 && (l) >= 0 && (c) < a->w && (l) < a->h && a->alpha[(l) * a->w + (c)] >= 40)
	for (j = 0; j < a->h; j++)
		for (i = 0; i < a->w; i++) {
			if (!PLEIN(i, j))
				continue;
			if (!(PLEIN(i - 1, j) && PLEIN(i + 1, j) && PLEIN(i, j - 1) && PLEIN(i, j + 1)) ||
			    (x + i + 2 * (y + j)) % 4 == 0)
				zoom_point(&noir, x + i, y + j);
			else
				zoom_point(&fond, x + i, y + j);
		}
#undef PLEIN
	g = XCreateGC(p->dpy, d, 0, NULL);
	XSetForeground(p->dpy, g, ptk_color(p, assombri ? "#666680" : "#ccccff").pixel);
	if (fond.n > 0)
		XDrawPoints(p->dpy, d, g, fond.pts, fond.n, CoordModeOrigin);
	XSetForeground(p->dpy, g, p->black.pixel);
	if (noir.n > 0)
		XDrawPoints(p->dpy, d, g, noir.pts, noir.n, CoordModeOrigin);
	XFreeGC(p->dpy, g);
	free(noir.pts);
	free(fond.pts);
}

int
ptk_fantome_debut(Ptk *p, PtkFantome *f, int w, int h)
{
	XSetWindowAttributes a;
	unsigned d = (unsigned)DefaultDepth(p->dpy, p->screen);
	int sw = DisplayWidth(p->dpy, p->screen), sh = DisplayHeight(p->dpy, p->screen);
	XGCValues v;
	GC g;

	f->w = w;
	f->h = h;
	f->ecran = XCreatePixmap(p->dpy, p->root, (unsigned)sw, (unsigned)sh, d);
	f->tampon = XCreatePixmap(p->dpy, p->root, (unsigned)w, (unsigned)h, d);
	v.subwindow_mode = IncludeInferiors;
	g = XCreateGC(p->dpy, p->root, GCSubwindowMode, &v);
	XCopyArea(p->dpy, p->root, f->ecran, g, 0, 0, (unsigned)sw, (unsigned)sh, 0, 0);
	XFreeGC(p->dpy, g);
	a.override_redirect = True;
	a.background_pixmap = None;
	f->win = XCreateWindow(p->dpy, p->root, -w, -h, (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixmap, &a);
	/* transparent aux clics : le dépôt doit trouver la fenêtre qui est dessous */
	XShapeCombineRectangles(p->dpy, f->win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
	XMapRaised(p->dpy, f->win);
	return 0;
}

void
ptk_fantome_poser(Ptk *p, PtkFantome *f, int x, int y, void (*dessin)(Ptk *, Drawable, void *), void *user)
{
	if (f->win == None)
		return;
	XCopyArea(p->dpy, f->ecran, f->tampon, p->gc, x, y, (unsigned)f->w, (unsigned)f->h, 0, 0);
	dessin(p, f->tampon, user);
	XMoveWindow(p->dpy, f->win, x, y);
	XCopyArea(p->dpy, f->tampon, f->win, p->gc, 0, 0, (unsigned)f->w, (unsigned)f->h, 0, 0);
	XFlush(p->dpy);
}

void
ptk_fantome_fin(Ptk *p, PtkFantome *f)
{
	if (f->win == None)
		return;
	XDestroyWindow(p->dpy, f->win);
	XFreePixmap(p->dpy, f->ecran);
	XFreePixmap(p->dpy, f->tampon);
	f->win = None;
}

/* fonce de moitié les pixels d'un masque 1 bit : icône pressée, comme Mac OS 9 */
void
ptk_assombrir_masque(Ptk *p, Drawable d, Pixmap masque, int x, int y, int w, int h)
{
	XRenderPictFormat *fv = XRenderFindVisualFormat(p->dpy, p->visual);
	XRenderPictFormat *f1 = XRenderFindStandardFormat(p->dpy, PictStandardA1);
	XRenderColor noir = { 0, 0, 0, 0x8000 };
	Picture dst, src, m;

	if (fv == NULL || f1 == NULL)
		return;
	dst = XRenderCreatePicture(p->dpy, d, fv, 0, NULL);
	m = XRenderCreatePicture(p->dpy, masque, f1, 0, NULL);
	src = XRenderCreateSolidFill(p->dpy, &noir);
	XRenderComposite(p->dpy, PictOpOver, src, m, dst, 0, 0, 0, 0, x, y, (unsigned)w, (unsigned)h);
	XRenderFreePicture(p->dpy, src);
	XRenderFreePicture(p->dpy, m);
	XRenderFreePicture(p->dpy, dst);
}

void
ptk_fill_alpha(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c, unsigned short alpha)
{
	XRenderPictFormat *fmt = XRenderFindVisualFormat(p->dpy, p->visual);
	XRenderColor rc;
	Picture dst;

	if (fmt == NULL)
		return;
	rc.red = (unsigned short)(c.r * (unsigned)alpha / 0xffff);
	rc.green = (unsigned short)(c.g * (unsigned)alpha / 0xffff);
	rc.blue = (unsigned short)(c.b * (unsigned)alpha / 0xffff);
	rc.alpha = alpha;
	dst = XRenderCreatePicture(p->dpy, d, fmt, 0, NULL);
	XRenderFillRectangle(p->dpy, PictOpOver, dst, &rc, x, y, (unsigned)w, (unsigned)h);
	XRenderFreePicture(p->dpy, dst);
}

static PtkColor
sb_gris(Ptk *p, int v)
{
	char n[8];

	snprintf(n, sizeof(n), "#%02x%02x%02x", v, v, v);
	return ptk_color(p, n);
}

static int
sb_longueur(const PtkScrollbar *s)
{
	return s->horizontal ? s->w : s->h;
}

static int
sb_origine(const PtkScrollbar *s)
{
	return s->horizontal ? s->x : s->y;
}

static void
sb_rect(Ptk *p, Drawable d, const PtkScrollbar *s, int a, int b,
    int la, int lb, PtkColor c)
{
	if (la <= 0 || lb <= 0)
		return;
	if (s->horizontal)
		ptk_fill(p, d, s->x + a, s->y + b, la, lb, c);
	else
		ptk_fill(p, d, s->x + b, s->y + a, lb, la, c);
}

void
ptk_scrollbar_thumb(const PtkScrollbar *s, int *pos, int *len)
{
	int rail = sb_longueur(s) - 30;
	int total = s->max - s->min + s->page;
	int etendue = s->max - s->min;

	if (rail < 1)
		rail = 1;
	if (total < 1)
		total = 1;
	*len = rail * s->page / total;
	if (*len < 16)
		*len = 16;
	if (*len > rail)
		*len = rail;
	*pos = sb_origine(s) + (etendue > 0 ?
	    (rail - *len) * (s->value - s->min) / etendue : 0);
}

int
ptk_scrollbar_zone(const PtkScrollbar *s, int x, int y)
{
	int along = s->horizontal ? x : y;
	int travers = s->horizontal ? y - s->y : x - s->x;
	int fin = sb_origine(s) + sb_longueur(s);
	int p, l;

	if (travers < 0 || travers >= 16 || along < sb_origine(s) ||
	    along >= fin || s->max <= s->min)
		return PTK_SB_RIEN;
	if (along >= fin - 15)
		return PTK_SB_AVANCE;
	if (along >= fin - 30)
		return PTK_SB_RECUL;
	ptk_scrollbar_thumb(s, &p, &l);
	if (along < p)
		return PTK_SB_PAGE_RECUL;
	if (along >= p + l)
		return PTK_SB_PAGE_AVANCE;
	return PTK_SB_POUCE;
}

int
ptk_scrollbar_value(const PtkScrollbar *s, int x, int y, int offset)
{
	int rail = sb_longueur(s) - 30;
	int along = s->horizontal ? x : y;
	int p, l, libre, v;

	ptk_scrollbar_thumb(s, &p, &l);
	libre = rail - l;
	if (libre <= 0 || s->max <= s->min)
		return s->min;
	p = along - offset - sb_origine(s);
	if (p < 0)
		p = 0;
	if (p > libre)
		p = libre;
	v = s->min + (p * (s->max - s->min) + libre / 2) / libre;
	return v;
}

static void
sb_fleche(Ptk *p, Drawable d, const PtkScrollbar *s, int a, int recul,
    int on, int presse)
{
	int i, n;

	if (on) {
		sb_rect(p, d, s, a, 1, 14, 14, sb_gris(p, 0xdd));
		sb_rect(p, d, s, a, 1, 13, 1, p->white);
		sb_rect(p, d, s, a, 1, 1, 13, p->white);
		sb_rect(p, d, s, a + 13, 1, 1, 1, sb_gris(p, 0xdd));
		sb_rect(p, d, s, a + 13, 2, 1, 13, sb_gris(p, 0xbb));
		sb_rect(p, d, s, a + 1, 14, 13, 1, sb_gris(p, 0xbb));
		sb_rect(p, d, s, a, 14, 1, 1, sb_gris(p, 0xdd));
	} else {
		sb_rect(p, d, s, a, 1, 14, 14, sb_gris(p, 0xee));
	}
	if (presse)
		sb_rect(p, d, s, a, 1, 14, 14, sb_gris(p, 0x77));
	for (i = 0; i < 4; i++) {
		PtkColor c = presse ? p->white : on ? p->black : sb_gris(p, 0x88);

		/* sb_rect échange déjà les axes d'un ascenseur vertical : le
		 * même triangle (pointe vers le recul ou l'avance) sert aux deux */
		n = 2 + 2 * i;
		if (recul)
			sb_rect(p, d, s, a + 5 + i, 8 - n / 2, 1, n, c);
		else
			sb_rect(p, d, s, a + 8 - i, 8 - n / 2, 1, n, c);
	}
}

void
ptk_scrollbar_draw(Ptk *p, Drawable d, const PtkScrollbar *s)
{
	int on = s->active && s->max > s->min;
	int L = sb_longueur(s) - 30;
	int r;

	sb_rect(p, d, s, 0, 0, L + 30, 1,
	    s->active ? p->black : sb_gris(p, 0x55));
	if (!s->active) {
		sb_rect(p, d, s, 0, 1, L + 30, 14, sb_gris(p, 0xee));
		return;
	}
	if (on) {
		static const struct { int b, v; } t[] = {
			{1, 0x77}, {2, 0x88}, {13, 0xbb}, {14, 0xcc}
		};
		int p0, l, g;

		sb_rect(p, d, s, 0, 3, L, 10, sb_gris(p, 0xaa));
		for (r = 0; r < 4; r++)
			sb_rect(p, d, s, 0, t[r].b, L, 1, sb_gris(p, t[r].v));
		ptk_scrollbar_thumb(s, &p0, &l);
		p0 -= sb_origine(s);
		sb_rect(p, d, s, 0, 1, 1, 13, sb_gris(p, 0x77));
		sb_rect(p, d, s, 1, 2, 1, 11, sb_gris(p, 0x88));
		if (p0 > 0)
			sb_rect(p, d, s, p0 - 1, 1, 1, 14, p->black);
		if (p0 + l < L) {
			sb_rect(p, d, s, p0 + l, 1, 1, 14, p->black);
			if (p0 + l + 2 < L) {
				sb_rect(p, d, s, p0 + l + 1, 1, 1, 13,
				    sb_gris(p, 0x77));
				sb_rect(p, d, s, p0 + l + 2, 2, 1, 11,
				    sb_gris(p, 0x88));
			}
		}
		sb_rect(p, d, s, p0, 1, l, 14, ptk_color(p, "#9999ff"));
		sb_rect(p, d, s, p0, 1, l, 1, ptk_color(p, "#ccccff"));
		sb_rect(p, d, s, p0, 1, 1, 13, ptk_color(p, "#ccccff"));
		sb_rect(p, d, s, p0, 14, l, 1, ptk_color(p, "#6666cc"));
		sb_rect(p, d, s, p0 + l - 1, 2, 1, 13, ptk_color(p, "#6666cc"));
		sb_rect(p, d, s, p0, 1, 1, 1, sb_gris(p, 0xee));
		sb_rect(p, d, s, p0, 14, 1, 1, ptk_color(p, "#9999ff"));
		sb_rect(p, d, s, p0 + l - 1, 1, 1, 1, ptk_color(p, "#9999ff"));
		if (l >= 16) {
			g = p0 + (l - 8) / 2;
			for (r = 0; r < 4; r++) {
				sb_rect(p, d, s, g + 2 * r, 4, 1, 1,
				    sb_gris(p, 0xee));
				sb_rect(p, d, s, g + 2 * r, 5, 1, 6,
				    ptk_color(p, "#ccccff"));
				sb_rect(p, d, s, g + 2 * r + 1, 5, 1, 7,
				    ptk_color(p, "#333399"));
			}
		}
	} else {
		sb_rect(p, d, s, 0, 1, L, 14, sb_gris(p, 0xee));
	}
	sb_rect(p, d, s, L, 1, 1, 14, on ? p->black : sb_gris(p, 0x55));
	sb_rect(p, d, s, L + 15, 1, 1, 14,
	    on ? p->black : sb_gris(p, 0x88));
	sb_fleche(p, d, s, L + 1, 1, on,
	    s->pressed == PTK_SB_RECUL);
	sb_fleche(p, d, s, L + 16, 0, on,
	    s->pressed == PTK_SB_AVANCE);
}

void
ptk_separateur(Ptk *p, Drawable d, int x, int y, int w, int h)
{
	/* inactif : #bbb puis #ddd (relevé) */
	PtkColor g = ptk_color(p, p->inactif ? "#bbbbbb" : "#888888");
	PtkColor l = p->inactif ? p->face : p->white;

	if (w >= h) {
		ptk_fill(p, d, x, y, w, 1, g);
		ptk_fill(p, d, x + 1, y + 1, w, 1, l);
	} else {
		ptk_fill(p, d, x, y, 1, h, g);
		ptk_fill(p, d, x + 1, y + 1, 1, h, l);
	}
}

/* Petites flèches, relevées sur Apparence > Polices (Mac OS 9.2.1, champ
 * Taille) : bouton 22×20 aux coins arrondis, biseau blanc / #aaa / #777,
 * deux triangles de 4 rangées (base 7) de part et d'autre du milieu. La
 * moitié enfoncée prend l'état de la zone des flèches du menu local ouvert,
 * relevé sur la même VM : fond #666, biseau #444 / #555, triangle blanc. */
static const char *const petites_fleches[PTK_PETITES_FLECHES_H] = {
	"  111111111111111111  ",
	" 12222222222222222231 ",
	"1244444444444444444251",
	"1242222222222222222561",
	"1242222222222222222561",
	"1242222222122222222561",
	"1242222221112222222561",
	"1242222211111222222561",
	"1242222111111122222561",
	"1242222222222222222561",
	"1242222222222222222561",
	"1242222111111122222561",
	"1242222211111222222561",
	"1242222221112222222561",
	"1242222222122222222561",
	"1242222222222222222561",
	"1242222222222222222561",
	"1225555555555555555661",
	"516666666666666666661 ",
	"  111111111111111111  ",
};

void
ptk_petites_fleches(Ptk *p, Drawable d, int x, int y, int presse, int actif)
{
	int r, c;

	if (p->inactif)
		actif = 0;
	if (!actif)
		presse = 0;

	for (r = 0; r < PTK_PETITES_FLECHES_H; r++) {
		/* moitié de la rangée : 1 en haut (rangées 2 à 9), 2 en bas
		 * (10 à 17) ; l'enfoncement couvre l'intérieur du biseau */
		int moitie = r >= 2 && r <= 9 ? 1 : r >= 10 && r <= 17 ? 2 : 0;
		int enf = presse != 0 && moitie == presse;

		for (c = 0; c < PTK_PETITES_FLECHES_L; c++) {
			char k = petites_fleches[r][c];
			const char *h;

			if (!actif && c == 0 && r == 1)
				k = '3';

			switch (k) {
			case '1':
				/* triangles et contour */
				if (c > 2 && c < 19 && r > 1 && r < 18)
					h = enf ? "#ffffff" : actif ? "#000000" : "#888888";
				else
					h = actif ? "#000000" : "#888888";
				break;
			case '2': h = enf ? "#666666" : "#dddddd"; break;
			case '3': h = "#bbbbbb"; break;
			case '4': h = enf ? "#444444" : "#ffffff"; break;
			case '5': h = enf ? "#555555" : "#aaaaaa"; break;
			case '6': h = enf ? "#555555" : "#777777"; break;
			default: continue;
			}
			/* inactif (relevé) : contour et triangles #888, face
			 * #ddd sans biseau, #aaa aux deux coins de gauche */
			if (!actif && k != '1')
				h = (c == 0 && (r == 1 || r == 18)) ? "#aaaaaa" :
				    "#dddddd";
			ptk_fill(p, d, x + c, y + r, 1, 1, ptk_color(p, h));
		}
	}
}

/* Triangle de détail, relevé sur la vue liste du Finder de la VM : 12 rangées,
 * bord noir, dégradé lavande, ombre #888 / #bbb. */
void
ptk_triangle(Ptk *p, Drawable d, int x, int y, int ouvert, int presse)
{
	static const char *m[12] = { "#", "##", "#a#", "#ab#", "#abb#",
	    "#abbc#", "#abc#ds", "#ac#ds", "#b#ds", "##ds", "#ds", " s" };
	int r, c;

	for (r = 0; r < 12; r++)
		for (c = 0; m[r][c] != '\0'; c++) {
			const char *h;

			if (p->inactif) {
				/* contour #888, intérieur #ddd, sans ombre */
				if (m[r][c] == ' ' || m[r][c] == 's' ||
				    m[r][c] == 'd')
					continue;
				h = m[r][c] == '#' ? "#888888" : "#dddddd";
			} else switch (m[r][c]) {
			case '#': h = "#000000"; break;
			case 'a': h = presse ? "#6666cc" : "#ccccff"; break;
			case 'b': h = presse ? "#333399" : "#9999ff"; break;
			case 'c': h = presse ? "#222266" : "#6666cc"; break;
			case 'd': h = "#888888"; break;
			case 's': h = "#bbbbbb"; break;
			default: continue;
			}
			if (ouvert)
				ptk_fill(p, d, x + r, y + c, 1, 1, ptk_color(p, h));
			else
				ptk_fill(p, d, x + c, y + r, 1, 1, ptk_color(p, h));
		}
}

/* le creux commun aux barres de progression : cadre noir, lueurs #88/#dd,
 * face du chrome dans le fond */
static void
progress_creux(Ptk *p, Drawable d, int x, int y, int w, int h)
{
	if (h < 6)
		h = 6;
	if (w < 6)
		w = 6;
	ptk_rect(p, d, x, y, w, h, p->black);
	/* relevé sur l'écran de démarrage : creux #bb, #88 en haut, #dd en bas et à droite */
	ptk_fill(p, d, x + 1, y + 1, w - 2, h - 2, ptk_color(p, "#bbbbbb"));
	ptk_line(p, d, x + 1, y + 1, x + w - 2, y + 1, ptk_color(p, "#888888"));
	ptk_line(p, d, x + 1, y + 1, x + 1, y + h - 2, ptk_color(p, "#888888"));
	ptk_line(p, d, x + 1, y + h - 2, x + w - 2, y + h - 2, ptk_color(p, "#dddddd"));
	ptk_line(p, d, x + w - 2, y + 1, x + w - 2, y + h - 2, ptk_color(p, "#dddddd"));
	ptk_fill(p, d, x + 2, y + 2, w - 4, h - 4, p->menu);
}

/* Barre de progression bleue polie, relevée sur About This Computer. */
void
ptk_progress(Ptk *p, Drawable d, int x, int y, int w, int h,
    int value, int max)
{
	int fill, i;
	PtkColor grad[10];

	if (max <= 0)
		max = 100;
	if (value < 0)
		value = 0;
	if (value > max)
		value = max;
	if (h < 6)
		h = 6;
	fill = (w - 4) * value / max;
	if (fill < 0)
		fill = 0;

	grad[0] = p->selection;          /* #333399 */
	grad[1] = p->focus;              /* #6666cc */
	grad[2] = ptk_color(p, "#9999ff");
	grad[3] = p->field_select;       /* #ccccff */
	grad[4] = p->gray[7];            /* #eeeeee */
	grad[5] = p->field_select;
	grad[6] = ptk_color(p, "#9999ff");
	grad[7] = p->focus;
	grad[8] = p->selection;
	grad[9] = ptk_color(p, "#000055");

	progress_creux(p, d, x, y, w, h);

	if (fill > 0 && h > 4) {
		int inner = h - 4;
		for (i = 0; i < inner; i++) {
			int k = inner > 1 ? i * 9 / (inner - 1) : 4;
			ptk_line(p, d, x + 2, y + 2 + i,
			    x + 1 + fill, y + 2 + i, grad[k]);
		}
		ptk_line(p, d, x + 1, y + 2, x + 1, y + h - 3,
		    p->focus);
	}
	ptk_line(p, d, x + 1, y + h, x + w - 2, y + h, p->white);
}

void
ptk_barbier(Ptk *p, Drawable d, int x, int y, int w, int h, int phase)
{
	/* la trame de 6 × 6, active et inactive, préparée une fois */
	static Pixmap tuile[2];
	static Display *tuile_dpy;
	int k = p->inactif ? 1 : 0;

	progress_creux(p, d, x, y, w, h);
	if (w <= 4 || h <= 4)
		return;
	if (tuile_dpy != p->dpy)
		tuile[0] = tuile[1] = None, tuile_dpy = p->dpy;
	if (tuile[k] == None) {
		int i, j;

		tuile[k] = XCreatePixmap(p->dpy, RootWindow(p->dpy, p->screen), 6, 6,
		    (unsigned)DefaultDepth(p->dpy, p->screen));
		/* rayures diagonales à 45°, « / » : à coordonnée (i - j) constante.
		 * Deux de bleu sombre, une de transition, deux de bleu clair. */
		for (j = 0; j < 6; j++)
			for (i = 0; i < 6; i++) {
				int u = ((i - j) % 6 + 6) % 6;
				const char *f;

				if (k)
					f = u < 3 ? "#999999" : u == 3 ? "#a3a3a3" : "#cccccc";
				else
					f = u < 2 ? "#333399" : u == 2 ? "#4040b3" :
					    u < 5 ? "#ccccff" : "#9999ff";
				ptk_fill(p, tuile[k], i, j, 1, 1, ptk_color(p, f));
			}
	}
	/* une seule requête : la trame pavée, décalée de la phase (phase glisse
	 * la trame, comme Apparence) */
	XSetTile(p->dpy, p->gc, tuile[k]);
	XSetTSOrigin(p->dpy, p->gc, x + 2 - phase, y + 2);
	XSetFillStyle(p->dpy, p->gc, FillTiled);
	XFillRectangle(p->dpy, d, p->gc, x + 2, y + 2, (unsigned)(w - 4), (unsigned)(h - 4));
	XSetFillStyle(p->dpy, p->gc, FillSolid);
	XSetTSOrigin(p->dpy, p->gc, 0, 0);
}

void
ptk_recherche(Ptk *p, Drawable d, int x, int y, int w, int h, int phase, int actif)
{
	/* chevron « ▶ » de 9 rangées : largeur de chaque rangée */
	static const int largeurs[9] = { 2, 3, 4, 5, 7, 5, 4, 3, 2 };
	int pas = 15;	/* distance entre deux chevrons */
	int k, cy, ah = h - 6, i, j;
	PtkColor fl = actif && !p->inactif ? p->selection : ptk_color(p, "#999999");

	if (h < 12)
		h = 12;
	if (w < 12)
		w = 12;
	if (ah > 9)
		ah = 9;
	if (ah < 3)
		ah = 3;
	progress_creux(p, d, x, y, w, h);
	cy = y + 2 + (h - 4 - ah) / 2;
	ptk_clip(p, x + 3, y + 3, w - 6, h - 6);
	for (k = 0; k < 3; k++) {
		int bx = x + 3 + (phase + k * pas) % (pas * 3);

		for (j = 0; j < ah; j++)
			for (i = 0; i < largeurs[j] * ah / 9; i++)
				ptk_fill(p, d, bx + i, cy + j, 1, 1, fl);
	}
	ptk_unclip(p);
}



/*
 * Case de barre de titre, relevée au pixel sur Mac OS 9.2 : (x, y) = coin de l'ombre #88.
 * glyph : 0 fermer, 1 zoom, 2 repli. Surface en dégradé diagonal #99 → #ff.
 */
void
ptk_title_button(Ptk *p, Drawable d, int x, int y, int glyph, int enfonce)
{
	static const unsigned char deg[7][7] = {
		{ 0x99, 0x99, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc },
		{ 0x99, 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc },
		{ 0xaa, 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd },
		{ 0xaa, 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd },
		{ 0xbb, 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, 0xee },
		{ 0xbb, 0xcc, 0xcc, 0xdd, 0xdd, 0xee, 0xee },
		{ 0xcc, 0xcc, 0xdd, 0xdd, 0xee, 0xee, 0xff },
	};
	/* enfoncée, relevée sur la VM : même cadre, surface 9×9 plus sombre, sans liseré */
	static const unsigned char creux[9][9] = {
		{ 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88 },
		{ 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88 },
		{ 0x55, 0x55, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99 },
		{ 0x55, 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99, 0x99 },
		{ 0x66, 0x66, 0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0x99 },
		{ 0x66, 0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xaa, 0x99 },
		{ 0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xaa, 0xaa, 0x99 },
		{ 0x77, 0x88, 0x88, 0x99, 0x99, 0xaa, 0xaa, 0xbb, 0x99 },
		{ 0x88, 0x88, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99 },
	};
	PtkColor c88 = ptk_color(p, "#888888"), c22 = ptk_color(p, "#222222");
	PtkColor ccc = ptk_color(p, "#cccccc");
	int r, c;
	char n[8];

	ptk_fill(p, d, x, y, 12, 1, c88);
	ptk_fill(p, d, x, y, 1, 12, c88);
	ptk_rect(p, d, x + 1, y + 1, 11, 11, c22);
	ptk_fill(p, d, x + 12, y + 1, 1, 12, p->white);
	ptk_fill(p, d, x + 1, y + 12, 12, 1, p->white);
	if (enfonce) {
		for (r = 0; r < 9; r++)
			for (c = 0; c < 9; c++) {
				snprintf(n, sizeof(n), "#%02x%02x%02x", creux[r][c], creux[r][c], creux[r][c]);
				ptk_fill(p, d, x + 2 + c, y + 2 + r, 1, 1, ptk_color(p, n));
			}
	} else {
		ptk_fill(p, d, x + 2, y + 2, 9, 9, ccc);
		ptk_fill(p, d, x + 2, y + 2, 1, 1, p->white);
		for (r = 0; r < 7; r++)
			for (c = 0; c < 7; c++) {
				snprintf(n, sizeof(n), "#%02x%02x%02x", deg[r][c], deg[r][c], deg[r][c]);
				ptk_fill(p, d, x + 3 + c, y + 3 + r, 1, 1, ptk_color(p, n));
			}
		ptk_fill(p, d, x + 10, y + 3, 1, 7, c88);
		ptk_fill(p, d, x + 3, y + 10, 8, 1, c88);
	}
	if (glyph == 1) {
		ptk_fill(p, d, x + 7, y + 2, 1, 6, c22);
		ptk_fill(p, d, x + 2, y + 7, 6, 1, c22);
	} else if (glyph == 2) {
		ptk_fill(p, d, x + 2, y + 5, 9, 1, c22);
		ptk_fill(p, d, x + 2, y + 7, 9, 1, c22);
	}
}

/* rayures : blanc aux rangs pairs, #777 aux rangs impairs, décalées d'un pixel */
static void
title_stripes(Ptk *p, Drawable d, int x, int y, int x0, int x1)
{
	PtkColor c77 = ptk_color(p, "#777777");
	int r;

	if (x1 <= x0)
		return;
	for (r = 4; r <= 14; r += 2) {
		ptk_fill(p, d, x + x0, y + r, x1 - x0, 1, p->white);
		ptk_fill(p, d, x + x0 + 1, y + r + 1, x1 - x0, 1, c77);
	}
}

/*
 * Barre de titre Platinum, dans le cadre dessiné par le WM : rangs 2 à 19.
 * Colonnes (Mac OS 9.2) : fermer à 4, rayures de 21 au titre et du titre à w-1-38,
 * zoom à w-1-32, repli à w-1-16. Inactive : fond #dd, ni rayures ni cases.
 */
void
ptk_titlebar(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *title, int active, Pixmap icon, Pixmap icon_mask)
{
	int tw, gw, gx, baseline;
	char t[256];

	(void)h;
	ptk_fill(p, d, x + 2, y + 2, w - 4, 18,
	    active ? p->titlebar : ptk_color(p, "#dddddd"));
	ptk_fit_text(p, p->font_title, title ? title : "", t, sizeof(t), w - 90);
	tw = ptk_text_width(p, p->font_title, t);
	gw = tw + (icon != None ? 18 : 0);
	gx = x + (w - gw) / 2;
	if (active) {
		title_stripes(p, d, x, y, 21, gx - x - 5);
		title_stripes(p, d, x, y, gx - x + gw + 5, w - 1 - (p->titre_sans_zoom ? 21 : 37));
		ptk_title_button(p, d, x + 4, y + 4, 0, 0);
		if (!p->titre_sans_zoom)
			ptk_title_button(p, d, x + w - 1 - 32, y + 4, 1, 0);
		ptk_title_button(p, d, x + w - 1 - 16, y + 4, 2, 0);
	}
	if (icon != None) {
		ptk_draw_pixmap(p, d, icon, icon_mask, gx, y + 2, 16, 16);
		if (p->titre_icone_presse && icon_mask != None)
			ptk_assombrir_masque(p, d, icon_mask, gx, y + 2, 16, 16);
	}
	baseline = y + 14;
	ptk_text(p, d, p->font_title, gx + (icon != None ? 18 : 0), baseline, t,
	    active ? p->text : p->text_dim);
}

void
ptk_text(Ptk *p, Drawable d, XftFont *font, int x, int baseline,
    const char *text, PtkColor c)
{
	XftDraw *draw;
	XftColor xc;
	XRenderColor rc;

	if (text == NULL || *text == '\0' || font == NULL)
		return;
	draw = XftDrawCreate(p->dpy, d, p->visual, p->cmap);
	if (draw == NULL)
		return;
	if (p->clip_on)
		XftDrawSetClipRectangles(draw, 0, 0, &p->clip, 1);
	rc.red = c.r;
	rc.green = c.g;
	rc.blue = c.b;
	rc.alpha = 0xffff;
	if (XftColorAllocValue(p->dpy, p->visual, p->cmap, &rc, &xc)) {
		XftDrawStringUtf8(draw, &xc, font, x, baseline,
		    (const FcChar8 *)text, (int)strlen(text));
		XftColorFree(p->dpy, p->visual, p->cmap, &xc);
	}
	XftDrawDestroy(draw);
}

int
ptk_text_width(Ptk *p, XftFont *font, const char *text)
{
	XGlyphInfo ext;

	if (font == NULL || text == NULL || *text == '\0')
		return 0;
	XftTextExtentsUtf8(p->dpy, font, (const FcChar8 *)text,
	    (int)strlen(text), &ext);
	return ext.xOff;
}

void
ptk_fit_text(Ptk *p, XftFont *font, const char *src, char *dst,
    size_t dstsz, int maxw)
{
	size_t n;
	int ew;

	if (dstsz == 0)
		return;
	snprintf(dst, dstsz, "%s", src);
	if (ptk_text_width(p, font, dst) <= maxw)
		return;
	ew = ptk_text_width(p, font, "…");
	while (strlen(dst) > 0) {
		n = strlen(dst);
		n--;
		while (n > 0 && ((unsigned char)dst[n] & 0xc0) == 0x80)
			n--;
		dst[n] = '\0';
		if (ptk_text_width(p, font, dst) + ew <= maxw)
			break;
	}
	if (strlen(dst) + 4 < dstsz)
		strncat(dst, "…", dstsz - strlen(dst) - 1);
}

int
ptk_text_height(Ptk *p, XftFont *font)
{
	(void)p;
	if (font == NULL)
		return 0;
	return font->ascent + font->descent;
}

void
ptk_set_root_pattern(Ptk *p)
{
	/* le motif réel du bureau, posé en fond d'écran : pas de clignotement au démarrage */
	{
		int tw, th;
		Pixmap t = ptk_load_ppm(p, "fond.ppm", &tw, &th);

		if (t != None) {
			XSetWindowBackgroundPixmap(p->dpy, p->root, t);
			XClearWindow(p->dpy, p->root);
			XFreePixmap(p->dpy, t);
			return;
		}
	}
	int x, y;

	p->root_pattern = XCreatePixmap(p->dpy, p->root, 8, 8,
	    (unsigned)DefaultDepth(p->dpy, p->screen));
	ptk_fill(p, p->root_pattern, 0, 0, 8, 8, p->desktop);
	for (y = 0; y < 8; y += 2) {
		for (x = 0; x < 8; x += 2) {
			if (((x + y) / 2) % 2 == 0)
				ptk_fill(p, p->root_pattern, x, y, 1, 1,
				    p->desktop2);
		}
	}
	XSetWindowBackgroundPixmap(p->dpy, p->root, p->root_pattern);
	XClearWindow(p->dpy, p->root);
}

void
ptk_icon(Ptk *p, Drawable d, int x, int y, int size, const char *kind)
{
	int s = size;

	if (s < 12)
		s = 12;
	if (kind == NULL)
		kind = "app";

	if (strcmp(kind, "folder") == 0) {
		int bx = x + s / 8, by = y + s / 4;
		int bw = s - s / 4, bh = s - s / 4 - s / 8;
		ptk_fill(p, d, bx, y + s / 8, s / 3, s / 6, p->folder);
		ptk_rect(p, d, bx, y + s / 8, s / 3, s / 6, p->black);
		ptk_fill(p, d, bx, by, bw, bh, p->folder);
		ptk_rect(p, d, bx, by, bw, bh, p->black);
		ptk_line(p, d, bx + 1, by + 1, bx + bw - 2, by + 1, p->white);
		ptk_line(p, d, bx + 1, by + 1, bx + 1, by + bh - 2, p->white);
		ptk_line(p, d, bx + 2, by + bh - 2, bx + bw - 2,
		    by + bh - 2, p->folder_dark);
	} else if (strcmp(kind, "doc") == 0) {
		int bx = x + s / 5, by = y + s / 10;
		int bw = s - 2 * (s / 5), bh = s - 2 * (s / 10);
		ptk_fill(p, d, bx, by, bw, bh, p->paper);
		ptk_rect(p, d, bx, by, bw, bh, p->black);
		ptk_line(p, d, bx + bw - s / 6, by, bx + bw - 1,
		    by + s / 6, p->black);
		ptk_line(p, d, bx + bw - s / 6, by, bx + bw - s / 6,
		    by + s / 6, p->black);
		ptk_line(p, d, bx + 2, by + s / 3, bx + bw - 3,
		    by + s / 3, p->paper_shadow);
		ptk_line(p, d, bx + 2, by + s / 2, bx + bw - 3,
		    by + s / 2, p->paper_shadow);
		ptk_line(p, d, bx + 2, by + 2 * s / 3, bx + bw - 3,
		    by + 2 * s / 3, p->paper_shadow);
	} else if (strcmp(kind, "disk") == 0) {
		int bx = x + s / 6, by = y + s / 6;
		int bw = s - s / 3, bh = s - s / 3;
		ptk_fill(p, d, bx, by, bw, bh, p->disk);
		ptk_rect(p, d, bx, by, bw, bh, p->black);
		ptk_fill(p, d, bx + s / 6, by + s / 6, bw - s / 3, s / 5,
		    p->paper);
		ptk_rect(p, d, bx + s / 6, by + s / 6, bw - s / 3, s / 5,
		    p->black);
		ptk_fill(p, d, bx + s / 6, by + bh - s / 4, bw - s / 3,
		    s / 10, p->trash_dark);
	} else if (strcmp(kind, "trash") == 0) {
		int bx = x + s / 4, by = y + s / 4;
		int bw = s / 2, bh = s / 2;
		ptk_fill(p, d, bx - s / 10, by - s / 10, bw + s / 5,
		    s / 10, p->trash);
		ptk_rect(p, d, bx - s / 10, by - s / 10, bw + s / 5,
		    s / 10, p->black);
		ptk_fill(p, d, bx, by, bw, bh, p->trash);
		ptk_rect(p, d, bx, by, bw, bh, p->black);
		ptk_line(p, d, bx + bw / 3, by + 2, bx + bw / 3,
		    by + bh - 3, p->trash_dark);
		ptk_line(p, d, bx + 2 * bw / 3, by + 2,
		    bx + 2 * bw / 3, by + bh - 3, p->trash_dark);
	} else {
		int bx = x + s / 8, by = y + s / 6;
		int bw = s - s / 4, bh = s - s / 3;
		ptk_fill(p, d, bx, by, bw, bh, p->window);
		ptk_rect(p, d, bx, by, bw, bh, p->black);
		ptk_fill(p, d, bx + 1, by + 1, bw - 2, s / 5, p->menu);
		ptk_line(p, d, bx + 1, by + s / 5, bx + bw - 2,
		    by + s / 5, p->black);
		ptk_line(p, d, bx + s / 6, by + s / 2, bx + bw - s / 6,
		    by + s / 2, p->shadow);
		ptk_line(p, d, bx + s / 6, by + 2 * s / 3,
		    bx + bw - s / 6, by + 2 * s / 3, p->shadow);
	}
}

static void
asset_path(Ptk *p, const char *sub, const char *name, char *out, size_t outsz)
{
	int n;

	/* chemin trop long : chaîne vide, le chargement échoue proprement */
	if (sub != NULL && *sub != '\0')
		n = snprintf(out, outsz, "%s/%s/%s", p->assets_dir, sub, name);
	else
		n = snprintf(out, outsz, "%s/%s", p->assets_dir, name);
	if (n < 0 || (size_t)n >= outsz)
		out[0] = '\0';
}

/* format .argb : « ARGB », largeur et hauteur en 2 octets gros-boutistes, puis BGRA prémultiplié */
PtkArgb *
ptk_load_argb(Ptk *p, const char *name)
{
	char c[PATH_MAX];
	unsigned char t[8];
	unsigned char *px;
	FILE *f;
	PtkArgb *a;
	XImage *im;
	Pixmap pm;
	GC g;
	XRenderPictFormat *fmt;
	int w, h;

	if (snprintf(c, sizeof(c), "%s/icones/%s.argb", p->assets_dir, name) >=
	    (int)sizeof(c) || (f = fopen(c, "rb")) == NULL)
		return NULL;
	if (fread(t, 1, 8, f) != 8 || memcmp(t, "ARGB", 4) != 0) {
		fclose(f);
		return NULL;
	}
	w = t[4] << 8 | t[5];
	h = t[6] << 8 | t[7];
	if ((px = malloc((size_t)w * (size_t)h * 4)) == NULL ||
	    fread(px, 4, (size_t)w * (size_t)h, f) != (size_t)w * (size_t)h ||
	    (fmt = XRenderFindStandardFormat(p->dpy, PictStandardARGB32)) == NULL ||
	    (a = malloc(sizeof(*a))) == NULL) {
		free(px);
		fclose(f);
		return NULL;
	}
	fclose(f);
	pm = XCreatePixmap(p->dpy, p->root, (unsigned)w, (unsigned)h, 32);
	g = XCreateGC(p->dpy, pm, 0, NULL);
	a->alpha = malloc((size_t)w * (size_t)h);
	if (a->alpha != NULL) {
		int i;

		for (i = 0; i < w * h; i++)
			a->alpha[i] = px[4 * i + 3];
	}
	im = XCreateImage(p->dpy, NULL, 32, ZPixmap, 0, (char *)px, (unsigned)w, (unsigned)h, 32, w * 4);
	im->byte_order = LSBFirst;
	XPutImage(p->dpy, pm, g, im, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
	XDestroyImage(im);	/* libère aussi px */
	XFreeGC(p->dpy, g);
	a->pic = XRenderCreatePicture(p->dpy, pm, fmt, 0, NULL);
	a->w = w;
	a->h = h;
	XFreePixmap(p->dpy, pm);
	return a;
}

void
ptk_draw_argb(Ptk *p, Drawable d, const PtkArgb *a, int x, int y, int assombri)
{
	static Picture noir;
	XRenderPictFormat *fmt = XRenderFindVisualFormat(p->dpy, p->visual);
	Picture dst;

	if (a == NULL || fmt == NULL)
		return;
	dst = XRenderCreatePicture(p->dpy, d, fmt, 0, NULL);
	if (assombri == 3) {
		/* fantôme de l'icône d'un titre : sa vraie forme, translucide, sans foncer (relevé sur la VM) */
		XRenderColor m = { 0, 0, 0, 0x8ccc };
		Picture pm = XRenderCreateSolidFill(p->dpy, &m);

		XRenderComposite(p->dpy, PictOpOver, a->pic, pm, dst, 0, 0, 0, 0, x, y, (unsigned)a->w, (unsigned)a->h);
		XRenderFreePicture(p->dpy, pm);
		XRenderFreePicture(p->dpy, dst);
		return;
	}
	if (assombri == 2) {
		/* fantôme de glisser : icône foncée à 60 % d'opacité */
		XRenderColor m = { 0, 0, 0, 0x9999 }, n2 = { 0, 0, 0, 0x4ccc };
		Picture pm = XRenderCreateSolidFill(p->dpy, &m), pn = XRenderCreateSolidFill(p->dpy, &n2);

		XRenderComposite(p->dpy, PictOpOver, a->pic, pm, dst, 0, 0, 0, 0, x, y, (unsigned)a->w, (unsigned)a->h);
		XRenderComposite(p->dpy, PictOpOver, pn, a->pic, dst, 0, 0, 0, 0, x, y, (unsigned)a->w, (unsigned)a->h);
		XRenderFreePicture(p->dpy, pm);
		XRenderFreePicture(p->dpy, pn);
		XRenderFreePicture(p->dpy, dst);
		return;
	}
	XRenderComposite(p->dpy, PictOpOver, a->pic, None, dst, 0, 0, 0, 0, x, y, (unsigned)a->w, (unsigned)a->h);
	if (assombri) {	/* sélection, comme Mac OS : l'icône et son ombre foncent de moitié */
		if (noir == None) {
			XRenderColor c = { 0, 0, 0, 0x8000 };

			noir = XRenderCreateSolidFill(p->dpy, &c);
		}
		XRenderComposite(p->dpy, PictOpOver, noir, a->pic, dst, 0, 0, 0, 0, x, y, (unsigned)a->w, (unsigned)a->h);
	}
	XRenderFreePicture(p->dpy, dst);
}

int
ptk_load_xpm(Ptk *p, const char *name, Pixmap *pix, Pixmap *mask)
{
	char path[PATH_MAX];
	XpmAttributes attr;
	int rc;

	/* un chemin absolu (icône fournie avec un module, un programme) tel quel,
	 * sinon une icône des assets */
	if (name[0] == '/')
		snprintf(path, sizeof(path), "%s", name);
	else
		asset_path(p, "icones", name, path, sizeof(path));
	memset(&attr, 0, sizeof(attr));
	attr.valuemask = XpmVisual | XpmColormap | XpmDepth;
	attr.visual = p->visual;
	attr.colormap = p->cmap;
	attr.depth = DefaultDepth(p->dpy, p->screen);
	rc = XpmReadFileToPixmap(p->dpy, p->root, path, pix, mask, &attr);
	XpmFreeAttributes(&attr);
	if (rc != XpmSuccess) {
		fprintf(stderr, "ptk: XPM introuvable ou invalide: %s\n", path);
		*pix = None;
		if (mask)
			*mask = None;
		return -1;
	}
	return 0;
}

void
ptk_draw_pixmap(Ptk *p, Drawable d, Pixmap pix, Pixmap mask,
    int x, int y, int w, int h)
{
	if (pix == None)
		return;
	if (mask != None) {
		XSetClipOrigin(p->dpy, p->gc, x, y);
		XSetClipMask(p->dpy, p->gc, mask);
	}
	XCopyArea(p->dpy, pix, d, p->gc, 0, 0, (unsigned)w, (unsigned)h,
	    x, y);
	if (mask != None) {
		XSetClipMask(p->dpy, p->gc, None);
		XSetClipOrigin(p->dpy, p->gc, 0, 0);
	}
}

Pixmap
ptk_load_ppm(Ptk *p, const char *name, int *w, int *h)
{
	char path[PATH_MAX];
	FILE *f;
	char magic[3];
	int maxv, x, y;
	unsigned char *rgb;
	XImage *img;
	Pixmap pix;
	unsigned long rmax, gmax, bmax;
	int rshift, gshift, bshift;

	asset_path(p, NULL, name, path, sizeof(path));
	f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "ptk: PPM introuvable: %s\n", path);
		return None;
	}
	if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) {
		fclose(f);
		return None;
	}
	/* sauter les commentaires et blancs */
	{
		int c;
		while ((c = fgetc(f)) != EOF) {
			if (c == '#') {
				while ((c = fgetc(f)) != EOF && c != '\n')
					;
			} else if (c != ' ' && c != '\t' && c != '\n' &&
			    c != '\r') {
				ungetc(c, f);
				break;
			}
		}
	}
	if (fscanf(f, "%d %d %d", w, h, &maxv) != 3) {
		fclose(f);
		return None;
	}
	fgetc(f); /* fin du header */
	rgb = malloc((size_t)(*w) * (*h) * 3);
	if (rgb == NULL) {
		fclose(f);
		return None;
	}
	if (fread(rgb, 3, (size_t)(*w) * (*h), f) !=
	    (size_t)(*w) * (*h)) {
		free(rgb);
		fclose(f);
		return None;
	}
	fclose(f);

	img = XCreateImage(p->dpy, p->visual,
	    (unsigned)DefaultDepth(p->dpy, p->screen), ZPixmap, 0, NULL,
	    (unsigned)*w, (unsigned)*h, 32, 0);
	if (img == NULL) {
		free(rgb);
		return None;
	}
	img->data = calloc((size_t)img->bytes_per_line, (size_t)*h);
	if (img->data == NULL) {
		XDestroyImage(img);
		free(rgb);
		return None;
	}
	rmax = p->visual->red_mask >> 16;
	gmax = p->visual->green_mask >> 8;
	bmax = p->visual->blue_mask;
	rshift = 16;
	gshift = 8;
	bshift = 0;
	for (y = 0; y < *h; y++) {
		for (x = 0; x < *w; x++) {
			unsigned char *q = rgb + ((size_t)y * (*w) + x) * 3;
			unsigned long px =
			    ((unsigned long)q[0] * rmax / 255) << rshift |
			    ((unsigned long)q[1] * gmax / 255) << gshift |
			    ((unsigned long)q[2] * bmax / 255) << bshift;
			XPutPixel(img, x, y, px);
		}
	}
	pix = XCreatePixmap(p->dpy, p->root, (unsigned)*w, (unsigned)*h,
	    (unsigned)DefaultDepth(p->dpy, p->screen));
	XPutImage(p->dpy, pix, p->gc, img, 0, 0, 0, 0,
	    (unsigned)*w, (unsigned)*h);
	free(rgb);
	img->data = NULL;
	XDestroyImage(img);
	return pix;
}

Pixmap
ptk_tile_pixmap(Ptk *p, Pixmap tile, int tw, int th, int w, int h)
{
	Pixmap out;
	int x, y;

	out = XCreatePixmap(p->dpy, p->root, (unsigned)w, (unsigned)h,
	    (unsigned)DefaultDepth(p->dpy, p->screen));
	for (y = 0; y < h; y += th) {
		for (x = 0; x < w; x += tw) {
			int cw = (x + tw > w) ? w - x : tw;
			int ch = (y + th > h) ? h - y : th;
			XCopyArea(p->dpy, tile, out, p->gc, 0, 0,
			    (unsigned)cw, (unsigned)ch, x, y);
		}
	}
	return out;
}

static const char *
son_categorie(const char *code)
{
	if (strncmp(code, "mnu", 3) == 0)
		return "son_menus";
	if (code[0] == 'w')
		return "son_fenetres";
	if (code[0] == 'f' || strncmp(code, "dsk", 3) == 0 ||
	    strncmp(code, "dtdr", 4) == 0)
		return "son_finder";
	return "son_controles";
}

int
ptk_son(Ptk *p, const char *code)
{
	const char *journal = getenv("PLATINE_SONS_JOURNAL");
	const char *a = getenv("PLATINE_ASSETS");
	char motif[PATH_MAX], choix[64];
	int alerte = strcmp(code, "lerr") == 0;
	glob_t g;
	pid_t pid = -1;

	(void)p;
	if (journal != NULL) {
		FILE *f = fopen(journal, "a");
		if (f != NULL) {
			fprintf(f, "%s\n", code);
			fclose(f);
		}
	}
	if (getenv("PLATINE_SONS") == NULL)
		return -1;
	if (!alerte) {
		reglage_lire("theme_sonore", choix, sizeof(choix), "Platine");
		if (strcmp(choix, "Aucun") == 0 ||
		    !reglage_int(son_categorie(code), 1))
			return -1;
		snprintf(motif, sizeof(motif), "%s/sons/*%s*.wav",
		    a != NULL ? a : "/usr/local/share/platine/assets", code);
	} else {
		reglage_lire("son_alerte", choix, sizeof(choix), "simplebeep");
		snprintf(motif, sizeof(motif), "%s/alertes/%s.wav",
		    a != NULL ? a : "/usr/local/share/platine/assets", choix);
	}
	memset(&g, 0, sizeof(g));
	if (glob(motif, 0, NULL, &g) == 0 && g.gl_pathc > 0) {
		/* posix_spawn et non fork : le Finder est gros, le recopier retarde le prochain clic */
		{
			extern char **environ;
			posix_spawn_file_actions_t fa;
			char *av[] = { (char *)SYS_LECTEUR_SON, (char *)SYS_LECTEUR_OPT, g.gl_pathv[0], NULL };
			int i;

			posix_spawn_file_actions_init(&fa);
			for (i = 0; i < 3; i++)
				posix_spawn_file_actions_addopen(&fa, i, "/dev/null", O_RDWR, 0);
			if (posix_spawnp(&pid, SYS_LECTEUR_SON, &fa, NULL, av, environ) != 0)
				pid = -1;
			posix_spawn_file_actions_destroy(&fa);
		}
	}
	globfree(&g);
	return (int)pid;
}

void
ptk_son_stop(int pid)
{
	if (pid > 0)
		kill(pid, SIGTERM);
}
