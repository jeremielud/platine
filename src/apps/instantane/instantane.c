/*
 * platine-instantane — Instantané, l'outil de capture d'écran de Platine,
 * inspiré de Snapz Pro 2 sous Mac OS 9 (audit/macos9-ref/snapz-panneau.png).
 *
 * Une extension : lancée par la session, elle attend ⌘⇧3. À la frappe, l'écran
 * est figé tel qu'il est (l'image est prise avant toute fenêtre), puis le
 * panneau s'ouvre :
 *
 *   Capture :  Écran | Fenêtre | Menu | Sélection      (gros boutons biseautés)
 *   Envoyer vers, Couleurs, Échelle, Format             (menus locaux)
 *   Curseur visible, Choisir le nom du fichier          (cases)
 *
 * Fenêtre : on désigne une fenêtre sur l'image figée (elle s'entoure sous le
 * pointeur) ; Sélection : on trace un rectangle. Le résultat va dans le dossier
 * « Captures » du dossier personnel (« Capture 1.png »…), dans le presse-papiers
 * (image/png), ou là où la zone de dialogue d'enregistrement le dit. Les
 * réglages restent dans Préférences (~/.config/platine/instantane). Menu est
 * grisé : un menu ouvert tient le clavier, ⌘⇧3 n'arrive pas (comme le panneau
 * relevé sur la VM sans menu ouvert).
 */
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/extensions/Xfixes.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

PTK_IDENTITE("Instantané", "snap", "app-instantane", PTK_VERSION, "");

enum { MODE_ECRAN, MODE_FENETRE, MODE_MENU, MODE_SELECTION };

static struct {
	PtkApp app;
	Display *dpy;
	Window racine;
	int sw, sh;
	/* l'écran figé à la frappe de ⌘⇧3 */
	XImage *image;
	Pixmap fige;
	int px, py;		/* pointeur */
	XFixesCursorImage *curseur;
	/* panneau */
	PtkWindow *panneau;
	PtkControl *envoyer, *couleurs, *echelle, *format, *voir_curseur, *choisir;
	/* superposition (Fenêtre, Sélection) */
	Window sur;
	GC gc_sur;
	int mode, fini, annule;
	int x0, y0, x1, y1, enfonce;
	int rx, ry, rw, rh;	/* rectangle désigné, -1 : aucun */
	/* presse-papiers */
	Window sel;
	Atom a_clip, a_targets, a_png, a_moi;
	unsigned char *png;
	size_t png_lg;
	/* réglages */
	char r_envoyer[64], r_couleurs[64], r_echelle[16], r_format[16];
	char r_dossier[PATH_MAX];	/* dernier dossier choisi par Enregistrer sous… */
	int r_curseur, r_choisir;
	int xfixes;
} I;

/* --- réglages (Préférences) --- */

static void
reglages_chemin(char *out, size_t n)
{
	const char *h = getenv("HOME");

	snprintf(out, n, "%s/.config/platine/instantane", h != NULL ? h : "/tmp");
}

static void
reglages_lire(void)
{
	char ch[PATH_MAX], l[256];
	FILE *f;

	snprintf(I.r_envoyer, sizeof(I.r_envoyer), "Dossier Captures");
	snprintf(I.r_couleurs, sizeof(I.r_couleurs), "Inchangées");
	snprintf(I.r_echelle, sizeof(I.r_echelle), "100 %%");
	snprintf(I.r_format, sizeof(I.r_format), "PNG");
	I.r_curseur = 1;
	I.r_choisir = 0;
	reglages_chemin(ch, sizeof(ch));
	if ((f = fopen(ch, "r")) == NULL)
		return;
	while (fgets(l, sizeof(l), f) != NULL) {
		char *v = strchr(l, '=');

		if (v == NULL)
			continue;
		*v++ = '\0';
		v[strcspn(v, "\n")] = '\0';
		if (strcmp(l, "envoyer") == 0)
			snprintf(I.r_envoyer, sizeof(I.r_envoyer), "%s", v);
		else if (strcmp(l, "couleurs") == 0)
			snprintf(I.r_couleurs, sizeof(I.r_couleurs), "%s", v);
		else if (strcmp(l, "echelle") == 0)
			snprintf(I.r_echelle, sizeof(I.r_echelle), "%s", v);
		else if (strcmp(l, "format") == 0)
			snprintf(I.r_format, sizeof(I.r_format), "%s", v);
		else if (strcmp(l, "curseur") == 0)
			I.r_curseur = atoi(v);
		else if (strcmp(l, "choisir") == 0)
			I.r_choisir = atoi(v);
		else if (strcmp(l, "dossier") == 0)
			snprintf(I.r_dossier, sizeof(I.r_dossier), "%s", v);
	}
	fclose(f);
}

static void
reglages_ecrire(void)
{
	char ch[PATH_MAX], d[PATH_MAX];
	FILE *f;

	reglages_chemin(ch, sizeof(ch));
	snprintf(d, sizeof(d), "%s", ch);
	*strrchr(d, '/') = '\0';
	/* ~/.config puis ~/.config/platine, s'ils manquent */
	*strrchr(d, '/') = '\0';
	mkdir(d, 0755);
	d[strlen(d)] = '/';
	mkdir(d, 0755);
	if ((f = fopen(ch, "w")) == NULL)
		return;
	fprintf(f, "envoyer=%s\ncouleurs=%s\nechelle=%s\nformat=%s\ncurseur=%d\nchoisir=%d\n",
	    I.r_envoyer, I.r_couleurs, I.r_echelle, I.r_format, I.r_curseur, I.r_choisir);
	if (I.r_dossier[0] != '\0')
		fprintf(f, "dossier=%s\n", I.r_dossier);
	fclose(f);
}

/* --- image : extraction, curseur, couleurs, échelle --- */

typedef struct {
	int w, h;
	unsigned char *rgb;
} Image;

static int
canal(unsigned long pix, unsigned long masque)
{
	int dec = 0, bits = 0;

	if (masque == 0)
		return 0;
	while (!(masque & 1)) {
		masque >>= 1;
		dec++;
	}
	while (masque & 1) {
		masque >>= 1;
		bits++;
	}
	pix = (pix >> dec) & ((1UL << bits) - 1);
	return bits >= 8 ? (int)(pix >> (bits - 8)) : (int)(pix * 255 / ((1UL << bits) - 1));
}

static Image
extraire(int x, int y, int w, int h)
{
	Image im = { w, h, malloc((size_t)w * (size_t)h * 3) };
	int i, j;

	if (im.rgb == NULL)
		return im;
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++) {
			unsigned long p = XGetPixel(I.image, x + i, y + j);
			unsigned char *o = im.rgb + ((size_t)j * (size_t)w + (size_t)i) * 3;

			o[0] = (unsigned char)canal(p, I.image->red_mask);
			o[1] = (unsigned char)canal(p, I.image->green_mask);
			o[2] = (unsigned char)canal(p, I.image->blue_mask);
		}
	/* le curseur, composé à sa place si on le veut visible */
	if (I.r_curseur && I.curseur != NULL) {
		XFixesCursorImage *c = I.curseur;
		int cx = I.px - c->xhot - x, cy = I.py - c->yhot - y;

		for (j = 0; j < c->height; j++)
			for (i = 0; i < c->width; i++) {
				unsigned long a = c->pixels[j * c->width + i];
				int X = cx + i, Y = cy + j, al = (int)((a >> 24) & 0xff), k;
				unsigned char *o;

				if (X < 0 || Y < 0 || X >= w || Y >= h || al == 0)
					continue;
				o = im.rgb + ((size_t)Y * (size_t)w + (size_t)X) * 3;
				/* pixels prémultipliés par l'alpha */
				for (k = 0; k < 3; k++) {
					int v = (int)((a >> (16 - 8 * k)) & 0xff);

					o[k] = (unsigned char)(v + o[k] * (255 - al) / 255);
				}
			}
	}
	return im;
}

static void
couleurs(Image *im)
{
	size_t i, n = (size_t)im->w * (size_t)im->h;
	int gris = strcmp(I.r_couleurs, "Niveaux de gris") == 0;
	int nb = strcmp(I.r_couleurs, "Noir et blanc") == 0;

	if (!gris && !nb)
		return;
	for (i = 0; i < n; i++) {
		unsigned char *p = im->rgb + i * 3;
		int l = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;

		if (nb) {
			/* tramage ordonné 4×4 : le noir et blanc d'un écran 1 bit */
			static const int bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
			int x = (int)(i % (size_t)im->w), y = (int)(i / (size_t)im->w);

			l = l * 16 / 256 > bayer[(y & 3) * 4 + (x & 3)] ? 255 : 0;
		}
		p[0] = p[1] = p[2] = (unsigned char)l;
	}
}

static void
echelle(Image *im)
{
	int pc = atoi(I.r_echelle), w, h, i, j;
	unsigned char *o;

	if (pc <= 0 || pc >= 100)
		return;
	w = im->w * pc / 100;
	h = im->h * pc / 100;
	if (w < 1 || h < 1 || (o = malloc((size_t)w * (size_t)h * 3)) == NULL)
		return;
	/* moyenne de la boîte source de chaque pixel */
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++) {
			int x0 = i * im->w / w, x1 = (i + 1) * im->w / w;
			int y0 = j * im->h / h, y1 = (j + 1) * im->h / h, x, y, k;
			long s[3] = { 0, 0, 0 }, n = 0;

			for (y = y0; y < (y1 > y0 ? y1 : y0 + 1); y++)
				for (x = x0; x < (x1 > x0 ? x1 : x0 + 1); x++, n++)
					for (k = 0; k < 3; k++)
						s[k] += im->rgb[((size_t)y * (size_t)im->w + (size_t)x) * 3 + (size_t)k];
			for (k = 0; k < 3; k++)
				o[((size_t)j * (size_t)w + (size_t)i) * 3 + (size_t)k] = (unsigned char)(s[k] / n);
		}
	free(im->rgb);
	im->rgb = o;
	im->w = w;
	im->h = h;
}

/* --- encodage : PNG (zlib) ou PPM --- */

static void
png_bloc(unsigned char **b, size_t *n, const char *type, const unsigned char *d, size_t l)
{
	unsigned char t[8];
	unsigned long c;

	*b = realloc(*b, *n + l + 12);
	t[0] = (unsigned char)(l >> 24); t[1] = (unsigned char)(l >> 16);
	t[2] = (unsigned char)(l >> 8); t[3] = (unsigned char)l;
	memcpy(t + 4, type, 4);
	memcpy(*b + *n, t, 8);
	if (l > 0)
		memcpy(*b + *n + 8, d, l);
	c = crc32(0L, *b + *n + 4, (uInt)(l + 4));
	(*b)[*n + 8 + l] = (unsigned char)(c >> 24);
	(*b)[*n + 9 + l] = (unsigned char)(c >> 16);
	(*b)[*n + 10 + l] = (unsigned char)(c >> 8);
	(*b)[*n + 11 + l] = (unsigned char)c;
	*n += l + 12;
}

static unsigned char *
png(const Image *im, size_t *lg)
{
	size_t ligne = (size_t)im->w * 3 + 1, brut = ligne * (size_t)im->h;
	unsigned char *r = malloc(brut), *z, *b = NULL, ih[13];
	uLongf zl = compressBound((uLong)brut);
	size_t n = 0;
	int j;

	z = malloc(zl);
	if (r == NULL || z == NULL) {
		free(r);
		free(z);
		return NULL;
	}
	for (j = 0; j < im->h; j++) {
		r[(size_t)j * ligne] = 0;	/* filtre : aucun */
		memcpy(r + (size_t)j * ligne + 1, im->rgb + (size_t)j * (size_t)im->w * 3, ligne - 1);
	}
	if (compress2(z, &zl, r, (uLong)brut, 6) != Z_OK) {
		free(r);
		free(z);
		return NULL;
	}
	b = malloc(8);
	memcpy(b, "\211PNG\r\n\032\n", 8);
	n = 8;
	ih[0] = (unsigned char)(im->w >> 24); ih[1] = (unsigned char)(im->w >> 16);
	ih[2] = (unsigned char)(im->w >> 8); ih[3] = (unsigned char)im->w;
	ih[4] = (unsigned char)(im->h >> 24); ih[5] = (unsigned char)(im->h >> 16);
	ih[6] = (unsigned char)(im->h >> 8); ih[7] = (unsigned char)im->h;
	ih[8] = 8; ih[9] = 2; ih[10] = 0; ih[11] = 0; ih[12] = 0;
	png_bloc(&b, &n, "IHDR", ih, 13);
	png_bloc(&b, &n, "IDAT", z, zl);
	png_bloc(&b, &n, "IEND", NULL, 0);
	free(r);
	free(z);
	*lg = n;
	return b;
}

static int
ecrire(const Image *im, const char *chemin)
{
	FILE *f = fopen(chemin, "wb");
	int ok;

	if (f == NULL)
		return 0;
	if (strcmp(I.r_format, "PPM") == 0) {
		fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
		ok = fwrite(im->rgb, 3, (size_t)im->w * (size_t)im->h, f) ==
		    (size_t)im->w * (size_t)im->h;
	} else {
		size_t lg;
		unsigned char *b = png(im, &lg);

		ok = b != NULL && fwrite(b, 1, lg, f) == lg;
		free(b);
	}
	return fclose(f) == 0 && ok;
}

/* --- presse-papiers : image/png servie tant qu'Instantané tourne --- */

static void
presse_papiers(const Image *im)
{
	free(I.png);
	I.png = png(im, &I.png_lg);
	if (I.png != NULL)
		XSetSelectionOwner(I.dpy, I.a_clip, I.sel, CurrentTime);
}

static void
servir(XSelectionRequestEvent *r)
{
	XEvent rep;
	Atom prop = r->property != None ? r->property : r->target;

	memset(&rep, 0, sizeof(rep));
	rep.xselection.type = SelectionNotify;
	rep.xselection.requestor = r->requestor;
	rep.xselection.selection = r->selection;
	rep.xselection.target = r->target;
	rep.xselection.time = r->time;
	rep.xselection.property = None;
	if (r->target == I.a_targets) {
		Atom t[2] = { I.a_targets, I.a_png };

		XChangeProperty(I.dpy, r->requestor, prop, XA_ATOM, 32, PropModeReplace,
		    (unsigned char *)t, 2);
		rep.xselection.property = prop;
	} else if (r->target == I.a_png && I.png != NULL &&
	    I.png_lg < (size_t)XExtendedMaxRequestSize(I.dpy) * 4 - 1024) {
		XChangeProperty(I.dpy, r->requestor, prop, I.a_png, 8, PropModeReplace,
		    I.png, (int)I.png_lg);
		rep.xselection.property = prop;
	}
	XSendEvent(I.dpy, r->requestor, False, 0, &rep);
	XFlush(I.dpy);
}

/* --- la capture --- */

static void
nom_libre(const char *dossier, char *out, size_t n)
{
	const char *ext = strcmp(I.r_format, "PPM") == 0 ? "ppm" : "png";
	int k;

	for (k = 1; k < 10000; k++) {
		if ((size_t)snprintf(out, n, "%s/Capture %d.%s", dossier, k, ext) >= n)
			return;
		if (access(out, F_OK) != 0)
			return;
	}
}

static void
capturer(int x, int y, int w, int h)
{
	Image im;
	char dossier[PATH_MAX], ch[PATH_MAX];

	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > I.sw) w = I.sw - x;
	if (y + h > I.sh) h = I.sh - y;
	if (w < 1 || h < 1)
		return;
	im = extraire(x, y, w, h);
	if (im.rgb == NULL)
		return;
	couleurs(&im);
	echelle(&im);
	if (strcmp(I.r_envoyer, "Presse-papiers") == 0) {
		presse_papiers(&im);
		printf("instantane : presse-papiers %dx%d\n", im.w, im.h);
		free(im.rgb);
		return;
	}
	ptk_perso("Captures", dossier, sizeof(dossier));	/* dans le dossier personnel */
	mkdir(dossier, 0755);
	/* Enregistrer sous… : dossier et nom dans la zone de dialogue, ouverte
	 * sur le dernier dossier choisi */
	if (strcmp(I.r_envoyer, "Enregistrer sous…") == 0) {
		struct stat st;
		const char *b;

		if (I.r_dossier[0] != '\0' && stat(I.r_dossier, &st) == 0 && S_ISDIR(st.st_mode))
			snprintf(dossier, sizeof(dossier), "%s", I.r_dossier);
		nom_libre(dossier, ch, sizeof(ch));
		b = strrchr(ch, '/');
		if (!ptk_enregistrer(&I.app, "Enregistrer la capture sous :", b != NULL ? b + 1 : ch,
		    dossier, ch, sizeof(ch))) {
			free(im.rgb);
			return;
		}
		snprintf(I.r_dossier, sizeof(I.r_dossier), "%s", ch);
		*strrchr(I.r_dossier, '/') = '\0';
		reglages_ecrire();
	} else
		nom_libre(dossier, ch, sizeof(ch));
	if (I.r_choisir && strcmp(I.r_envoyer, "Enregistrer sous…") != 0) {
		const char *b = strrchr(ch, '/');

		if (!ptk_enregistrer(&I.app, "Enregistrer la capture sous :", b != NULL ? b + 1 : ch,
		    dossier, ch, sizeof(ch))) {
			free(im.rgb);
			return;
		}
	}
	if (!ecrire(&im, ch)) {
		PtkWindow *w0 = ptk_window_new(&I.app, "Instantané", 200, 150, 300, 100);

		ptk_alert(w0, "La capture n'a pas pu être enregistrée.", strerror(errno), "OK");
		ptk_window_close(w0);
	} else
		printf("instantane : %s %dx%d\n", ch, im.w, im.h);
	free(im.rgb);
}

/* --- superposition : désigner une fenêtre ou tracer un rectangle --- */

/* la fenêtre de premier niveau sous le point (cadre du WM compris) : la plus
 * haute qui porte un nom, hors bureau, barre de menus et plein écran */
static int
fenetre_sous(int x, int y, int *rx, int *ry, int *rw, int *rh)
{
	Window r, p, *enf = NULL;
	unsigned n = 0;
	int i;

	if (!XQueryTree(I.dpy, I.racine, &r, &p, &enf, &n))
		return 0;
	for (i = (int)n - 1; i >= 0; i--) {
		XWindowAttributes a;
		Window *e2 = NULL, r2, p2;
		unsigned n2 = 0, k;
		int nommee = 0;
		char *nom = NULL;

		if (enf[i] == I.sur || !XGetWindowAttributes(I.dpy, enf[i], &a) ||
		    a.map_state != IsViewable || x < a.x || y < a.y ||
		    x >= a.x + a.width || y >= a.y + a.height)
			continue;
		if (a.width >= I.sw && a.height >= I.sh - 30)
			continue;	/* bureau */
		if (a.y == 0 && a.height <= 24 && a.width >= I.sw)
			continue;	/* barre de menus */
		if (XFetchName(I.dpy, enf[i], &nom) && nom != NULL) {
			nommee = strcmp(nom, "Bureau") != 0;
			XFree(nom);
		}
		/* un cadre du WM : son client porte le nom */
		if (!nommee && XQueryTree(I.dpy, enf[i], &r2, &p2, &e2, &n2)) {
			for (k = 0; k < n2 && !nommee; k++)
				if (XFetchName(I.dpy, e2[k], &nom) && nom != NULL) {
					nommee = 1;
					XFree(nom);
				}
			if (e2 != NULL)
				XFree(e2);
		}
		if (!nommee)
			continue;
		*rx = a.x;
		*ry = a.y;
		*rw = a.width;
		*rh = a.height;
		XFree(enf);
		return 1;
	}
	if (enf != NULL)
		XFree(enf);
	return 0;
}

static void
sur_effacer(void)
{
	if (I.rw > 0 && I.rh > 0) {
		/* le fond de la superposition est l'écran figé : il se rétablit */
		XClearArea(I.dpy, I.sur, I.rx - 3, I.ry - 3, (unsigned)I.rw + 6, 6, False);
		XClearArea(I.dpy, I.sur, I.rx - 3, I.ry + I.rh - 3, (unsigned)I.rw + 6, 6, False);
		XClearArea(I.dpy, I.sur, I.rx - 3, I.ry - 3, 6, (unsigned)I.rh + 6, False);
		XClearArea(I.dpy, I.sur, I.rx + I.rw - 3, I.ry - 3, 6, (unsigned)I.rh + 6, False);
		XClearArea(I.dpy, I.sur, I.rx + I.rw + 4, I.ry + I.rh + 4, 90, 18, False);
	}
}

static void
sur_tracer(void)
{
	char t[32];
	Ptk *p = &I.app.ptk;

	if (I.rw <= 0 || I.rh <= 0)
		return;
	if (I.mode == MODE_FENETRE) {
		/* fenêtre désignée : entourée d'un trait couleur sélection de 3 px,
		 * posé sur son bord */
		XSetForeground(I.dpy, I.gc_sur, p->selection.pixel);
		XSetLineAttributes(I.dpy, I.gc_sur, 3, LineSolid, CapButt, JoinMiter);
		XDrawRectangle(I.dpy, I.sur, I.gc_sur, I.rx + 1, I.ry + 1, (unsigned)I.rw - 3,
		    (unsigned)I.rh - 3);
	} else {
		/* rectangle de sélection : pointillé noir et blanc, et ses dimensions */
		XSetLineAttributes(I.dpy, I.gc_sur, 1, LineDoubleDash, CapButt, JoinMiter);
		XSetForeground(I.dpy, I.gc_sur, p->black.pixel);
		XSetBackground(I.dpy, I.gc_sur, p->white.pixel);
		XDrawRectangle(I.dpy, I.sur, I.gc_sur, I.rx, I.ry, (unsigned)I.rw - 1,
		    (unsigned)I.rh - 1);
		snprintf(t, sizeof(t), "%d x %d", I.rw, I.rh);
		ptk_fill(p, I.sur, I.rx + I.rw + 4, I.ry + I.rh + 4,
		    ptk_text_width(p, p->font_small, t) + 8, 16, p->white);
		ptk_rect(p, I.sur, I.rx + I.rw + 4, I.ry + I.rh + 4,
		    ptk_text_width(p, p->font_small, t) + 8, 16, p->black);
		ptk_text(p, I.sur, p->font_small, I.rx + I.rw + 8, I.ry + I.rh + 16, t, p->text);
	}
	XFlush(I.dpy);
}

static void
sur_rect(int x, int y, int w, int h)
{
	sur_effacer();
	I.rx = x;
	I.ry = y;
	I.rw = w;
	I.rh = h;
	sur_tracer();
}

static int
hors(PtkApp *app, XEvent *e, void *user)
{
	(void)app;
	(void)user;
	if (e->type == SelectionRequest && e->xselectionrequest.owner == I.sel) {
		servir(&e->xselectionrequest);
		return 1;
	}
	if (e->type == SelectionClear && e->xselectionclear.window == I.sel) {
		/* une nouvelle instance (réinstallée) prend la relève : on lui
		 * laisse ⌘⇧3 */
		if (e->xselectionclear.selection == I.a_moi) {
			printf("instantane : relève par une nouvelle instance\n");
			exit(0);
		}
		free(I.png);
		I.png = NULL;
		return 1;
	}
	if (I.sur == None || e->xany.window != I.sur)
		return e->type == KeyPress && e->xany.window == I.racine;
	switch (e->type) {
	case MotionNotify:
		if (I.mode == MODE_FENETRE) {
			int x, y, w, h;

			if (fenetre_sous(e->xmotion.x, e->xmotion.y, &x, &y, &w, &h)) {
				if (x != I.rx || y != I.ry || w != I.rw || h != I.rh)
					sur_rect(x, y, w, h);
			} else if (I.rw > 0) {
				sur_effacer();
				I.rw = I.rh = 0;
			}
		} else if (I.enfonce) {
			int x = e->xmotion.x < I.x0 ? e->xmotion.x : I.x0;
			int y = e->xmotion.y < I.y0 ? e->xmotion.y : I.y0;

			sur_rect(x, y, abs(e->xmotion.x - I.x0) + 1, abs(e->xmotion.y - I.y0) + 1);
		}
		break;
	case ButtonPress:
		if (e->xbutton.button != Button1)
			break;
		if (I.mode == MODE_SELECTION) {
			I.enfonce = 1;
			I.x0 = e->xbutton.x;
			I.y0 = e->xbutton.y;
			sur_rect(I.x0, I.y0, 1, 1);
		}
		break;
	case ButtonRelease:
		if (e->xbutton.button != Button1)
			break;
		if (I.mode == MODE_FENETRE && I.rw > 0)
			I.fini = 1;
		else if (I.mode == MODE_SELECTION && I.enfonce) {
			I.enfonce = 0;
			if (I.rw > 2 && I.rh > 2)
				I.fini = 1;
			else {
				sur_effacer();
				I.rw = I.rh = 0;
			}
		}
		break;
	case KeyPress:
		if (XLookupKeysym(&e->xkey, 0) == XK_Escape) {
			I.annule = 1;
			I.fini = 1;
		}
		break;
	}
	return 1;
}

/* superposition plein écran sur l'image figée ; 1 et le rectangle choisi */
static int
designer(int mode)
{
	XSetWindowAttributes a;
	Cursor cur = XCreateFontCursor(I.dpy, mode == MODE_SELECTION ? XC_crosshair : XC_hand2);
	int i;

	a.override_redirect = True;
	a.background_pixmap = I.fige;
	a.event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask;
	I.sur = XCreateWindow(I.dpy, I.racine, 0, 0, (unsigned)I.sw, (unsigned)I.sh, 0,
	    CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixmap | CWEventMask, &a);
	I.gc_sur = XCreateGC(I.dpy, I.sur, 0, NULL);
	XMapRaised(I.dpy, I.sur);
	XSync(I.dpy, False);
	for (i = 0; i < 50 && XGrabPointer(I.dpy, I.sur, False, ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask, GrabModeAsync, GrabModeAsync, I.sur, cur, CurrentTime) != GrabSuccess; i++)
		usleep(20000);
	for (i = 0; i < 50 && XGrabKeyboard(I.dpy, I.sur, False, GrabModeAsync, GrabModeAsync,
	    CurrentTime) != GrabSuccess; i++)
		usleep(20000);
	I.mode = mode;
	I.fini = I.annule = I.enfonce = 0;
	I.rx = I.ry = I.rw = I.rh = 0;
	ptk_app_run_modal(&I.app, &I.fini);
	XUngrabPointer(I.dpy, CurrentTime);
	XUngrabKeyboard(I.dpy, CurrentTime);
	XFreeGC(I.dpy, I.gc_sur);
	XDestroyWindow(I.dpy, I.sur);
	XFreeCursor(I.dpy, cur);
	I.sur = None;
	XSync(I.dpy, False);
	return !I.annule && I.rw > 0 && I.rh > 0;
}

/* --- le panneau --- */

static void
panneau_fermer(void)
{
	if (I.panneau == NULL)
		return;
	/* valeurs des menus locaux (éléments courts, connus) */
	if (snprintf(I.r_envoyer, sizeof(I.r_envoyer), "%s", I.envoyer->text) < 0 ||
	    snprintf(I.r_couleurs, sizeof(I.r_couleurs), "%s", I.couleurs->text) < 0 ||
	    snprintf(I.r_echelle, sizeof(I.r_echelle), "%s", I.echelle->text) < 0 ||
	    snprintf(I.r_format, sizeof(I.r_format), "%s", I.format->text) < 0)
		return;
	I.r_curseur = I.voir_curseur->checked;
	I.r_choisir = I.choisir->checked;
	reglages_ecrire();
	ptk_window_close(I.panneau);
	I.panneau = NULL;
	XSync(I.dpy, False);
}

static void
liberer_fige(void)
{
	if (I.image != NULL)
		XDestroyImage(I.image);
	I.image = NULL;
	if (I.fige != None)
		XFreePixmap(I.dpy, I.fige);
	I.fige = None;
	if (I.curseur != NULL)
		XFree(I.curseur);
	I.curseur = NULL;
}

static void
agir(int mode)
{
	panneau_fermer();
	if (mode == MODE_ECRAN)
		capturer(0, 0, I.sw, I.sh);
	else if (designer(mode))
		capturer(I.rx, I.ry, I.rw, I.rh);
	liberer_fige();
}

static void
b_ecran(PtkWindow *w, PtkControl *c, void *u) { (void)w; (void)c; (void)u; agir(MODE_ECRAN); }
static void
b_fenetre(PtkWindow *w, PtkControl *c, void *u) { (void)w; (void)c; (void)u; agir(MODE_FENETRE); }
static void
b_selection(PtkWindow *w, PtkControl *c, void *u) { (void)w; (void)c; (void)u; agir(MODE_SELECTION); }

/* icônes des boutons, dessinées au trait comme celles du panneau de Snapz */
static void
icone(PtkWindow *w, PtkControl *c, int x, int y, int W, int H, int presse)
{
	Ptk *p = w->p;
	PtkColor n = p->inactif ? ptk_color(p, "#888888") : p->black;
	PtkColor lav = p->inactif ? p->face : ptk_color(p, "#ccccff");
	int cx = x + W / 2, cy = y + H / 2, k;

	(void)presse;
	if (c->value == MODE_ECRAN) {
		ptk_rect(p, w->surface, cx - 17, cy - 13, 34, 26, n);
		ptk_rect(p, w->surface, cx - 16, cy - 12, 32, 24, p->white);
		ptk_fill(p, w->surface, cx - 14, cy - 10, 28, 20, lav);
		ptk_rect(p, w->surface, cx - 15, cy - 11, 30, 22, n);
	} else if (c->value == MODE_FENETRE) {
		ptk_fill(p, w->surface, cx - 16, cy - 12, 32, 24, p->white);
		ptk_rect(p, w->surface, cx - 16, cy - 12, 32, 24, n);
		for (k = 0; k < 3; k++)
			ptk_line(p, w->surface, cx - 14, cy - 10 + 2 * k, cx + 13, cy - 10 + 2 * k, n);
		ptk_fill(p, w->surface, cx - 10, cy - 1, 7, 6, lav);
		ptk_rect(p, w->surface, cx - 10, cy - 1, 7, 6, n);
		ptk_fill(p, w->surface, cx + 2, cy - 1, 7, 6, lav);
		ptk_rect(p, w->surface, cx + 2, cy - 1, 7, 6, n);
		ptk_line(p, w->surface, cx - 11, cy + 8, cx - 3, cy + 8, n);
		ptk_line(p, w->surface, cx + 1, cy + 8, cx + 9, cy + 8, n);
	} else if (c->value == MODE_MENU) {
		ptk_fill(p, w->surface, cx - 12, cy - 14, 24, 28, p->white);
		ptk_rect(p, w->surface, cx - 12, cy - 14, 24, 28, n);
		for (k = 0; k < 5; k++)
			ptk_line(p, w->surface, cx - 8, cy - 9 + 5 * k, cx + 6, cy - 9 + 5 * k, n);
	} else {
		/* coins d'un rectangle de sélection */
		static const int coin[8][2] = { { -14, -11 }, { -2, -11 }, { 10, -11 },
		    { -14, -1 }, { 10, -1 }, { -14, 9 }, { -2, 9 }, { 10, 9 } };

		for (k = 0; k < 8; k++)
			ptk_fill(p, w->surface, cx + coin[k][0], cy + coin[k][1], 4, 4, n);
	}
}

static void
dessin(PtkWindow *w, void *user)
{
	Ptk *p = w->p;
	static const char *noms[4] = { "Écran", "Fenêtre", "Menu", "Sélection" };
	int k;

	(void)user;
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	/* bandeau du nom, comme celui du panneau de Snapz */
	ptk_fill(p, w->surface, 0, 0, w->w, 22, ptk_color(p, "#333355"));
	ptk_line(p, w->surface, 0, 22, w->w, 22, p->black);
	{
		/* l'icône du programme en tête du bandeau, comme le logo de Snapz */
		Pixmap ip = None, im = None;

		if (ptk_load_xpm(p, "menu/app-instantane.xpm", &ip, &im) == 0 && ip != None) {
			ptk_draw_pixmap(p, w->surface, ip, im, 8, 3, 16, 16);
			XFreePixmap(p->dpy, ip);
			if (im != None)
				XFreePixmap(p->dpy, im);
		}
	}
	ptk_text(p, w->surface, p->font_title, 30, 16, "Instantané", p->white);
	ptk_text(p, w->surface, p->font_small, w->w - 128, 15, "Capture d'écran Platine",
	    ptk_color(p, "#ccccff"));
	ptk_text(p, w->surface, p->font_menu_bold, 12, 42, "Capture :", p->text);
	for (k = 0; k < 4; k++) {
		int x = 54 + 96 * k + 32;

		ptk_text(p, w->surface, p->font_small,
		    x - ptk_text_width(p, p->font_small, noms[k]) / 2, 106, noms[k],
		    k == MODE_MENU || !w->active ? p->text_inactif : p->text);
	}
	p->inactif = !w->active;
	ptk_separateur(p, w->surface, 8, 116, w->w - 16, 2);
	p->inactif = 0;
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	(void)w;
	(void)user;
	if (strcmp(cmd, "Fermer") == 0 || strcmp(cmd, "Annuler") == 0) {
		panneau_fermer();
		liberer_fige();
	} else if (strcmp(cmd, "Quitter Instantané") == 0)
		ptk_app_quit(&I.app);
}

static void
evenement(PtkWindow *w, PtkEvent *ev, void *user)
{
	(void)w;
	(void)user;
	if (ev->type != PTK_EV_KEY)
		return;
	/* raccourcis du panneau : É ou E Écran, F Fenêtre, S Sélection, Échap */
	if (ev->keysym == XK_Escape) {
		panneau_fermer();
		liberer_fige();
	} else if (ev->keysym == XK_e || ev->keysym == XK_E || ev->keysym == XK_eacute)
		agir(MODE_ECRAN);
	else if (ev->keysym == XK_f || ev->keysym == XK_F)
		agir(MODE_FENETRE);
	else if (ev->keysym == XK_s || ev->keysym == XK_S)
		agir(MODE_SELECTION);
}

static PtkControl *
menu_local(PtkWindow *w, int x, int y, int lw, const char *etiquette, int pw,
    const char *items, const char *valeur)
{
	PtkControl *c;

	ptk_control_add(w, PTK_STATIC, x, y + 2, lw, 16, etiquette);
	c = ptk_control_add(w, PTK_POPUP, x + lw, y, pw, 20, "");
	ptk_control_set_items(c, items);
	ptk_control_set_text(c, valeur);
	return c;
}

static void
panneau_ouvrir(void)
{
	static void (*const actions[4])(PtkWindow *, PtkControl *, void *) = {
		b_ecran, b_fenetre, NULL, b_selection };
	PtkWindow *w;
	int k;

	w = ptk_window_new(&I.app, "Instantané", (I.sw - 460) / 2, 90, 460, 206);
	I.panneau = w;
	ptk_window_set_menu(w, "Fichier|Annuler\t⌘.|Fermer\t⌘W|-|Quitter Instantané");
	ptk_window_set_draw(w, dessin, NULL);
	ptk_window_set_command(w, commande, NULL);
	ptk_window_set_event(w, evenement, NULL);
	{
		XSizeHints h;

		memset(&h, 0, sizeof(h));
		h.flags = PMinSize | PMaxSize | PPosition;
		h.x = (I.sw - 460) / 2;
		h.y = 90;
		h.min_width = h.max_width = 460;
		h.min_height = h.max_height = 206;
		XSetWMNormalHints(I.dpy, w->win, &h);
	}
	for (k = 0; k < 4; k++) {
		PtkControl *b = ptk_control_add(w, PTK_BISEAU, 54 + 96 * k, 50, 64, 44, "");

		b->value = k;
		b->contenu = icone;
		if (actions[k] != NULL)
			ptk_control_set_action(b, actions[k], NULL);
		else
			b->enabled = 0;
	}
	I.envoyer = menu_local(w, 12, 126, 92, "Envoyer vers :", 156,
	    "Dossier Captures|Presse-papiers|Enregistrer sous…", I.r_envoyer);
	I.couleurs = menu_local(w, 272, 126, 66, "Couleurs :", 120,
	    "Inchangées|Niveaux de gris|Noir et blanc", I.r_couleurs);
	I.echelle = menu_local(w, 12, 152, 92, "Échelle :", 72, "100 %|75 %|50 %|25 %", I.r_echelle);
	I.format = menu_local(w, 272, 152, 66, "Format :", 72, "PNG|PPM", I.r_format);
	I.voir_curseur = ptk_control_add(w, PTK_CHECKBOX, 14, 182, 150, 14, "Curseur visible");
	I.voir_curseur->checked = I.r_curseur;
	I.choisir = ptk_control_add(w, PTK_CHECKBOX, 230, 182, 210, 14,
	    "Choisir le nom du fichier");
	I.choisir->checked = I.r_choisir;
	ptk_window_show(w);
}

/* l'écran pris tel quel, avant toute fenêtre d'Instantané ; 0 en cas d'échec */
static int
figer(void)
{
	GC g;
	XGCValues v;
	Window r, c;
	int wx, wy;
	unsigned m;

	liberer_fige();
	XQueryPointer(I.dpy, I.racine, &r, &c, &I.px, &I.py, &wx, &wy, &m);
	I.image = XGetImage(I.dpy, I.racine, 0, 0, (unsigned)I.sw, (unsigned)I.sh, AllPlanes, ZPixmap);
	if (I.image == NULL)
		return 0;
	I.fige = XCreatePixmap(I.dpy, I.racine, (unsigned)I.sw, (unsigned)I.sh,
	    (unsigned)DefaultDepth(I.dpy, DefaultScreen(I.dpy)));
	v.subwindow_mode = IncludeInferiors;
	g = XCreateGC(I.dpy, I.racine, GCSubwindowMode, &v);
	XPutImage(I.dpy, I.fige, g, I.image, 0, 0, 0, 0, (unsigned)I.sw, (unsigned)I.sh);
	XFreeGC(I.dpy, g);
	if (I.xfixes)
		I.curseur = XFixesGetCursorImage(I.dpy);
	return 1;
}

/* ⌘⇧3 : l'écran est figé, puis le panneau s'ouvre */
static void
declencher(void)
{
	if (I.panneau != NULL || I.sur != None || !figer())
		return;
	ptk_son(&I.app.ptk, "wopn");
	printf("instantane : panneau\n");
	panneau_ouvrir();
}

/* capture sans panneau (module Control Strip) : Écran tout de suite,
 * Fenêtre et Sélection par la désignation sur l'écran figé */
static void
capture_directe(int mode)
{
	if (I.panneau != NULL || I.sur != None || !figer())
		return;
	printf("instantane : capture directe\n");
	agir(mode);
}

static int
racine_touche(PtkApp *app, XEvent *e, void *user)
{
	if (e->type == KeyPress && e->xkey.window == I.racine) {
		declencher();
		return 1;
	}
	return hors(app, e, user);
}

static int saisie_refusee;

static int
erreur_x(Display *d, XErrorEvent *e)
{
	(void)d;
	if (e->error_code == BadAccess)
		saisie_refusee = 1;	/* ⌘⇧3 encore tenu par l'ancienne instance */
	return 0;
}

int
main(int argc, char **argv)
{
	static const unsigned mods[4] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
	KeyCode k3;
	int ev, er, i, deja;

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (ptk_app_init(&I.app, "Instantané", "app-instantane") != 0)
		return 1;
	I.dpy = I.app.dpy;
	I.racine = I.app.root;
	I.sw = DisplayWidth(I.dpy, I.app.screen);
	I.sh = DisplayHeight(I.dpy, I.app.screen);
	I.app.quit_on_last_close = 0;
	I.app.hors = racine_touche;
	I.xfixes = XFixesQueryExtension(I.dpy, &ev, &er);
	I.a_clip = XInternAtom(I.dpy, "CLIPBOARD", False);
	I.a_targets = XInternAtom(I.dpy, "TARGETS", False);
	I.a_png = XInternAtom(I.dpy, "image/png", False);
	I.sel = XCreateSimpleWindow(I.dpy, I.racine, -10, -10, 1, 1, 0, 0, 0);
	reglages_lire();
	/* une seule instance : la nouvelle prend la sélection _PLATINE_INSTANTANE,
	 * l'ancienne s'en va (relève après une réinstallation) */
	I.a_moi = XInternAtom(I.dpy, "_PLATINE_INSTANTANE", False);
	/* une instance tournait déjà : on est lancé depuis le menu Pomme (ou
	 * réinstallé) — la relève faite, le panneau s'ouvre aussitôt */
	deja = XGetSelectionOwner(I.dpy, I.a_moi) != None;
	XSetSelectionOwner(I.dpy, I.a_moi, I.sel, CurrentTime);
	XSetErrorHandler(erreur_x);
	/* ⌘⇧3 : ⌘ est Option (Mod1) sous Platine ; verrouillages ignorés */
	k3 = XKeysymToKeycode(I.dpy, XK_3);
	for (ev = 0; ev < 40; ev++) {
		saisie_refusee = 0;
		for (i = 0; i < 4; i++)
			XGrabKey(I.dpy, k3, Mod1Mask | ShiftMask | mods[i], I.racine, False,
			    GrabModeAsync, GrabModeAsync);
		XSync(I.dpy, False);
		if (!saisie_refusee)
			break;
		usleep(100000);
	}
	XSetErrorHandler(NULL);
	XSelectInput(I.dpy, I.racine, KeyPressMask);
	XSync(I.dpy, False);
	printf("instantane : prêt (⌘⇧3)\n");
	/* argument (module Control Strip, menu Pomme) : « maintenant » ou
	 * « panneau » ouvre le panneau, « ecran », « fenetre », « selection »
	 * capturent sans lui ; sans argument, une relève ouvre le panneau */
	if (argc > 1 && strcmp(argv[1], "ecran") == 0)
		capture_directe(MODE_ECRAN);
	else if (argc > 1 && strcmp(argv[1], "fenetre") == 0)
		capture_directe(MODE_FENETRE);
	else if (argc > 1 && strcmp(argv[1], "selection") == 0)
		capture_directe(MODE_SELECTION);
	else if ((argc > 1 && (strcmp(argv[1], "maintenant") == 0 ||
	    strcmp(argv[1], "panneau") == 0)) || (argc == 1 && deja))
		declencher();
	I.app.running = 1;
	ptk_app_run(&I.app);
	return 0;
}
