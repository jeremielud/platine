/*
 * platine-calculette : copie de la Calculette de Mac OS 9 (Calculator 7.5), relevée au pixel sur la VM.
 * La barre de titre noire est dessinée par platine-wm (style « calculette »). Fenêtre 103×147, pur noir
 * et blanc : fond en damier (motif 1 bit de la ressource ppat), afficheur 86×20, touches 18×16 et 2 px d'ombre, en Geneva 9. Calcul de
 * gauche à droite ; textes d'erreur de la ressource STR# : infinity, -infinity, not a number.
 */
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include "../../../toolbox/ptk.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Calculette", "calc", "app-calculette", PTK_VERSION, "");


#define W 103
#define H 147

static const struct { char c; int x, y, w, h; int lx; } touches[] = {
	{ 'C', 8, 32, 18, 16, 6 }, { '=', 30, 32, 18, 16, 6 }, { '/', 52, 32, 18, 16, 6 }, { '*', 74, 32, 18, 16, 6 },
	{ '7', 8, 54, 18, 16, 6 }, { '8', 30, 54, 18, 16, 6 }, { '9', 52, 54, 18, 16, 6 }, { '-', 74, 54, 18, 16, 6 },
	{ '4', 8, 76, 18, 16, 6 }, { '5', 30, 76, 18, 16, 6 }, { '6', 52, 76, 18, 16, 6 }, { '+', 74, 76, 18, 16, 6 },
	{ '1', 8, 98, 18, 16, 6 }, { '2', 30, 98, 18, 16, 6 }, { '3', 52, 98, 18, 16, 6 }, { 'e', 74, 98, 18, 38, 6 },
	{ '0', 8, 120, 40, 16, 6 }, { '.', 52, 120, 18, 16, 6 },
};
#define NT ((int)(sizeof(touches) / sizeof(touches[0])))

static Ptk ptk;
static Display *dpy;
static Window win;
static Pixmap dos, fond;
static int fond_w, fond_h;
static XftFont *police;
static Atom a_cmd, a_delete, a_clip;

/* --- Le calcul --- */
static char saisie[32] = "0", affiche[32] = "0";
static long double acc, val;
static char op;
static int en_saisie, deux_operandes;

static void
formater(long double v, char *b, size_t n)
{
	char t[64], *e;

	if (isnan(v)) {
		snprintf(b, n, "not a number");
		return;
	}
	if (isinf(v)) {
		snprintf(b, n, v > 0 ? "infinity" : "-infinity");
		return;
	}
	if (fabsl(v) < 1e13L && v == truncl(v))
		snprintf(t, sizeof(t), "%.0Lf", v);
	else
		snprintf(t, sizeof(t), "%.11Lg", v);
	if (strcmp(t, "-0") == 0)
		snprintf(t, sizeof(t), "0");
	/* Mac OS : pas de zéro avant le point, exposant « E15 » */
	if (t[0] == '0' && t[1] == '.')
		memmove(t, t + 1, strlen(t));
	else if (t[0] == '-' && t[1] == '0' && t[2] == '.')
		memmove(t + 1, t + 2, strlen(t + 1));
	if ((e = strchr(t, 'e')) != NULL) {
		int x = atoi(e + 1);
		long double m;
		char ms[32];

		sscanf(t, "%Lf", &m);
		/* mantisse raccourcie pour tenir en 12 caractères */
		snprintf(ms, sizeof(ms), "%.*s", (int)(e - t), t);
		snprintf(t, sizeof(t), "%sE%d", ms, x);
		if (strlen(t) > 12) {
			int surplus = (int)strlen(t) - 12;
			size_t lm = strlen(ms);

			if ((size_t)surplus < lm)
				ms[lm - (size_t)surplus] = '\0';
			snprintf(t, sizeof(t), "%sE%d", ms, x);
		}
	}
	snprintf(b, n, "%s", t);
}

static void
calculer(void)
{
	switch (op) {
	case '+': acc += val; break;
	case '-': acc -= val; break;
	case '*': acc *= val; break;
	case '/': acc /= val; break;
	default: acc = val; break;
	}
}

static void
effacer(void)
{
	snprintf(saisie, sizeof(saisie), "0");
	snprintf(affiche, sizeof(affiche), "0");
	acc = val = 0;
	op = 0;
	en_saisie = 0;
	deux_operandes = 0;
}

static void
touche(char k)
{
	if (k == 'C') {
		effacer();
	} else if ((k >= '0' && k <= '9') || k == '.') {
		if (!en_saisie) {
			saisie[0] = '\0';
			en_saisie = 1;
			if (op != 0)
				deux_operandes = 1;
		}
		if (k == '.' && strchr(saisie, '.') != NULL)
			return;
		if (strlen(saisie) >= 13)
			return;
		if (k == '0' && strcmp(saisie, "0") == 0)
			return;
		if (strcmp(saisie, "0") == 0 && k != '.')
			saisie[0] = '\0';
		{
			size_t l = strlen(saisie);

			saisie[l] = k;
			saisie[l + 1] = '\0';
		}
		snprintf(affiche, sizeof(affiche), "%s", saisie);
		sscanf(saisie, "%Lf", &val);
	} else if (k == '+' || k == '-' || k == '*' || k == '/') {
		if (en_saisie || op == 0) {
			if (op != 0 && deux_operandes)
				calculer();
			else if (op == 0)
				acc = en_saisie ? val : acc;
			formater(acc, affiche, sizeof(affiche));
		}
		op = k;
		en_saisie = 0;
		deux_operandes = 0;
	} else if (k == 'e') {
		if (op != 0 && deux_operandes) {
			calculer();
			formater(acc, affiche, sizeof(affiche));
		} else if (en_saisie) {
			acc = val;
		}
		op = 0;
		en_saisie = 0;
		deux_operandes = 0;
	}
}

/* --- Le dessin --- */
static void
dessiner_touche(int i, int enfoncee)
{
	char s[2] = { touches[i].c == 'e' ? '=' : touches[i].c, 0 };
	int x = touches[i].x, y = touches[i].y, w = touches[i].w, h = touches[i].h;
	int base = (touches[i].c == 'e' ? 120 : y) + 12;

	ptk_fill(&ptk, dos, x + w, y + 2, 2, h, ptk.black);
	ptk_fill(&ptk, dos, x + 2, y + h, w, 2, ptk.black);
	ptk_fill(&ptk, dos, x, y, w, h, ptk.black);
	if (!enfoncee)
		ptk_fill(&ptk, dos, x + 1, y + 1, w - 2, h - 2, ptk.white);
	ptk_text(&ptk, dos, police, x + touches[i].lx - (s[0] == '.' ? 0 : 0), base, s, enfoncee ? ptk.white : ptk.black);
}

static void
dessiner_afficheur(void)
{
	ptk_fill(&ptk, dos, 7, 7, 86, 20, ptk.black);
	ptk_fill(&ptk, dos, 8, 8, 84, 18, ptk.white);
	ptk_text(&ptk, dos, police, 89 - ptk_text_width(&ptk, police, affiche), 21, affiche, ptk.black);
}

static void
montrer(void)
{
	XCopyArea(dpy, dos, win, ptk.gc, 0, 0, W, H, 0, 0);
	XFlush(dpy);
}

static void
dessiner(void)
{
	int x, y, i;
	static const struct { int x, y, w; } noirs[] = {
		/* coins intérieurs relevés ; le bord droit et le bas sont noirs, l'ombre vient du cadre */
		{ 0, 0, 2 }, { 0, 1, 1 }, { 100, 0, 2 }, { 101, 1, 1 },
		{ 0, 143, 1 }, { 0, 144, 1 }, { 0, 145, 2 }, { 101, 144, 1 }, { 100, 145, 2 },
	};

	if (fond != None) {
		for (y = 0; y < H; y += fond_h)
			for (x = 0; x < W; x += fond_w)
				XCopyArea(dpy, fond, dos, ptk.gc, 0, 0, (unsigned)fond_w, (unsigned)fond_h, x, y);
	} else {
		ptk_fill(&ptk, dos, 0, 0, W, H, ptk_color(&ptk, "#cccccc"));
	}
	ptk_fill(&ptk, dos, 102, 0, 1, H, ptk.black);
	ptk_fill(&ptk, dos, 0, 146, W, 1, ptk.black);
	for (i = 0; i < (int)(sizeof(noirs) / sizeof(noirs[0])); i++)
		ptk_fill(&ptk, dos, noirs[i].x, noirs[i].y, noirs[i].w, 1, ptk.black);
	dessiner_afficheur();
	for (i = 0; i < NT; i++)
		dessiner_touche(i, 0);
	montrer();
}

static int
touche_en(int x, int y)
{
	int i;

	for (i = 0; i < NT; i++)
		if (x >= touches[i].x && x < touches[i].x + touches[i].w &&
		    y >= touches[i].y && y < touches[i].y + touches[i].h)
			return i;
	return -1;
}

static void
actionner(int i)
{
	touche(touches[i].c);
	dessiner_afficheur();
	montrer();
}

/* touche du clavier : la touche à l'écran s'enfonce un instant, comme un clic */
static void
frappe(char k)
{
	int i;

	for (i = 0; i < NT; i++)
		if (touches[i].c == k) {
			dessiner_touche(i, 1);
			montrer();
			XSync(dpy, False);
			usleep(90000);
			dessiner_touche(i, 0);
			actionner(i);
			return;
		}
}

/* --- Presse-papiers --- */
static void
coller(const char *t)
{
	for (; *t != '\0'; t++) {
		char k = *t;

		if (k == 'c' || k == 'C')
			k = 'C';
		else if (k == ',' )
			k = '.';
		else if (k == '=' || k == '\n' || k == '\r')
			k = 'e';
		frappe(k);
	}
}

static void
commande(const char *c)
{
	if (strcmp(c, "Fermer") == 0 || strcmp(c, "Quitter") == 0) {
		exit(0);
	} else if (strcmp(c, "Copier") == 0 || strcmp(c, "Couper") == 0) {
		XSetSelectionOwner(dpy, a_clip, win, CurrentTime);
		if (strcmp(c, "Couper") == 0)
			frappe('C');
	} else if (strcmp(c, "Coller") == 0) {
		XConvertSelection(dpy, a_clip, XA_STRING, a_clip, win, CurrentTime);
	} else if (strcmp(c, "Effacer") == 0) {
		frappe('C');
	}
}

int
main(void)
{
	XSizeHints h;
	XEvent e;
	int suivie = -1, dedans = 0;
	const char *menu = "Fichier|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V|Effacer\n"
	    "Aide|Aide Platine";

	setlocale(LC_ALL, "C");
	if ((dpy = XOpenDisplay(NULL)) == NULL || ptk_init(&ptk, dpy, DefaultScreen(dpy)) != 0)
		return 1;
	police = ptk_police_bitmap(&ptk, "geneva-9.bdf", 9);
	if (police == NULL)
		police = ptk.font_small;
	fond = ptk_load_ppm(&ptk, "calculette-fond.ppm", &fond_w, &fond_h);
	win = XCreateSimpleWindow(dpy, ptk.root, 347, 124, W, H, 0, 0, ptk.white.pixel);
	dos = XCreatePixmap(dpy, win, W, H, (unsigned)DefaultDepth(dpy, ptk.screen));
	XStoreName(dpy, win, "Calculette");
#define PROP(n, v) XChangeProperty(dpy, win, XInternAtom(dpy, n, False), XInternAtom(dpy, "UTF8_STRING", False), 8, \
	PropModeReplace, (const unsigned char *)(v), (int)strlen(v))
	PROP("_NET_WM_NAME", "Calculette");
	PROP("_PLATINE_APP_NAME", "Calculette");
	PROP("_PLATINE_ICON", "app-calculette");
	PROP("_PLATINE_MENU", menu);
	PROP("_PLATINE_STYLE", "calculette");
	a_cmd = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	a_clip = XInternAtom(dpy, "CLIPBOARD", False);
	XSetWMProtocols(dpy, win, &a_delete, 1);
	memset(&h, 0, sizeof(h));
	h.flags = PPosition | PMinSize | PMaxSize;
	h.x = 347;
	h.y = 124;
	h.min_width = h.max_width = W;
	h.min_height = h.max_height = H;
	XSetWMNormalHints(dpy, win, &h);
	XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | KeyPressMask |
	    PropertyChangeMask);
	dessiner();
	XMapWindow(dpy, win);
	for (;;) {
		XNextEvent(dpy, &e);
		switch (e.type) {
		case Expose:
			if (e.xexpose.count == 0)
				montrer();
			break;
		case ButtonPress:
			if (e.xbutton.button != Button1)
				break;
			suivie = touche_en(e.xbutton.x, e.xbutton.y);
			dedans = 1;
			if (suivie >= 0) {
				dessiner_touche(suivie, 1);
				montrer();
			}
			break;
		case MotionNotify:
			if (suivie >= 0) {
				int d = touche_en(e.xmotion.x, e.xmotion.y) == suivie;

				if (d != dedans) {
					dedans = d;
					dessiner_touche(suivie, d);
					montrer();
				}
			}
			break;
		case ButtonRelease:
			if (suivie >= 0) {
				dessiner_touche(suivie, 0);
				if (dedans)
					actionner(suivie);
				else
					montrer();
				suivie = -1;
			}
			break;
		case KeyPress: {
			char b[8] = "";
			KeySym ks = NoSymbol;
			int n = XLookupString(&e.xkey, b, sizeof(b) - 1, &ks, NULL);

			if (ks == XK_KP_Enter || ks == XK_Return)
				frappe('e');
			else if (ks == XK_Clear || ks == XK_Escape || ks == XK_Delete)
				frappe('C');
			else if (n == 1) {
				char k = b[0];

				if (k == 'c' || k == 'C')
					frappe('C');
				else if (k == '=')
					frappe('e');
				else if (k == ',')
					frappe('.');
				else
					frappe(k);
			}
			break;
		}
		case ClientMessage:
			if (e.xclient.message_type == XInternAtom(dpy, "WM_PROTOCOLS", False) &&
			    (Atom)e.xclient.data.l[0] == a_delete)
				return 0;
			if (e.xclient.message_type == a_cmd) {
				Atom t;
				int f;
				unsigned long nb, r;
				unsigned char *v = NULL;

				if (XGetWindowProperty(dpy, win, a_cmd, 0, 256, False, AnyPropertyType, &t, &f, &nb, &r, &v) ==
				    Success && v != NULL) {
					char c[128];
					char *tab;

					snprintf(c, sizeof(c), "%s", v);
					if ((tab = strchr(c, '\t')) != NULL)
						*tab = '\0';
					XFree(v);
					commande(c);
				}
			}
			break;
		case SelectionRequest: {
			XSelectionRequestEvent *q = &e.xselectionrequest;
			XEvent r;

			memset(&r, 0, sizeof(r));
			r.xselection.type = SelectionNotify;
			r.xselection.requestor = q->requestor;
			r.xselection.selection = q->selection;
			r.xselection.target = q->target;
			r.xselection.time = q->time;
			r.xselection.property = None;
			if (q->target == XA_STRING || q->target == XInternAtom(dpy, "UTF8_STRING", False)) {
				XChangeProperty(dpy, q->requestor, q->property, q->target, 8, PropModeReplace,
				    (unsigned char *)affiche, (int)strlen(affiche));
				r.xselection.property = q->property;
			}
			XSendEvent(dpy, q->requestor, False, 0, &r);
			break;
		}
		case SelectionNotify:
			if (e.xselection.property != None) {
				Atom t;
				int f;
				unsigned long nb, r;
				unsigned char *v = NULL;

				if (XGetWindowProperty(dpy, win, e.xselection.property, 0, 256, True, AnyPropertyType, &t, &f,
				    &nb, &r, &v) == Success && v != NULL) {
					coller((char *)v);
					XFree(v);
				}
			}
			break;
		}
	}
}
