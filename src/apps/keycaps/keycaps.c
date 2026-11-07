/*
 * Key Caps : le clavier à l'écran, comme l'accessoire Key Caps de Mac OS 9.
 *
 * Fenêtre à taille fixe : en haut, la bande de texte où s'inscrivent les caractères tapés
 * (au clavier ou en cliquant sur une touche) ; dessous, le clavier dessiné, touches
 * enfoncées en noir. Les libellés suivent la police choisie (menu Police) et les touches
 * de modification maintenues (Maj, AltGr) : comme sous Mac OS 9, on voit d'un coup d'œil
 * quels caractères donne chaque combinaison ; les touches mortes sont encadrées.
 *
 * Le clavier dessiné est celui du 701 (AZERTY). Chaque touche est retrouvée par le
 * symbole de son niveau de base (XKeysymToKeycode), puis son libellé est lu dans la
 * table X du moment : un autre agencement s'affiche juste.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

PTK_IDENTITE("Key Caps", "kcap", "app-touches", PTK_VERSION, "");

/* une touche : position en quarts d'unité, largeur en quarts, symbole de base */
typedef struct {
	int x, y, l;
	KeySym ks;
	int code;	/* keycode, résolu au démarrage */
	int bas;	/* enfoncée */
} Touche;

#define U 26		/* une unité de touche, en pixels */
#define Q(n) ((n) * U / 4)
#define BANDE 30	/* hauteur de la bande de texte */
#define MARGE 10

static Touche touches[] = {
	/* rangée des fonctions : demi-hauteur */
	{ 0, 0, 4, XK_Escape, 0, 0 }, { 5, 0, 4, XK_F1, 0, 0 }, { 9, 0, 4, XK_F2, 0, 0 }, { 13, 0, 4, XK_F3, 0, 0 },
	{ 17, 0, 4, XK_F4, 0, 0 }, { 22, 0, 4, XK_F5, 0, 0 }, { 26, 0, 4, XK_F6, 0, 0 }, { 30, 0, 4, XK_F7, 0, 0 },
	{ 34, 0, 4, XK_F8, 0, 0 }, { 39, 0, 4, XK_F9, 0, 0 }, { 43, 0, 4, XK_F10, 0, 0 }, { 47, 0, 4, XK_F11, 0, 0 },
	{ 51, 0, 4, XK_F12, 0, 0 }, { 56, 0, 4, XK_Delete, 0, 0 },
	/* chiffres */
	{ 0, 1, 4, XK_twosuperior, 0, 0 }, { 4, 1, 4, XK_ampersand, 0, 0 }, { 8, 1, 4, XK_eacute, 0, 0 },
	{ 12, 1, 4, XK_quotedbl, 0, 0 }, { 16, 1, 4, XK_apostrophe, 0, 0 }, { 20, 1, 4, XK_parenleft, 0, 0 },
	{ 24, 1, 4, XK_minus, 0, 0 }, { 28, 1, 4, XK_egrave, 0, 0 }, { 32, 1, 4, XK_underscore, 0, 0 },
	{ 36, 1, 4, XK_ccedilla, 0, 0 }, { 40, 1, 4, XK_agrave, 0, 0 }, { 44, 1, 4, XK_parenright, 0, 0 },
	{ 48, 1, 4, XK_equal, 0, 0 }, { 52, 1, 8, XK_BackSpace, 0, 0 },
	/* AZERTY */
	{ 0, 2, 6, XK_Tab, 0, 0 }, { 6, 2, 4, XK_a, 0, 0 }, { 10, 2, 4, XK_z, 0, 0 }, { 14, 2, 4, XK_e, 0, 0 },
	{ 18, 2, 4, XK_r, 0, 0 }, { 22, 2, 4, XK_t, 0, 0 }, { 26, 2, 4, XK_y, 0, 0 }, { 30, 2, 4, XK_u, 0, 0 },
	{ 34, 2, 4, XK_i, 0, 0 }, { 38, 2, 4, XK_o, 0, 0 }, { 42, 2, 4, XK_p, 0, 0 },
	{ 46, 2, 4, XK_dead_circumflex, 0, 0 }, { 50, 2, 4, XK_dollar, 0, 0 }, { 54, 2, 6, XK_Return, 0, 0 },
	/* QSDF */
	{ 0, 3, 7, XK_Caps_Lock, 0, 0 }, { 7, 3, 4, XK_q, 0, 0 }, { 11, 3, 4, XK_s, 0, 0 }, { 15, 3, 4, XK_d, 0, 0 },
	{ 19, 3, 4, XK_f, 0, 0 }, { 23, 3, 4, XK_g, 0, 0 }, { 27, 3, 4, XK_h, 0, 0 }, { 31, 3, 4, XK_j, 0, 0 },
	{ 35, 3, 4, XK_k, 0, 0 }, { 39, 3, 4, XK_l, 0, 0 }, { 43, 3, 4, XK_m, 0, 0 }, { 47, 3, 4, XK_ugrave, 0, 0 },
	{ 51, 3, 4, XK_asterisk, 0, 0 },
	/* WXCV */
	{ 0, 4, 5, XK_Shift_L, 0, 0 }, { 5, 4, 4, XK_less, 0, 0 }, { 9, 4, 4, XK_w, 0, 0 }, { 13, 4, 4, XK_x, 0, 0 },
	{ 17, 4, 4, XK_c, 0, 0 }, { 21, 4, 4, XK_v, 0, 0 }, { 25, 4, 4, XK_b, 0, 0 }, { 29, 4, 4, XK_n, 0, 0 },
	{ 33, 4, 4, XK_comma, 0, 0 }, { 37, 4, 4, XK_semicolon, 0, 0 }, { 41, 4, 4, XK_colon, 0, 0 },
	{ 45, 4, 4, XK_exclam, 0, 0 }, { 49, 4, 7, XK_Shift_R, 0, 0 }, { 56, 4, 4, XK_Up, 0, 0 },
	/* espace */
	{ 0, 5, 5, XK_Control_L, 0, 0 }, { 5, 5, 4, XK_Super_L, 0, 0 }, { 9, 5, 5, XK_Alt_L, 0, 0 },
	{ 14, 5, 22, XK_space, 0, 0 }, { 36, 5, 5, XK_ISO_Level3_Shift, 0, 0 }, { 41, 5, 4, XK_Menu, 0, 0 },
	{ 45, 5, 4, XK_Control_R, 0, 0 }, { 52, 5, 4, XK_Left, 0, 0 }, { 56, 5, 4, XK_Down, 0, 0 },
	{ 60, 5, 4, XK_Right, 0, 0 },
};
#define NT ((int)(sizeof(touches) / sizeof(touches[0])))

static const char *polices[] = { "Charcoal", "Chicago", "Geneva", "Monaco", "Gadget",
    "Sand", "Techno", "Textile", "Capitals" };
#define NP ((int)(sizeof(polices) / sizeof(polices[0])))

static struct {
	PtkApp app;
	PtkWindow *w;
	XftFont *police;
	int choix;
	int maj, altgr, verr;	/* touches de modification maintenues (ou verrouillée) */
	int clic;		/* touche cliquée à la souris, -1 */
	char texte[1024];
} K;

static int
haut_rangee(int r)
{
	return r == 0 ? U * 3 / 4 : U;
}

static int
y_rangee(int r)
{
	return r == 0 ? 0 : U * 3 / 4 + 6 + (r - 1) * U;
}

static void
rect_touche(const Touche *t, int *x, int *y, int *w, int *h)
{
	*x = MARGE + Q(t->x);
	*y = MARGE + BANDE + 10 + y_rangee(t->y);
	*w = Q(t->l) - 2;
	*h = haut_rangee(t->y) - 2;
}

/* keysym → UTF-8 : Latin-1 et keysyms Unicode directement, touches mortes par leur accent */
static int
ks_utf8(KeySym ks, char *out, size_t n)
{
	static const struct { KeySym ks; const char *s; } morts[] = {
		{ XK_dead_circumflex, "^" }, { XK_dead_diaeresis, "¨" }, { XK_dead_grave, "`" },
		{ XK_dead_acute, "´" }, { XK_dead_tilde, "~" }, { XK_dead_cedilla, "¸" },
		{ XK_EuroSign, "€" }, { XK_dead_abovering, "°" }, { XK_dead_caron, "ˇ" },
		{ XK_dead_macron, "¯" }, { XK_dead_breve, "˘" }, { XK_dead_ogonek, "˛" },
		{ XK_dead_doubleacute, "˝" }, { XK_dead_abovedot, "˙" },
	};
	unsigned long u = 0;
	size_t i;

	out[0] = '\0';
	for (i = 0; i < sizeof(morts) / sizeof(morts[0]); i++)
		if (morts[i].ks == ks) {
			snprintf(out, n, "%s", morts[i].s);
			return 2;
		}
	if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff))
		u = ks;
	else if ((ks & 0xff000000UL) == 0x01000000UL)
		u = ks & 0x00ffffffUL;
	if (u == 0 || n < 5)
		return 0;
	if (u < 0x80) {
		out[0] = (char)u;
		out[1] = '\0';
	} else if (u < 0x800) {
		out[0] = (char)(0xc0 | (u >> 6));
		out[1] = (char)(0x80 | (u & 0x3f));
		out[2] = '\0';
	} else if (u < 0x10000) {
		out[0] = (char)(0xe0 | (u >> 12));
		out[1] = (char)(0x80 | ((u >> 6) & 0x3f));
		out[2] = (char)(0x80 | (u & 0x3f));
		out[3] = '\0';
	} else {
		out[0] = (char)(0xf0 | (u >> 18));
		out[1] = (char)(0x80 | ((u >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((u >> 6) & 0x3f));
		out[3] = (char)(0x80 | (u & 0x3f));
		out[4] = '\0';
	}
	return 1;
}

/* caractère d'une touche selon les modificateurs : 1 normal, 2 touche morte, 0 rien */
static int
caractere(const Touche *t, char *out, size_t n)
{
	int niveau = (K.maj ? 1 : 0) + (K.altgr ? 2 : 0);
	KeySym ks;

	out[0] = '\0';
	if (t->code == 0)
		return 0;
	ks = XkbKeycodeToKeysym(K.app.dpy, (KeyCode)t->code, 0, niveau);
	if (ks == NoSymbol && niveau >= 2)
		ks = XkbKeycodeToKeysym(K.app.dpy, (KeyCode)t->code, 0, niveau - 2);
	/* Verr. maj. : les lettres en capitales */
	if (K.verr && !K.maj && niveau < 2) {
		KeySym bas, haut;

		XConvertCase(ks, &bas, &haut);
		ks = haut;
	}
	if (ks == XK_space)
		return 0;
	return ks_utf8(ks, out, n);
}

static void
dessiner(PtkWindow *w, void *user)
{
	Ptk *p = w->p;
	PtkColor gris = ptk_color(p, "#dddddd"), ombre = ptk_color(p, "#888888");
	int i, tw;

	(void)user;
	/* bande de texte : champ blanc, le texte défile vers la gauche */
	ptk_field(p, w->surface, MARGE, MARGE, w->w - 2 * MARGE, BANDE - 4, 0);
	ptk_fill(p, w->surface, MARGE + 3, MARGE + 3, w->w - 2 * MARGE - 6, BANDE - 10, p->white);
	ptk_clip(p, MARGE + 3, MARGE + 3, w->w - 2 * MARGE - 6, BANDE - 10);
	tw = ptk_text_width(p, K.police, K.texte);
	ptk_text(p, w->surface, K.police, tw > w->w - 2 * MARGE - 16 ?
	    w->w - MARGE - 8 - tw : MARGE + 6, MARGE + 3 + (BANDE - 10 + K.police->ascent -
	    K.police->descent) / 2, K.texte, p->text);
	ptk_unclip(p);
	/* le clavier */
	for (i = 0; i < NT; i++) {
		Touche *t = &touches[i];
		char c[8];
		int x, y, tl, th, m, bas = t->bas || K.clic == i;

		rect_touche(t, &x, &y, &tl, &th);
		ptk_fill(p, w->surface, x + 1, y + 1, tl, th, ombre);
		ptk_fill(p, w->surface, x, y, tl, th, bas ? p->black : gris);
		ptk_rect(p, w->surface, x, y, tl, th, p->black);
		if (!bas) {
			ptk_line(p, w->surface, x + 1, y + 1, x + tl - 2, y + 1, p->white);
			ptk_line(p, w->surface, x + 1, y + 1, x + 1, y + th - 2, p->white);
		}
		m = caractere(t, c, sizeof(c));
		if (m == 0)
			continue;
		{
			int lw = ptk_text_width(p, K.police, c);
			int bx = x + (tl - lw) / 2, by = y + (th + K.police->ascent - K.police->descent) / 2;

			/* touche morte : son accent dans un cadre, comme le relief de Mac OS 9 */
			if (m == 2)
				ptk_rect(p, w->surface, x + 3, y + 3, tl - 6, th - 6, bas ? p->white : ombre);
			ptk_text(p, w->surface, K.police, bx, by, c, bas ? p->white : p->text);
		}
	}
}

static void
ajouter(const char *s)
{
	size_t l = strlen(K.texte), n = strlen(s);

	if (n == 0)
		return;
	if (l + n >= sizeof(K.texte)) {	/* garder la fin */
		memmove(K.texte, K.texte + l / 2, l - l / 2 + 1);
		l = strlen(K.texte);
	}
	memcpy(K.texte + l, s, n + 1);
}

static void
effacer_dernier(void)
{
	size_t l = strlen(K.texte);

	while (l > 0 && ((unsigned char)K.texte[l - 1] & 0xc0) == 0x80)
		l--;
	if (l > 0)
		l--;
	K.texte[l] = '\0';
}

static int
touche_du_code(int code)
{
	int i;

	for (i = 0; i < NT; i++)
		if (touches[i].code == code)
			return i;
	return -1;
}

static int
touche_en(int x, int y)
{
	int i, tx, ty, tw, th;

	for (i = 0; i < NT; i++) {
		rect_touche(&touches[i], &tx, &ty, &tw, &th);
		if (x >= tx && x < tx + tw && y >= ty && y < ty + th)
			return i;
	}
	return -1;
}

/* une touche frappée (clavier ou clic) : son caractère s'inscrit dans la bande */
static void
frapper(int i)
{
	char c[8];
	KeySym ks = XkbKeycodeToKeysym(K.app.dpy, (KeyCode)touches[i].code, 0, 0);

	if (ks == XK_BackSpace) {
		effacer_dernier();
		return;
	}
	if (ks == XK_space) {
		ajouter(" ");
		return;
	}
	if (caractere(&touches[i], c, sizeof(c)) == 1)
		ajouter(c);
}

static void
modificateur(KeySym ks, int bas)
{
	if (ks == XK_Shift_L || ks == XK_Shift_R)
		K.maj = bas;
	else if (ks == XK_ISO_Level3_Shift || ks == XK_Mode_switch)
		K.altgr = bas;
	else if (ks == XK_Caps_Lock && bas)
		K.verr = !K.verr;
}

static void
evenement(PtkWindow *w, PtkEvent *e, void *user)
{
	int i;

	(void)user;
	switch (e->type) {
	case PTK_EV_KEY:
	case PTK_EV_KEY_UP: {
		int bas = e->type == PTK_EV_KEY;
		KeySym ks = XkbKeycodeToKeysym(K.app.dpy, (KeyCode)e->keycode, 0, 0);

		/* ⌘ (Alt) : raccourci de menu, laissé au gestionnaire de fenêtres */
		if (bas && (e->state & Mod1Mask))
			break;
		modificateur(ks, bas);
		i = touche_du_code(e->keycode);
		if (i >= 0) {
			if (bas && !touches[i].bas)
				frapper(i);
			else if (bas)	/* répétition automatique */
				frapper(i);
			touches[i].bas = bas;
		}
		ptk_window_invalidate(w);
		break;
	}
	case PTK_EV_MOUSE_DOWN:
		K.clic = touche_en(e->x, e->y);
		if (K.clic >= 0) {
			KeySym ks = XkbKeycodeToKeysym(K.app.dpy, (KeyCode)touches[K.clic].code, 0, 0);

			/* un clic sur Maj ou AltGr la verrouille jusqu'au clic suivant */
			if (ks == XK_Shift_L || ks == XK_Shift_R)
				K.maj = !K.maj;
			else if (ks == XK_ISO_Level3_Shift)
				K.altgr = !K.altgr;
			else if (ks == XK_Caps_Lock)
				K.verr = !K.verr;
			else
				frapper(K.clic);
		}
		ptk_window_invalidate(w);
		break;
	case PTK_EV_MOUSE_UP:
		if (K.clic >= 0) {
			K.clic = -1;
			ptk_window_invalidate(w);
		}
		break;
	case PTK_EV_ACTIVATE:
		/* modificateurs relâchés pendant que la fenêtre était derrière */
		K.maj = K.altgr = 0;
		for (i = 0; i < NT; i++)
			touches[i].bas = 0;
		break;
	default:
		break;
	}
}

static void
menu(void)
{
	char m[1024];
	int i, n;

	n = snprintf(m, sizeof(m),
	    "Fichier|Fermer\t⌘W|-|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|~Coller\t⌘V|Effacer\n"
	    "Police");
	for (i = 0; i < NP && n < (int)sizeof(m); i++)
		n += snprintf(m + n, sizeof(m) - (size_t)n, "|%s%s",
		    i == K.choix ? "√ " : "", polices[i]);
	if (n < (int)sizeof(m))
		snprintf(m + n, sizeof(m) - (size_t)n, "\nAide|À propos de Key Caps");
	ptk_window_set_menu(K.w, m);
}

static void
choisir_police(int i)
{
	char spec[64];
	XftFont *f;

	snprintf(spec, sizeof(spec), "%s:pixelsize=12", polices[i]);
	f = ptk_font(&K.app.ptk, spec, "sans:pixelsize=12");
	if (f == NULL)
		return;
	K.police = f;
	K.choix = i;
	menu();
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	int i;

	(void)user;
	if (strncmp(cmd, "√ ", 4) == 0)
		cmd += 4;
	for (i = 0; i < NP; i++)
		if (strcmp(cmd, polices[i]) == 0) {
			choisir_police(i);
			ptk_window_invalidate(w);
			return;
		}
	if (strcmp(cmd, "Copier") == 0 || strcmp(cmd, "Couper") == 0) {
		ptk_copier(w, K.texte);
		if (strcmp(cmd, "Couper") == 0)
			K.texte[0] = '\0';
	} else if (strcmp(cmd, "Effacer") == 0) {
		K.texte[0] = '\0';
	} else if (strcmp(cmd, "À propos de Key Caps") == 0) {
		ptk_alert(w, "Key Caps",
		    "Montre les caractères de chaque touche dans la police choisie. "
		    "Maintenez Maj ou AltGr pour voir les autres caractères ; "
		    "les touches mortes sont encadrées.", "OK");
	}
	ptk_window_invalidate(w);
}

int
main(void)
{
	int i, larg = MARGE * 2 + Q(64) + 2, haut = MARGE * 2 + BANDE + 10 + y_rangee(5) + U + 2;
	XSizeHints h;

	memset(&K, 0, sizeof(K));
	K.clic = -1;
	if (ptk_app_init(&K.app, "Key Caps", "app-touches") != 0)
		return 1;
	K.w = ptk_window_new(&K.app, "Key Caps", 120, 90, larg, haut);
	if (K.w == NULL)
		return 1;
	/* taille fixe, comme la fenêtre de Key Caps */
	memset(&h, 0, sizeof(h));
	h.flags = PMinSize | PMaxSize | PPosition;
	h.min_width = h.max_width = larg;
	h.min_height = h.max_height = haut;
	XSetWMNormalHints(K.app.dpy, K.w->win, &h);
	ptk_window_set_document(K.w, NULL);
	for (i = 0; i < NT; i++)
		touches[i].code = XKeysymToKeycode(K.app.dpy, touches[i].ks);
	K.choix = 0;
	choisir_police(ptk_reglage_int("keycaps_police", 0) % NP);
	if (K.police == NULL)
		K.police = K.app.ptk.font_menu;
	ptk_window_set_draw(K.w, dessiner, NULL);
	ptk_window_set_event(K.w, evenement, NULL);
	ptk_window_set_command(K.w, commande, NULL);
	ptk_window_show(K.w);
	ptk_app_run(&K.app);
	return 0;
}
