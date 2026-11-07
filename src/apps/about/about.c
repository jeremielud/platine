/*
 * platine-about — « À propos de cet ordinateur », aux mesures de Mac OS 9.2 : bandeau en
 * relief avec la bannière encadrée, quatre lignes d'information, puis la liste des
 * applications et de la mémoire qu'elles occupent, avec barre dégradée.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <signal.h>
#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "../../../toolbox/ptk_sys.h"
#include "../../../toolbox/ptk.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("À propos de cet ordinateur", "pabt", "app-profil", PTK_VERSION, "");


#define W 466
#define H_MIN 252	/* comme Mac OS 9 ; la poignée allonge la liste */
#define H_MAX 390
#define LISTE 168
#define BARRE 16
#define MAX_APPS 16

static Display *dpy;
static Ptk ptk;
static int H = H_MIN;
static int plus_enfonce, plus_dedans;
static Window win;
static Pixmap banniere;
static int ban_w, ban_h;
static Atom a_delete, a_cmd;

typedef struct { char nom[64]; char icone[32]; long ko; } App;

static PtkColor
gris(int v)
{
	char n[8];

	snprintf(n, sizeof(n), "#%02x%02x%02x", v, v, v);
	return ptk_color(&ptk, n);
}

static long
meminfo(const char *cle)
{
	FILE *f = fopen("/proc/meminfo", "r");
	char l[128];
	long v = 0;
	size_t n = strlen(cle);

	if (f == NULL)
		return 0;
	while (fgets(l, sizeof(l), f) != NULL)
		if (strncmp(l, cle, n) == 0 && l[n] == ':') {
			v = atol(l + n + 1);
			break;
		}
	fclose(f);
	return v;
}

static void commande_ligne(const char *cmd, char *b, size_t n);
/* mémoire libre en Ko. NetBSD compte les exécutables dans « Cached » : on repart de ce qui est
 * vraiment occupé (pages anonymes, câblées, code) */
static long
memoire_libre(void)
{
#ifdef __NetBSD__
	char l[32] = "";

	commande_ligne("vmstat -s 2>/dev/null | awk '/anonymous pages|cached executable pages|pages wired/ { p += $1 } "
	    "/bytes per page/ { t = $1 } END { printf \"%d\", p * t / 1024 }'", l, sizeof(l));
	if (l[0] != '\0')
		return meminfo("MemTotal") - atol(l);
#endif
	return meminfo("MemFree") + meminfo("Buffers") + meminfo("Cached");
}


/* nom affiché et icône d'un processus ; NULL : tâche de fond, comptée avec le système */
static const char *
nom_app(const char *comm, const char *cmd, const char **icone)
{
	static const struct { const char *cle, *nom, *ic; } t[] = {
		{ "tableaux/", "Tableau de bord", "dossier-tableaux" },
		/* arborescence du volume : System et Applications */
		{ "Control Panels/", "Tableau de bord", "dossier-tableaux" },
		{ "Tableaux de bord/", "Tableau de bord", "dossier-tableaux" },
		{ "System/Finder", "Finder", "app-finder" },
		{ "Extensions/À propos", "À propos de cet ordinateur", "app-profil" },
		{ "Applications/TextEdit", "TextEdit", "app-texte" },
		{ "Applications/Théière", "Théière", "application" },
		{ "Apple Menu Items/Calculette", "Calculette", "app-calculette" },
		{ "platine-textedit", "TextEdit", "app-texte" },
		{ "platine-finder", "Finder", "app-finder" },
		{ "platine-about", "À propos de cet ordinateur", "app-profil" },
		{ "scummvm", "ScummVM", "application" },
		{ "olympe", "Olympe", "application" },
		{ "netsurf", "Classilla", "app-classilla" },
		{ "xterm", "Terminal", "app-terminal" },
		{ "mousepad", "Texte simple", "app-texte" },
		{ "gpicview", "Album", "app-album" },
		{ "xcalc", "Calculette", "app-calculette" },
	};
	size_t i;

	(void)comm;
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (strstr(cmd, t[i].cle) != NULL) {
			*icone = t[i].ic;
			return t[i].nom;
		}
	return NULL;
}

static int
lister(App *a, int max, long *total_apps)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int n = 0, i;

	*total_apps = 0;
	if (d == NULL)
		return 0;
	while ((e = readdir(d)) != NULL) {
		char c[300], cmd[512] = "", l[128];
		const char *nom, *ic = "application";
		FILE *f;
		long ko = 0;

		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		snprintf(c, sizeof(c), "/proc/%s/cmdline", e->d_name);
		if ((f = fopen(c, "r")) == NULL)
			continue;
		size_t k = fread(cmd, 1, sizeof(cmd) - 1, f);
		fclose(f);
		for (size_t j = 0; j < k; j++)
			if (cmd[j] == '\0')
				cmd[j] = ' ';
		cmd[k] = '\0';
		if ((nom = nom_app(NULL, cmd, &ic)) == NULL)
			continue;
#ifdef __NetBSD__
		/* le procfs de NetBSD ne donne pas VmRSS */
		snprintf(c, sizeof(c), "ps -o rss= -p %s", e->d_name);
		if ((f = popen(c, "r")) == NULL)
			continue;
		if (fgets(l, sizeof(l), f) != NULL)
			ko = atol(l);
		pclose(f);
#else
		snprintf(c, sizeof(c), "/proc/%s/status", e->d_name);
		if ((f = fopen(c, "r")) == NULL)
			continue;
		while (fgets(l, sizeof(l), f) != NULL)
			if (strncmp(l, "VmRSS:", 6) == 0)
				ko = atol(l + 6);
		fclose(f);
#endif
		if (ko == 0)
			continue;
		*total_apps += ko;
		for (i = 0; i < n; i++)
			if (strcmp(a[i].nom, nom) == 0)
				break;
		if (i == n) {
			if (n >= max)
				continue;
			snprintf(a[n].nom, sizeof(a[n].nom), "%s", nom);
			snprintf(a[n].icone, sizeof(a[n].icone), "%s", ic);
			a[n].ko = 0;
			n++;
		}
		a[i].ko += ko;
	}
	closedir(d);
	return n;
}

static void
mo(char *b, size_t n, long ko)
{
	if (ko >= 10240)
		snprintf(b, n, "%ld Mo", ko / 1024);
	else
		snprintf(b, n, "%.1f Mo", ko / 1024.0);
	for (char *p = b; *p; p++)
		if (*p == '.')
			*p = ',';
}

/* barre : cadre noir, ombre #aa en haut à gauche, blanc en bas à droite, dégradé par rangée */
static void
barre_memoire(Drawable d, int x, int y, int l, double part)
{
	static const char *const r[10] = {
		"#333399", "#6666cc", "#9999ff", "#ccccff", "#eeeeee",
		"#ccccff", "#9999ff", "#6666cc", "#333399", "#000055",
	};
	int f = (int)((l - 2) * part), i;

	if (f < 2)
		f = 2;
	if (f > l - 2)
		f = l - 2;
	ptk_fill(&ptk, d, x - 1, y - 1, l + 1, 1, gris(0xaa));
	ptk_fill(&ptk, d, x - 1, y - 1, 1, 13, gris(0xaa));
	ptk_fill(&ptk, d, x + l, y - 1, 1, 13, ptk.white);
	ptk_fill(&ptk, d, x, y + 12, l + 1, 1, ptk.white);
	ptk_rect(&ptk, d, x, y, l, 12, ptk.black);
	ptk_fill(&ptk, d, x + 1, y + 1, l - 2, 10, gris(0xee));
	for (i = 0; i < 10; i++)
		ptk_fill(&ptk, d, x + 1, y + 1 + i, f, 1, ptk_color(&ptk, r[i]));
	ptk_fill(&ptk, d, x + 1, y + 1, 1, 10, ptk_color(&ptk, "#6666cc"));
	ptk_fill(&ptk, d, x + f, y + 1, 1, 10, ptk_color(&ptk, "#000055"));
}

static void commande_ligne(const char *cmd, char *b, size_t n);

static void
lire_ligne(const char *chemin, char *b, size_t n)
{
	FILE *f = fopen(chemin, "r");

	b[0] = '\0';
	if (f != NULL) {
		if (fgets(b, (int)n, f) != NULL)
			b[strcspn(b, "\n")] = '\0';
		fclose(f);
	}
}

/* « ASUSTeK Eee PC 701 » plutôt que le nom complet du constructeur */
static void
nom_ordinateur(char *b, size_t n)
{
	char vend[64], prod[64];

	commande_ligne(SYS_FABRICANT, vend, sizeof(vend));
	commande_ligne(SYS_MODELE, prod, sizeof(prod));
	vend[strcspn(vend, " ")] = '\0';
	if (strncmp(vend, "ASUS", 4) == 0 && strcmp(prod, "701") == 0)
		snprintf(b, n, "%s Eee PC 701", vend);
	else
		snprintf(b, n, "%s %s", vend, prod);
}

static void
distribution(char *b, size_t n)
{
	char rel[32];

	struct utsname u;

	(void)rel;
	if (uname(&u) != 0) {
		snprintf(b, n, "Inconnu");
		return;
	}
	if (strcmp(u.sysname, "Linux") == 0) {
		char nom[96] = "";

		commande_ligne("sed -n 's/^PRETTY_NAME=\"\\{0,1\\}\\([^\"]*\\)\"\\{0,1\\}$/\\1/p' /etc/os-release 2>/dev/null",
		    nom, sizeof(nom));
		snprintf(b, n, "%s", nom[0] != '\0' ? nom : "Linux");
	} else
		snprintf(b, n, "%s %s", u.sysname, u.release);
}

/* nom du système, sans version, et son logo : netbsd, openbsd, haiku ou linux (Tux) */
static const char *
logo_systeme(char *nom, size_t n)
{
	struct utsname u;

	if (uname(&u) != 0)
		u.sysname[0] = '\0';
	if (strcmp(u.sysname, "Linux") == 0) {
		commande_ligne("sed -n 's/^NAME=\"\\{0,1\\}\\([^\"]*\\)\"\\{0,1\\}$/\\1/p' /etc/os-release 2>/dev/null", nom, n);
		if (nom[0] == '\0')
			snprintf(nom, n, "Linux");
		return "os-linux-16.xpm";
	}
	snprintf(nom, n, "%s", u.sysname);
	if (strcmp(u.sysname, "NetBSD") == 0)
		return "os-netbsd-16.xpm";
	if (strcmp(u.sysname, "OpenBSD") == 0)
		return "os-openbsd-16.xpm";
	if (strcmp(u.sysname, "Haiku") == 0)
		return "os-haiku-16.xpm";
	return "app-finder-16.xpm";
}

/* bouton « Plus d'informations… », à droite de la dernière ligne */
#define PLUS_W 146
#define PLUS_H 20
#define PLUS_X (456 - PLUS_W)
#define PLUS_Y 144
static void
bouton_plus(Drawable d)
{
	ptk_button(&ptk, d, PLUS_X, PLUS_Y, PLUS_W, PLUS_H, "Plus d'informations…",
	    plus_enfonce && plus_dedans, 0, ptk.text);
}

static void
dessiner(void)
{
	/* toujours à la hauteur maximale : X répète le fond d'une fenêtre qui grandit,
	 * la bannière réapparaîtrait en bas le temps de redessiner */
	Pixmap b = XCreatePixmap(dpy, win, W, H_MAX, DefaultDepth(dpy, ptk.screen));
	static XftFont *gras;
	App apps[MAX_APPS];
	char v[64];
	long total = meminfo("MemTotal"), libre = memoire_libre();
	long sw = meminfo("SwapTotal"), sw_l = meminfo("SwapFree"), apps_ko;
	long utilise = total - libre;
	int n = lister(apps, MAX_APPS, &apps_ko), i, y;
	static const char *const lib[4] = { "Ordinateur :", "Version :", "Mémoire intégrée :", "Mémoire virtuelle :" };
	int lx = 0;
	char val[4][128];

	if (gras == NULL)
		gras = ptk_police_bitmap(&ptk, "geneva-10-gras.bdf", 10);
	if (gras == NULL)
		gras = ptk.font_small;
	/* bandeau */
	ptk_fill(&ptk, b, 0, 0, W, H, gris(0xdd));
	ptk_fill(&ptk, b, 0, 0, W - 1, 1, ptk.white);
	ptk_fill(&ptk, b, 0, 0, 1, LISTE - 2, ptk.white);
	ptk_fill(&ptk, b, W - 1, 1, 1, LISTE - 1, gris(0xaa));
	ptk_fill(&ptk, b, 1, LISTE - 2, W - 1, 1, gris(0xaa));
	ptk_fill(&ptk, b, 0, LISTE - 1, W, 1, ptk.black);
	/* bannière encadrée : ombre #88, cadre noir, blanc en bas à droite */
	ptk_fill(&ptk, b, 4, 4, 457, 1, gris(0x88));
	ptk_fill(&ptk, b, 4, 4, 1, 93, gris(0x88));
	ptk_fill(&ptk, b, 5, 97, 457, 1, ptk.white);
	ptk_fill(&ptk, b, 461, 5, 1, 93, ptk.white);
	ptk_rect(&ptk, b, 5, 5, 456, 92, ptk.black);
	ptk_fill(&ptk, b, 6, 6, 454, 90, ptk.white);
	if (banniere != None)
		XCopyArea(dpy, banniere, b, ptk.gc, 0, 0, (unsigned)ban_w, (unsigned)ban_h, 6, 6);
	/* quatre lignes concises, comme l'original ; le détail est dans « Plus d'informations… » */
	nom_ordinateur(val[0], sizeof(val[0]));
	distribution(val[1], sizeof(val[1]));
	{
		long u_ko, t_ko;

		ptk_memoire(&u_ko, &t_ko);
		snprintf(val[2], sizeof(val[2]), "%ld / %ld Mo", u_ko / 1024, ptk_memoire_installee_mo(t_ko));
	}
	if (sw > 0) {
		mo(v, sizeof(v), sw - sw_l);
		snprintf(val[3], sizeof(val[3]), "%s utilisés sur %ld Mo", v, sw / 1024);
	} else
		snprintf(val[3], sizeof(val[3]), "Désactivée");
	for (i = 0; i < 4; i++) {
		int l = ptk_text_width(&ptk, gras, lib[i]);
		if (l > lx)
			lx = l;
	}
	for (i = 0; i < 4; i++) {
		ptk_text(&ptk, b, gras, 8, 112 + 16 * i, lib[i], ptk.text);
		ptk_text(&ptk, b, ptk.font_small, 8 + lx + 8, 112 + 16 * i, val[i], ptk.text);
	}
	bouton_plus(b);
	/* liste */
	ptk_fill(&ptk, b, 0, LISTE, W - BARRE, H - LISTE, gris(0xee));
	y = LISTE + 2;
	{
		Pixmap ip = None, im = None;
		long sys_ko = utilise - apps_ko;

		char sys_nom[64];
		const char *logo = logo_systeme(sys_nom, sizeof(sys_nom));

		ptk_load_xpm(&ptk, logo, &ip, &im);
		if (ip != None)
			ptk_draw_pixmap(&ptk, b, ip, im, 16, y, 16, 16);
		ptk_text(&ptk, b, ptk.font_small, 41, y + 11, sys_nom, ptk.text);
		mo(v, sizeof(v), sys_ko > 0 ? sys_ko : 0);
		ptk_text(&ptk, b, ptk.font_small, 247 - ptk_text_width(&ptk, ptk.font_small, v), y + 11, v, ptk.text);
		barre_memoire(b, 258, y + 2, 182, utilise > 0 ? (double)sys_ko / utilise : 1);
		y += 18;
	}
	for (i = 0; i < n && y + 16 < H; i++, y += 18) {
		char ic[64];
		Pixmap ip = None, im = None;

		snprintf(ic, sizeof(ic), "menu/%s.xpm", apps[i].icone);
		if (ptk_load_xpm(&ptk, ic, &ip, &im) == 0 && ip != None)
			ptk_draw_pixmap(&ptk, b, ip, im, 16, y, 16, 16);
		ptk_text(&ptk, b, ptk.font_small, 41, y + 11, apps[i].nom, ptk.text);
		mo(v, sizeof(v), apps[i].ko);
		ptk_text(&ptk, b, ptk.font_small, 247 - ptk_text_width(&ptk, ptk.font_small, v), y + 11, v, ptk.text);
		barre_memoire(b, 258, y + 2, 182, utilise > 0 ? (double)apps[i].ko / utilise : 0);
	}
	{
		PtkScrollbar s = { 0 };

		s.x = W - BARRE;
		s.y = LISTE;
		s.w = BARRE;
		s.h = H - LISTE;
		s.page = s.h;
		s.active = 1;
		ptk_scrollbar_draw(&ptk, b, &s);
	}
	ptk_fill(&ptk, b, W - BARRE, LISTE - 1, 1, 1, ptk.black);
	ptk_fill(&ptk, b, W - BARRE + 1, LISTE - 1, 14, 1, gris(0x55));
	/* sous la taille actuelle : la liste se prolonge, le temps qu'un agrandissement soit redessiné */
	if (H < H_MAX) {
		ptk_fill(&ptk, b, 0, H, W - BARRE, H_MAX - H, gris(0xee));
		ptk_fill(&ptk, b, W - BARRE, H, 1, H_MAX - H, ptk.black);
		ptk_fill(&ptk, b, W - BARRE + 1, H, 14, H_MAX - H, gris(0xee));
	}
	XSetWindowBackgroundPixmap(dpy, win, b);
	XClearWindow(dpy, win);
	XFreePixmap(dpy, b);
	XFlush(dpy);
}

static void
prop(Atom a, const char *s)
{
	XChangeProperty(dpy, win, a, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
	    (const unsigned char *)s, (int)strlen(s));
}

/* --- « Plus d'informations » : toutes les caractéristiques de l'ordinateur -------------- */
static void
commande_ligne(const char *cmd, char *b, size_t n)
{
	FILE *f = popen(cmd, "r");

	b[0] = '\0';
	if (f != NULL) {
		if (fgets(b, (int)n, f) != NULL)
			b[strcspn(b, "\n")] = '\0';
		pclose(f);
	}
}

#define PI_W 440
static char pi_lib[16][32], pi_val[16][160];
static int pi_n, pi_h, pi_ok_enfonce, pi_ok_dedans;
/* moniteur du processeur : 60 secondes d'historique, sous la liste */
#define CPU_N 60
#define CPU_H 74
static int cpu_hist[CPU_N], cpu_n;

static void
cpu_echantillon(void)
{
	int c = ptk_cpu_charge();

	if (c < 0)
		return;
	if (cpu_n == CPU_N)
		memmove(cpu_hist, cpu_hist + 1, sizeof(cpu_hist) - sizeof(cpu_hist[0]));
	else
		cpu_n++;
	cpu_hist[cpu_n - 1] = c;
}

static void
pi_ajoute(const char *l, const char *v)
{
	if (v[0] == '\0' || pi_n >= 16)
		return;
	snprintf(pi_lib[pi_n], sizeof(pi_lib[0]), "%s :", l);
	snprintf(pi_val[pi_n], sizeof(pi_val[0]), "%s", v);
	pi_n++;
}

static void
pi_collecter(void)
{
	char b[160], c[160];
	char vend[64], prod[64];
	struct utsname un;
	struct statvfs st;
	FILE *f;
	long libre = memoire_libre();

	pi_n = 0;
	commande_ligne(SYS_FABRICANT, vend, sizeof(vend));
	commande_ligne(SYS_MODELE, prod, sizeof(prod));
	snprintf(b, sizeof(b), "%s %s", vend, prod);
	pi_ajoute("Ordinateur", b);
	distribution(b, sizeof(b));
	pi_ajoute("Distribution", b);
	if (uname(&un) == 0) {
		snprintf(b, sizeof(b), "%s %s (%s)", un.sysname, un.release, un.machine);
		pi_ajoute("Noyau", b);
	}
	b[0] = '\0';
	if ((f = fopen("/proc/cpuinfo", "r")) != NULL) {
		char l[256];

		while (fgets(l, sizeof(l), f))
			if (strncmp(l, "model name", 10) == 0 && strchr(l, ':')) {
				char *q = strchr(l, ':') + 2;

				q[strcspn(q, "\n")] = '\0';
				for (char *e = q; *e; )
					if (e[0] == ' ' && e[1] == ' ')
						memmove(e, e + 1, strlen(e));
					else
						e++;
				snprintf(b, sizeof(b), "%.150s", q);
				break;
			}
		fclose(f);
	}
	pi_ajoute("Processeur", b);
	{
		long u_ko, t_ko;

		ptk_memoire(&u_ko, &t_ko);
		snprintf(b, sizeof(b), "%ld / %ld Mo", u_ko / 1024, ptk_memoire_installee_mo(t_ko));
	}
	pi_ajoute("Mémoire intégrée", b);
	if (meminfo("SwapTotal") > 0) {
		char v[64];

		mo(v, sizeof(v), meminfo("SwapTotal") - meminfo("SwapFree"));
		snprintf(b, sizeof(b), "%s utilisés sur %ld Mo d'échange", v, meminfo("SwapTotal") / 1024);
	} else
		snprintf(b, sizeof(b), "Désactivée");
	pi_ajoute("Mémoire virtuelle", b);
	mo(b, sizeof(b), libre);
	pi_ajoute("Plus grand bloc libre", b);
	if (statvfs("/", &st) == 0) {
		double u = 1024.0 * 1024.0 * 1024.0;

		snprintf(b, sizeof(b), "%.2f Go libres sur %.2f Go",
		    (double)st.f_bavail * st.f_frsize / u, (double)st.f_blocks * st.f_frsize / u);
		for (char *e = b; *e; e++)
			if (*e == '.')
				*e = ',';
		pi_ajoute("Disque", b);
	}
	commande_ligne(SYS_SON, b, sizeof(b));
	snprintf(b, sizeof(b), "%d x %d%s%.120s", DisplayWidth(dpy, ptk.screen), DisplayHeight(dpy, ptk.screen),
	    c[0] ? ", " : "", c);
	pi_ajoute("Affichage", b);
	commande_ligne("lspci 2>/dev/null | sed -n 's/.*Audio device: //p' | head -1", b, sizeof(b));
	pi_ajoute("Son", b);
	commande_ligne(SYS_WIFI_SSID, c, sizeof(c));
	commande_ligne(SYS_IP, b, sizeof(b));
	if (c[0]) {
		char t[160];

		snprintf(t, sizeof(t), "AirPort, %.60s%s%.40s", c, b[0] ? ", " : "", b);
		pi_ajoute("Réseau", t);
	} else
		pi_ajoute("Réseau", b);
	commande_ligne(SYS_BATTERIE, c, sizeof(c));
	if (c[0]) {
		snprintf(b, sizeof(b), "%s %%", c);
		pi_ajoute("Batterie", b);
	}
	if ((f = fopen("/proc/uptime", "r")) != NULL) {
		double up = 0;

		if (fscanf(f, "%lf", &up) == 1) {
			snprintf(b, sizeof(b), "%d h %02d", (int)up / 3600, (int)up / 60 % 60);
			pi_ajoute("Allumé depuis", b);
		}
		fclose(f);
	}
	pi_h = 12 + pi_n * 16 + 40 + CPU_H;
}

static void
pi_dessiner(void)
{
	Pixmap b = XCreatePixmap(dpy, win, PI_W, (unsigned)pi_h, DefaultDepth(dpy, ptk.screen));
	XftFont *gras = ptk_police_bitmap(&ptk, "geneva-10-gras.bdf", 10);
	int i, lx = 0;

	if (gras == NULL)
		gras = ptk.font_small;
	ptk_fill(&ptk, b, 0, 0, PI_W, pi_h, gris(0xdd));
	ptk_fill(&ptk, b, 0, 0, PI_W - 1, 1, ptk.white);
	ptk_fill(&ptk, b, 0, 0, 1, pi_h - 1, ptk.white);
	ptk_fill(&ptk, b, PI_W - 1, 1, 1, pi_h - 1, gris(0xaa));
	ptk_fill(&ptk, b, 1, pi_h - 1, PI_W - 1, 1, gris(0xaa));
	for (i = 0; i < pi_n; i++) {
		int l = ptk_text_width(&ptk, gras, pi_lib[i]);

		if (l > lx)
			lx = l;
	}
	for (i = 0; i < pi_n; i++) {
		char t[160];

		ptk_text(&ptk, b, gras, 12, 22 + 16 * i, pi_lib[i], ptk.text);
		ptk_fit_text(&ptk, ptk.font_small, pi_val[i], t, sizeof(t), PI_W - 12 - lx - 8 - 12);
		ptk_text(&ptk, b, ptk.font_small, 12 + lx + 8, 22 + 16 * i, t, ptk.text);
	}
	{
		/* activité du processeur : cadre gravé, une barre de 4 px par seconde */
		int gx = 12 + lx + 8, gy = 12 + pi_n * 16 + 14, gw = CPU_N * 4, gh = 40, k;
		char t[48];

		ptk_text(&ptk, b, gras, 12, gy + 12, "Processeur :", ptk.text);
		snprintf(t, sizeof(t), cpu_n ? "%d %% utilisé" : "mesure…", cpu_n ? cpu_hist[cpu_n - 1] : 0);
		ptk_text(&ptk, b, ptk.font_small, 12, gy + 28, t, ptk.text);
		ptk_fill(&ptk, b, gx - 1, gy - 1, gw + 2, 1, gris(0x88));
		ptk_fill(&ptk, b, gx - 1, gy - 1, 1, gh + 2, gris(0x88));
		ptk_fill(&ptk, b, gx - 1, gy + gh + 1, gw + 3, 1, ptk.white);
		ptk_fill(&ptk, b, gx + gw + 1, gy - 1, 1, gh + 3, ptk.white);
		ptk_rect(&ptk, b, gx, gy, gw + 1, gh + 1, ptk.black);
		ptk_fill(&ptk, b, gx + 1, gy + 1, gw - 1, gh - 1, ptk.white);
		for (k = 1; k < 4; k++)	/* repères à 25, 50 et 75 % */
			ptk_fill(&ptk, b, gx + 1, gy + gh * k / 4, gw - 1, 1, gris(0xdd));
		for (k = 0; k < cpu_n; k++) {
			int v = cpu_hist[k] * (gh - 2) / 100, x = gx + 1 + (CPU_N - cpu_n + k) * 4;

			if (v > 0)
				ptk_fill(&ptk, b, x, gy + gh - v, 3, v, ptk_color(&ptk, "#333399"));
		}
	}
	ptk_button(&ptk, b, PI_W - 12 - 70, pi_h - 32, 70, 20, "OK", pi_ok_enfonce && pi_ok_dedans, 1, ptk.text);
	XSetWindowBackgroundPixmap(dpy, win, b);
	XClearWindow(dpy, win);
	XFreePixmap(dpy, b);
	XFlush(dpy);
}

static int
pi_dans_ok(int x, int y)
{
	return x >= PI_W - 82 && x < PI_W - 12 && y >= pi_h - 32 && y < pi_h - 12;
}

static int
plus_infos(void)
{
	XSizeHints h;
	XEvent ev;

	pi_collecter();
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy),
	    (DisplayWidth(dpy, ptk.screen) - PI_W) / 2 - 6, 50, PI_W, (unsigned)pi_h, 0, 0, gris(0xdd).pixel);
	XStoreName(dpy, win, "Plus d'informations");
	prop(XInternAtom(dpy, "_NET_WM_NAME", False), "Plus d'informations");
	prop(XInternAtom(dpy, "_PLATINE_APP_NAME", False), "Finder");
	prop(XInternAtom(dpy, "_PLATINE_ICON", False), "aucune");
	prop(XInternAtom(dpy, "_PLATINE_MENU", False), "Fichier|Fermer la fenêtre\t⌘W\nAide|Aide Platine");
	a_cmd = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &a_delete, 1);
	h.flags = PMinSize | PMaxSize;
	h.min_width = h.max_width = PI_W;
	h.min_height = h.max_height = pi_h;
	XSetWMNormalHints(dpy, win, &h);
	XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | KeyPressMask);
	XMapWindow(dpy, win);
	cpu_echantillon();
	pi_dessiner();
	for (;;) {
		struct pollfd pf = { ConnectionNumber(dpy), POLLIN, 0 };

		if (!XPending(dpy) && poll(&pf, 1, 1000) == 0) {
			cpu_echantillon();
			pi_dessiner();
			continue;
		}
		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage)
			return 0;
		if (ev.type == KeyPress) {
			KeySym k = XLookupKeysym(&ev.xkey, 0);

			if (k == XK_Return || k == XK_KP_Enter || k == XK_Escape)
				return 0;
		}
		if (ev.type == ButtonPress && pi_dans_ok(ev.xbutton.x, ev.xbutton.y)) {
			pi_ok_enfonce = pi_ok_dedans = 1;
			ptk_son(&ptk, "btnp");
			pi_dessiner();
		} else if (ev.type == MotionNotify && pi_ok_enfonce) {
			int d = pi_dans_ok(ev.xmotion.x, ev.xmotion.y);

			if (d != pi_ok_dedans) {
				pi_ok_dedans = d;
				pi_dessiner();
			}
		} else if (ev.type == ButtonRelease && pi_ok_enfonce) {
			int d = pi_dans_ok(ev.xbutton.x, ev.xbutton.y);

			pi_ok_enfonce = 0;
			ptk_son(&ptk, "btnr");
			if (d)
				return 0;
			pi_dessiner();
		}
	}
}

static int
dans_plus(int x, int y)
{
	return x >= PLUS_X && x < PLUS_X + PLUS_W && y >= PLUS_Y && y < PLUS_Y + PLUS_H;
}

int
main(int argc, char **argv)
{
	XSizeHints h;
	XEvent ev;

	ptk_ramasser_enfants();	/* pas SIG_IGN : hérité par les programmes lancés */
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL || ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;
	if (argc > 1 && strcmp(argv[1], "plus") == 0)
		return plus_infos();
	int x = (DisplayWidth(dpy, ptk.screen) - W) / 2 - 6;
	banniere = ptk_load_ppm(&ptk, "apropos.ppm", &ban_w, &ban_h);
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), x, 60, W, (unsigned)H, 0, 0, gris(0xdd).pixel);
	XStoreName(dpy, win, "À propos de cet ordinateur");
	prop(XInternAtom(dpy, "_NET_WM_NAME", False), "À propos de cet ordinateur");
	prop(XInternAtom(dpy, "_PLATINE_APP_NAME", False), "Finder");
	prop(XInternAtom(dpy, "_PLATINE_ICON", False), "aucune");
	prop(XInternAtom(dpy, "_PLATINE_MENU", False), "Fichier|Fermer la fenêtre\t⌘W\nAide|Aide Platine");
	a_cmd = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &a_delete, 1);
	h.flags = PPosition | PMinSize | PMaxSize;
	h.x = x;
	h.y = 60;
	h.min_width = h.max_width = W;
	h.min_height = H_MIN;
	h.max_height = H_MAX;
	XSetWMNormalHints(dpy, win, &h);
	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | ButtonPressMask |
	    ButtonReleaseMask | ButtonMotionMask);
	XMapWindow(dpy, win);
	dessiner();
	for (;;) {
		struct pollfd pf = { ConnectionNumber(dpy), POLLIN, 0 };

		if (!XPending(dpy) && poll(&pf, 1, 3000) == 0) {
			dessiner();
			continue;
		}
		XNextEvent(dpy, &ev);
		if (ev.type == ConfigureNotify && ev.xconfigure.height != H) {
			H = ev.xconfigure.height;
			dessiner();
		} else if (ev.type == ButtonPress && dans_plus(ev.xbutton.x, ev.xbutton.y)) {
			plus_enfonce = plus_dedans = 1;
			ptk_son(&ptk, "btnp");
			dessiner();
		} else if (ev.type == MotionNotify && plus_enfonce) {
			int d = dans_plus(ev.xmotion.x, ev.xmotion.y);

			if (d != plus_dedans) {
				plus_dedans = d;
				dessiner();
			}
		} else if (ev.type == ButtonRelease && plus_enfonce) {
			int d = dans_plus(ev.xbutton.x, ev.xbutton.y);

			plus_enfonce = 0;
			ptk_son(&ptk, "btnr");
			dessiner();
			if (d && fork() == 0) {
				char moi[512];
				ssize_t n = readlink("/proc/self/exe", moi, sizeof(moi) - 1);

				if (n > 0) {
					moi[n] = '\0';
					execl(moi, "platine-about", "plus", (char *)NULL);
				}
				_exit(127);
			}
		}
		if (ev.type == ClientMessage) {
			if (ev.xclient.message_type == a_cmd)
				return 0;	/* « Fermer la fenêtre » */
			if ((Atom)ev.xclient.data.l[0] == a_delete)
				return 0;
		}
	}
}
