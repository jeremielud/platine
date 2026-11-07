/*
 * platine-demarrage FIFO : séquence de démarrage, copie de celle de Mac OS 9.2 relevée image par image
 * sur la VM (voir docs/05). Fond gris, Happy Mac, flèche en haut à gauche ; fenêtre au logo sans
 * curseur ; le motif du bureau remplace le gris ; « Démarrage… » et barre ; balle qui tourne pendant
 * que chaque étape réelle de la session (une ligne « icône libellé » dans le tube) pose son icône
 * en bas à gauche ; la fenêtre disparaît, montre, et le gestionnaire de fenêtres prend la suite.
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "../../../toolbox/ptk.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Écran de démarrage", "pdem", "application", PTK_VERSION, "");


#define ETAT_RC "/var/run/platine-demarrage.etat"
#define IMAGE_RC "/var/run/platine-demarrage.ppm"
static int total_rc, reprise;

#define FW 422
#define FH 323
#define MAX_EXT 48

static Ptk ptk;
static Display *dpy;
static Window win;
static Pixmap ecran;
static int sw, sh, fx, fy;
static Cursor fleche, vide, balle[8], montre[7];
static PtkArgb *ext[MAX_EXT];
static int n_ext, total = 12;
static Pixmap logo, fond, happy;
static int logo_w, logo_h, fond_w, fond_h, happy_w, happy_h;

static Cursor
curseur_bits(const unsigned short *d, const unsigned short *m, int hx, int hy)
{
	unsigned char bd[32], bm[32];
	XColor noir = { 0, 0, 0, 0, DoRed | DoGreen | DoBlue, 0 };
	XColor blanc = { 0, 0xffff, 0xffff, 0xffff, DoRed | DoGreen | DoBlue, 0 };
	Pixmap pd, pm;
	Cursor c;
	int i, b;

	/* X veut le bit de poids faible à gauche, QuickDraw l'inverse */
	for (i = 0; i < 16; i++) {
		unsigned v = 0, w = 0;

		for (b = 0; b < 16; b++) {
			if (d[i] & (0x8000u >> b))
				v |= 1u << b;
			if (m[i] & (0x8000u >> b))
				w |= 1u << b;
		}
		bd[2 * i] = (unsigned char)v;
		bd[2 * i + 1] = (unsigned char)(v >> 8);
		bm[2 * i] = (unsigned char)w;
		bm[2 * i + 1] = (unsigned char)(w >> 8);
	}
	pd = XCreateBitmapFromData(dpy, ptk.root, (char *)bd, 16, 16);
	pm = XCreateBitmapFromData(dpy, ptk.root, (char *)bm, 16, 16);
	c = XCreatePixmapCursor(dpy, pd, pm, &noir, &blanc, (unsigned)hx, (unsigned)hy);
	XFreePixmap(dpy, pd);
	XFreePixmap(dpy, pm);
	return c;
}

/* fichier .cur : ressource CURS telle quelle (data 16×2, masque 16×2, point sensible) */
static Cursor
curseur_fichier(const char *nom)
{
	char c[PATH_MAX];
	unsigned char t[68];
	unsigned short d[16], m[16];
	FILE *f;
	int i;

	snprintf(c, sizeof(c), "%s/curseurs/%s.cur", ptk.assets_dir, nom);
	if ((f = fopen(c, "rb")) == NULL)
		return fleche;
	if (fread(t, 1, 68, f) != 68) {
		fclose(f);
		return fleche;
	}
	fclose(f);
	for (i = 0; i < 16; i++) {
		d[i] = (unsigned short)(t[2 * i] << 8 | t[2 * i + 1]);
		m[i] = (unsigned short)(t[32 + 2 * i] << 8 | t[33 + 2 * i]);
	}
	return curseur_bits(d, m, t[66] << 8 | t[67], t[64] << 8 | t[65]);
}

static void
montrer(void)
{
	XCopyArea(dpy, ecran, win, ptk.gc, 0, 0, (unsigned)sw, (unsigned)sh, 0, 0);
	XFlush(dpy);
}

static void
fond_gris(void)
{
	ptk_fill(&ptk, ecran, 0, 0, sw, sh, ptk_color(&ptk, "#888888"));
}

static void
fond_motif(void)
{
	int x, y;

	if (fond == None) {
		ptk_fill(&ptk, ecran, 0, 0, sw, sh, ptk_color(&ptk, "#63639c"));
		return;
	}
	for (y = 0; y < sh; y += fond_h)
		for (x = 0; x < sw; x += fond_w)
			XCopyArea(dpy, fond, ecran, ptk.gc, 0, 0, (unsigned)fond_w, (unsigned)fond_h, x, y);
}

/* fenêtre 422×323 relevée au pixel : noir, blanc en haut à gauche, #aa en bas à droite ; panneau du logo */
static void
fenetre(void)
{
	PtkColor gaa = ptk_color(&ptk, "#aaaaaa");

	ptk_fill(&ptk, ecran, fx, fy, FW, FH, ptk.black);
	ptk_fill(&ptk, ecran, fx + 1, fy + 1, FW - 2, FH - 2, ptk.white);
	ptk_fill(&ptk, ecran, fx + 2, fy + 2, FW - 3, FH - 3, gaa);
	ptk_fill(&ptk, ecran, fx + 2, fy + 2, FW - 4, FH - 4, ptk.face);
	ptk_line(&ptk, ecran, fx + 35, fy + 26, fx + 385, fy + 26, gaa);
	ptk_line(&ptk, ecran, fx + 35, fy + 26, fx + 35, fy + 249, gaa);
	ptk_rect(&ptk, ecran, fx + 36, fy + 27, 349, 222, ptk.black);
	ptk_line(&ptk, ecran, fx + 36, fy + 249, fx + 385, fy + 249, ptk.white);
	ptk_line(&ptk, ecran, fx + 385, fy + 27, fx + 385, fy + 249, ptk.white);
	ptk_fill(&ptk, ecran, fx + 37, fy + 28, 347, 220, ptk.white);
	if (logo != None)
		XCopyArea(dpy, logo, ecran, ptk.gc, 0, 0, (unsigned)logo_w, (unsigned)logo_h,
		    fx + 37 + (347 - logo_w) / 2, fy + 28 + (220 - logo_h) / 2);
}

static void
etat_chargement(int fait)
{
	const char *t = "Démarrage…";
	int v = fait > total ? total : fait;

	ptk_fill(&ptk, ecran, fx + 60, fy + 262, FW - 120, 18, ptk.face);
	ptk_text(&ptk, ecran, ptk.font_menu, fx + (FW - ptk_text_width(&ptk, ptk.font_menu, t)) / 2,
	    fy + 279, t, ptk.text);
	/* cadre gravé autour de la barre : #aa en haut et à gauche, blanc en bas et à droite */
	ptk_line(&ptk, ecran, fx + 130, fy + 290, fx + 291, fy + 290, ptk_color(&ptk, "#aaaaaa"));
	ptk_line(&ptk, ecran, fx + 130, fy + 290, fx + 130, fy + 303, ptk_color(&ptk, "#aaaaaa"));
	ptk_line(&ptk, ecran, fx + 131, fy + 303, fx + 291, fy + 303, ptk.white);
	ptk_line(&ptk, ecran, fx + 291, fy + 291, fx + 291, fy + 303, ptk.white);
	ptk_progress(&ptk, ecran, fx + 131, fy + 291, 160, 12, v, total);
}

/* les icônes s'alignent en bas de l'écran, 40 px d'écart, et remontent d'une rangée à droite */
static void
poser_icone(int i)
{
	int par_rangee = (sw - 8) / 40;
	int x = 8 + (i % par_rangee) * 40;
	int y = sh - 40 - (i / par_rangee) * 40;

	if (ext[i] != NULL)
		ptk_draw_argb(&ptk, ecran, ext[i], x, y, 0);
}

static long
ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void
attendre(int duree)
{
	usleep((useconds_t)duree * 1000);
}

/* la dernière image de platine-ecran-netbsd en fond de la racine, tout de suite */
static void
fond_rapide(void)
{
	struct stat st;
	FILE *f;
	int w, h, m, scr = DefaultScreen(dpy), x, y;
	Visual *v = DefaultVisual(dpy, scr);
	XImage *im;
	Pixmap pm;
	unsigned char *ligne;

	if (stat(IMAGE_RC, &st) != 0 || time(NULL) - st.st_mtime > 180 || v->class != TrueColor)
		return;
	if ((f = fopen(IMAGE_RC, "rb")) == NULL)
		return;
	if (fscanf(f, "P6 %d %d %d", &w, &h, &m) != 3 || m != 255 || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
		fclose(f);
		return;
	}
	fgetc(f);
	im = XCreateImage(dpy, v, (unsigned)DefaultDepth(dpy, scr), ZPixmap, 0, NULL, (unsigned)w, (unsigned)h, 32, 0);
	ligne = malloc((size_t)w * 3);
	if (im == NULL || ligne == NULL || (im->data = malloc((size_t)im->bytes_per_line * (size_t)h)) == NULL) {
		free(ligne);
		fclose(f);
		return;
	}
	for (y = 0; y < h && fread(ligne, 3, (size_t)w, f) == (size_t)w; y++)
		for (x = 0; x < w; x++) {
			unsigned long r = ligne[x * 3], g = ligne[x * 3 + 1], b = ligne[x * 3 + 2], p = 0;

			p |= (r * (v->red_mask / 255)) & v->red_mask;
			p |= (g * (v->green_mask / 255)) & v->green_mask;
			p |= (b * (v->blue_mask / 255)) & v->blue_mask;
			XPutPixel(im, x, y, p);
		}
	fclose(f);
	free(ligne);
	pm = XCreatePixmap(dpy, DefaultRootWindow(dpy), (unsigned)w, (unsigned)h, (unsigned)DefaultDepth(dpy, scr));
	XPutImage(dpy, pm, DefaultGC(dpy, scr), im, 0, 0, 0, 0, (unsigned)w, (unsigned)h);
	XSetWindowBackgroundPixmap(dpy, DefaultRootWindow(dpy), pm);
	XClearWindow(dpy, DefaultRootWindow(dpy));
	XFreePixmap(dpy, pm);
	XDestroyImage(im);
	XFlush(dpy);
}

int
main(int argc, char **argv)
{
	static const unsigned short ad[16] = { 0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
	    0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000 };
	static const unsigned short am[16] = { 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80,
	    0xFFC0, 0xFFE0, 0xFE00, 0xEF00, 0xCF00, 0x8780, 0x0780, 0x0380 };
	static const unsigned short zero[16];
	XSetWindowAttributes a;
	char tampon[1024];
	size_t rempli = 0;
	int fd = -1, i, fin = 0, poses = 0, recus = 0, b = 0;
	long prochaine_icone = 0, prochaine_balle = 0;

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		return 1;
	fond_rapide();	/* avant les polices et les icônes : pas d'écran noir après l'écran rc */
	if (ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;
	sw = DisplayWidth(dpy, ptk.screen);
	sh = DisplayHeight(dpy, ptk.screen);
	fx = (sw - FW) / 2;
	fy = sh <= 500 ? (sh - FH) / 2 - 20 : (sh - FH) / 2 - 55;
	if (argc > 1)
		fd = open(argv[1], O_RDONLY | O_NONBLOCK);
	fleche = curseur_bits(ad, am, 1, 1);
	vide = curseur_bits(zero, zero, 0, 0);
	for (i = 0; i < 8; i++) {
		snprintf(tampon, sizeof(tampon), "balle-%d", i);
		balle[i] = curseur_fichier(tampon);
	}
	for (i = 0; i < 7; i++) {
		snprintf(tampon, sizeof(tampon), "montre-%d", i);
		montre[i] = curseur_fichier(tampon);
	}
	logo = ptk_load_ppm(&ptk, "demarrage.ppm", &logo_w, &logo_h);
	fond = ptk_load_ppm(&ptk, "fond.ppm", &fond_w, &fond_h);
	happy = ptk_load_ppm(&ptk, "happymac.ppm", &happy_w, &happy_h);
	ecran = XCreatePixmap(dpy, ptk.root, (unsigned)sw, (unsigned)sh, (unsigned)DefaultDepth(dpy, ptk.screen));
	a.override_redirect = True;
	a.background_pixmap = None;
	a.cursor = vide;
	win = XCreateWindow(dpy, ptk.root, 0, 0, (unsigned)sw, (unsigned)sh, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixmap | CWCursor, &a);
	XDefineCursor(dpy, ptk.root, vide);
	XWarpPointer(dpy, None, ptk.root, 0, 0, 0, 0, 16, 18);

	/* Reprise : sous NetBSD, platine-ecran-netbsd a déjà montré le Happy Mac, la fenêtre et
	 * une icône par script rc ; on repart de là au lieu de tout rejouer. */
	{
		struct stat st;
		FILE *e;
		char l[128];

		if (stat(ETAT_RC, &st) == 0 && time(NULL) - st.st_mtime < 180 &&
		    (e = fopen(ETAT_RC, "r")) != NULL) {
			while (fgets(l, sizeof(l), e) != NULL) {
				l[strcspn(l, "\n")] = '\0';
				if (strncmp(l, "total ", 6) == 0)
					total_rc = atoi(l + 6);
				else if (l[0] != '\0' && n_ext < MAX_EXT)
					ext[n_ext++] = ptk_load_argb(&ptk, l);
			}
			fclose(e);
			total_rc = n_ext;	/* le total de l'écran rc inclut déjà la session */
			reprise = total_rc > 0;
		}
	}
	if (reprise) {
		total += total_rc;
		XMapRaised(dpy, win);
		goto etapes;
	}

	/* 1. gris et Happy Mac, sans curseur */
	fond_gris();
	if (happy != None)
		XCopyArea(dpy, happy, ecran, ptk.gc, 0, 0, (unsigned)happy_w, (unsigned)happy_h,
		    (sw - happy_w) / 2, (sh - happy_h) / 2);
	XMapRaised(dpy, win);
	montrer();
	attendre(700);
	/* 2. gris seul, la flèche apparaît */
	fond_gris();
	montrer();
	XDefineCursor(dpy, win, fleche);
	XFlush(dpy);
	attendre(350);
	/* 3. la fenêtre au logo sur le gris ; plus de curseur */
	XDefineCursor(dpy, win, vide);
	fenetre();
	montrer();
	attendre(800);
	/* 4. le motif du bureau remplace le gris : la flèche revient */
	fond_motif();
	fenetre();
	etat_chargement(0);
	montrer();
	XDefineCursor(dpy, win, fleche);
	XFlush(dpy);
	attendre(400);

etapes:
	if (reprise) {	/* même image que la dernière de platine-ecran-netbsd */
		fond_motif();
		fenetre();
		for (poses = 0; poses < n_ext; poses++)
			poser_icone(poses);
		etat_chargement(poses);
		montrer();
		XDefineCursor(dpy, win, fleche);
		XFlush(dpy);
	}
	/* 5. étapes réelles : la balle tourne, une icône par étape */
	for (;;) {
		long t = ms();
		struct pollfd pf;

		if (fd >= 0 && !fin) {
			ssize_t n = read(fd, tampon + rempli, sizeof(tampon) - rempli - 1);

			if (n > 0)
				rempli += (size_t)n;
			for (;;) {
				char *nl = memchr(tampon, '\n', rempli);
				char nom[64], *sp;

				if (nl == NULL)
					break;
				*nl = '\0';
				if (strncmp(tampon, "total ", 6) == 0) {
					total = atoi(tampon + 6) + total_rc;
					if (total < 1)
						total = 1;
				} else if (strcmp(tampon, "fin") == 0) {
					fin = 1;
				} else if (n_ext < MAX_EXT) {
					snprintf(nom, sizeof(nom), "%s", tampon);
					if ((sp = strchr(nom, ' ')) != NULL)
						*sp = '\0';
					snprintf(tampon, sizeof(tampon), "ext-%.60s", nom);
					ext[n_ext++] = ptk_load_argb(&ptk, tampon);
					recus++;
				}
				rempli -= (size_t)(nl + 1 - tampon);
				memmove(tampon, nl + 1, rempli);
			}
		} else if (fd < 0) {
			fin = 1;
		}
		if (t >= prochaine_balle) {
			XDefineCursor(dpy, win, balle[b]);
			b = (b + 1) % 8;
			prochaine_balle = t + 110;
		}
		if (poses < n_ext && t >= prochaine_icone) {
			poser_icone(poses);
			poses++;
			etat_chargement(poses);
			montrer();
			prochaine_icone = t + 140;
		}
		XFlush(dpy);
		if (fin && poses >= n_ext)
			break;
		pf.fd = fd >= 0 ? fd : ConnectionNumber(dpy);
		pf.events = POLLIN;
		poll(&pf, 1, 30);
	}
	etat_chargement(total);
	montrer();
	attendre(250);

	/* 6. la fenêtre disparaît, le bureau reste nu : montre le temps que le bureau se charge */
	fond_motif();
	montrer();
	XDefineCursor(dpy, win, montre[0]);
	XFlush(dpy);
	attendre(250);
	/* le motif passe en fond de l'écran : aucun clignotement quand le WM prend la suite */
	if (fond != None) {
		Pixmap r = XCreatePixmap(dpy, ptk.root, (unsigned)sw, (unsigned)sh, (unsigned)DefaultDepth(dpy, ptk.screen));

		XCopyArea(dpy, ecran, r, ptk.gc, 0, 0, (unsigned)sw, (unsigned)sh, 0, 0);
		XSetWindowBackgroundPixmap(dpy, ptk.root, r);
		XClearWindow(dpy, ptk.root);
	}
	XDefineCursor(dpy, ptk.root, montre[0]);
	XDestroyWindow(dpy, win);	/* seul le motif du fond d'écran doit survivre à ce programme */
	XSync(dpy, False);
	XSetCloseDownMode(dpy, RetainPermanent);
	return 0;
}
