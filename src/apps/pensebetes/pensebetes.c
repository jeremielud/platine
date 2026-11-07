/*
 * Pense-bêtes : les notes à l'écran de Mac OS 9 (Stickies).
 *
 * Chaque note est une fenêtre au cadre « pense-bete » de platine-wm (bord fin, barre de
 * 12 px plus foncée quand elle est active, case de fermeture et case de repli) et à la
 * couleur choisie au menu Couleur (Jaune, Bleu, Vert, Rose, Violet, Gris). Le texte va
 * à la ligne au bord de la note ; sélection à la souris ou Maj + flèches ; Couper,
 * Copier, Coller (presse-papiers X11), Effacer, Tout sélectionner.
 *
 * Les notes, leur place, leur taille et leur couleur sont enregistrées à chaque
 * changement dans ~/.config/platine/pense-betes et rouvertes au lancement ; au premier
 * lancement, une note de bienvenue. Fermer une note qui contient du texte demande, comme
 * Stickies, s'il faut l'enregistrer dans un fichier avant de la supprimer.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

PTK_IDENTITE("Pense-bêtes", "notz", "app-pense-betes", PTK_VERSION, "");

#define MAX_NOTES 32
#define MAX_TEXTE 8192
#define MAX_LIGNES 512
#define MARGE 4

static const struct { const char *nom, *hex; } couleurs[] = {
	{ "Jaune", "#ffff99" }, { "Bleu", "#ccffff" }, { "Vert", "#ccffcc" },
	{ "Rose", "#ffccff" }, { "Violet", "#ccccff" }, { "Gris", "#dddddd" },
};
#define NC ((int)(sizeof(couleurs) / sizeof(couleurs[0])))

typedef struct {
	PtkWindow *w;
	char texte[MAX_TEXTE];
	int caret, ancre;	/* sélection entre ancre et caret (octets) */
	int couleur;
	int glisse;		/* sélection à la souris en cours */
	int debuts[MAX_LIGNES];	/* début de chaque ligne affichée */
	int nlignes;
} Note;

static PtkApp app;
static Note *notes[MAX_NOTES];
static int nnotes;
static XftFont *police;
static int fermeture_globale;	/* Quitter : les notes restent enregistrées */

static Note *nouvelle_note(int x, int y, int w, int h, int couleur, const char *texte);

/* ---------- enregistrement ---------- */

static void
chemin_notes(char *out, size_t n)
{
	const char *h = getenv("HOME");

	snprintf(out, n, "%s/.config/platine/pense-betes", h ? h : ".");
}

/* place du cadre à l'écran : celle du client moins le bord et la barre */
static void
position(Note *nt, int *x, int *y)
{
	Window enfant;

	*x = nt->w->x;
	*y = nt->w->y;
	if (XTranslateCoordinates(app.dpy, nt->w->win, app.root, 0, 0, x, y, &enfant)) {
		*x -= 1;
		*y -= 12;
	}
}

static void
enregistrer_tout(void)
{
	char ch[1024], tmp[1100];
	FILE *f;
	int i;

	chemin_notes(ch, sizeof(ch));
	snprintf(tmp, sizeof(tmp), "%s.neuf", ch);
	if ((f = fopen(tmp, "w")) == NULL)
		return;
	for (i = 0; i < nnotes; i++) {
		Note *nt = notes[i];
		const char *p;
		int x, y;

		position(nt, &x, &y);
		fprintf(f, "note %d %d %d %d %s\n\t", x, y, nt->w->w, nt->w->h,
		    couleurs[nt->couleur].nom);
		for (p = nt->texte; *p; p++) {
			fputc(*p, f);
			if (*p == '\n')
				fputc('\t', f);
		}
		fputs("\nfin\n", f);
	}
	if (fclose(f) == 0)
		rename(tmp, ch);
}

static int
couleur_nommee(const char *nom)
{
	int i;

	for (i = 0; i < NC; i++)
		if (strcmp(nom, couleurs[i].nom) == 0)
			return i;
	return 0;
}

static int
charger_tout(void)
{
	char ch[1024], l[MAX_TEXTE], texte[MAX_TEXTE], nom[32];
	FILE *f;
	int x, y, w, h, n = 0, dedans = 0;
	size_t lt = 0;

	chemin_notes(ch, sizeof(ch));
	if ((f = fopen(ch, "r")) == NULL)
		return -1;
	while (fgets(l, sizeof(l), f) != NULL) {
		if (!dedans && sscanf(l, "note %d %d %d %d %31s", &x, &y, &w, &h, nom) == 5) {
			dedans = 1;
			lt = 0;
			texte[0] = '\0';
		} else if (dedans && strcmp(l, "fin\n") == 0) {
			if (lt > 0 && texte[lt - 1] == '\n')
				texte[--lt] = '\0';
			nouvelle_note(x, y, w, h, couleur_nommee(nom), texte);
			n++;
			dedans = 0;
		} else if (dedans && l[0] == '\t') {
			size_t k = strlen(l + 1);

			if (lt + k < sizeof(texte)) {
				memcpy(texte + lt, l + 1, k + 1);
				lt += k;
			}
		}
	}
	fclose(f);
	return n;
}

/* ---------- mise en page ---------- */

static int
debut_car(const char *s, int i)	/* début du caractère UTF-8 qui précède i */
{
	if (i <= 0)
		return 0;
	i--;
	while (i > 0 && ((unsigned char)s[i] & 0xc0) == 0x80)
		i--;
	return i;
}

static int
suite_car(const char *s, int i)
{
	if (s[i] == '\0')
		return i;
	i++;
	while (((unsigned char)s[i] & 0xc0) == 0x80)
		i++;
	return i;
}

static int
largeur(const char *s, int a, int b)
{
	char t[MAX_TEXTE];

	if (b <= a)
		return 0;
	memcpy(t, s + a, (size_t)(b - a));
	t[b - a] = '\0';
	return ptk_text_width(&app.ptk, police, t);
}

/* découpe en lignes : saut de ligne, ou dernier blanc avant le bord */
static void
couper_lignes(Note *nt)
{
	const char *s = nt->texte;
	int maxw = nt->w->w - 2 * MARGE - 4, i = 0, debut = 0, blanc = -1;

	nt->nlignes = 0;
	nt->debuts[nt->nlignes++] = 0;
	while (s[i] != '\0' && nt->nlignes < MAX_LIGNES) {
		int j = suite_car(s, i);

		if (s[i] == '\n') {
			debut = j;
			blanc = -1;
			nt->debuts[nt->nlignes++] = debut;
		} else {
			if (s[i] == ' ')
				blanc = j;
			if (largeur(s, debut, j) > maxw && i > debut) {
				debut = blanc > debut ? blanc : i;
				blanc = -1;
				nt->debuts[nt->nlignes++] = debut;
			}
		}
		i = j;
	}
}

static int
fin_ligne(Note *nt, int l)
{
	int f = l + 1 < nt->nlignes ? nt->debuts[l + 1] : (int)strlen(nt->texte);

	if (f > nt->debuts[l] && nt->texte[f - 1] == '\n')
		f--;
	return f;
}

static int
ligne_de(Note *nt, int pos)
{
	int l;

	for (l = nt->nlignes - 1; l > 0; l--)
		if (pos >= nt->debuts[l])
			break;
	return l;
}

static int
haut_ligne(void)
{
	return police->ascent + police->descent;
}

static int
pos_en(Note *nt, int x, int y)
{
	int l = (y - MARGE) / haut_ligne(), i, f;

	if (l < 0)
		return 0;
	if (l >= nt->nlignes)
		return (int)strlen(nt->texte);
	f = fin_ligne(nt, l);
	for (i = nt->debuts[l]; i < f; i = suite_car(nt->texte, i))
		if (MARGE + 2 + largeur(nt->texte, nt->debuts[l], suite_car(nt->texte, i)) -
		    (largeur(nt->texte, i, suite_car(nt->texte, i)) / 2) > x)
			return i;
	return f;
}

/* ---------- dessin ---------- */

static void
dessiner(PtkWindow *w, void *user)
{
	Note *nt = user;
	Ptk *p = w->p;
	PtkColor fond = ptk_color(p, couleurs[nt->couleur].hex);
	int l, a = nt->ancre < nt->caret ? nt->ancre : nt->caret;
	int b = nt->ancre < nt->caret ? nt->caret : nt->ancre;

	ptk_fill(p, w->surface, 0, 0, w->w, w->h, fond);
	couper_lignes(nt);
	for (l = 0; l < nt->nlignes; l++) {
		int d = nt->debuts[l], f = fin_ligne(nt, l), y = MARGE + l * haut_ligne();
		char t[MAX_TEXTE];

		if (y > w->h)
			break;
		/* sélection : fond inversé, comme TextEdit de Mac OS 9 */
		if (b > a && w->active && b > d && a <= f) {
			int sa = a > d ? a : d, sb = b < f ? b : f;
			int x0 = MARGE + 2 + largeur(nt->texte, d, sa);
			int x1 = MARGE + 2 + largeur(nt->texte, d, sb) + (b > f && l + 1 < nt->nlignes ? 4 : 0);

			ptk_fill(p, w->surface, x0, y, x1 - x0, haut_ligne(), p->black);
		}
		memcpy(t, nt->texte + d, (size_t)(f - d));
		t[f - d] = '\0';
		ptk_text(p, w->surface, police, MARGE + 2, y + police->ascent, t, p->text);
		if (b > a && w->active && b > d && a <= f) {
			int sa = a > d ? a : d, sb = b < f ? b : f;

			memcpy(t, nt->texte + sa, (size_t)(sb - sa));
			t[sb - sa] = '\0';
			ptk_text(p, w->surface, police, MARGE + 2 + largeur(nt->texte, d, sa),
			    y + police->ascent, t, fond);
		}
	}
	if (w->active && a == b) {
		l = ligne_de(nt, nt->caret);
		{
			int x = MARGE + 2 + largeur(nt->texte, nt->debuts[l], nt->caret);
			int y = MARGE + l * haut_ligne();

			ptk_line(p, w->surface, x, y, x, y + haut_ligne() - 1, p->black);
		}
	}
}

/* ---------- édition ---------- */

static void
effacer_selection(Note *nt)
{
	int a = nt->ancre < nt->caret ? nt->ancre : nt->caret;
	int b = nt->ancre < nt->caret ? nt->caret : nt->ancre;

	if (b > a)
		memmove(nt->texte + a, nt->texte + b, strlen(nt->texte + b) + 1);
	nt->caret = nt->ancre = a;
}

static void
inserer(Note *nt, const char *s)
{
	size_t l, n = strlen(s);

	effacer_selection(nt);
	l = strlen(nt->texte);
	if (l + n >= sizeof(nt->texte)) {
		ptk_beep(&app);
		return;
	}
	memmove(nt->texte + nt->caret + n, nt->texte + nt->caret, l - (size_t)nt->caret + 1);
	memcpy(nt->texte + nt->caret, s, n);
	nt->caret += (int)n;
	nt->ancre = nt->caret;
}

static void
selection(Note *nt, char *out, size_t n)
{
	int a = nt->ancre < nt->caret ? nt->ancre : nt->caret;
	int b = nt->ancre < nt->caret ? nt->caret : nt->ancre;

	snprintf(out, n, "%.*s", b - a, nt->texte + a);
}

/* keysym → UTF-8 (Latin-1 et keysyms Unicode) */
static int
utf8(KeySym ks, char *o)
{
	unsigned long u = 0;

	if ((ks >= 0x20 && ks <= 0x7e) || (ks >= 0xa0 && ks <= 0xff))
		u = ks;
	else if ((ks & 0xff000000UL) == 0x01000000UL)
		u = ks & 0xffffffUL;
	else if (ks == XK_EuroSign)
		u = 0x20ac;
	if (u == 0)
		return 0;
	if (u < 0x80) {
		o[0] = (char)u;
		o[1] = 0;
	} else if (u < 0x800) {
		o[0] = (char)(0xc0 | (u >> 6));
		o[1] = (char)(0x80 | (u & 0x3f));
		o[2] = 0;
	} else {
		o[0] = (char)(0xe0 | (u >> 12));
		o[1] = (char)(0x80 | ((u >> 6) & 0x3f));
		o[2] = (char)(0x80 | (u & 0x3f));
		o[3] = 0;
	}
	return 1;
}

static void
deplacer(Note *nt, int pos, int etendre)
{
	nt->caret = pos;
	if (!etendre)
		nt->ancre = pos;
}

static void
clavier(Note *nt, PtkEvent *e)
{
	XKeyEvent k;
	KeySym ks;
	char buf[16], u[8];
	int maj = (e->state & ShiftMask) != 0, l, x;

	/* le keysym complet (Maj, AltGr, touches mortes composées par X) */
	memset(&k, 0, sizeof(k));
	k.type = KeyPress;
	k.display = app.dpy;
	k.keycode = (unsigned)e->keycode;
	k.state = e->state;
	XLookupString(&k, buf, sizeof(buf), &ks, NULL);
	switch (ks) {
	case XK_BackSpace:
		if (nt->ancre == nt->caret)
			nt->ancre = debut_car(nt->texte, nt->caret);
		effacer_selection(nt);
		break;
	case XK_Delete:
		if (nt->ancre == nt->caret)
			nt->ancre = suite_car(nt->texte, nt->caret);
		effacer_selection(nt);
		break;
	case XK_Left:
		deplacer(nt, !maj && nt->ancre != nt->caret ? (nt->ancre < nt->caret ? nt->ancre :
		    nt->caret) : debut_car(nt->texte, nt->caret), maj);
		break;
	case XK_Right:
		deplacer(nt, !maj && nt->ancre != nt->caret ? (nt->ancre > nt->caret ? nt->ancre :
		    nt->caret) : suite_car(nt->texte, nt->caret), maj);
		break;
	case XK_Up:
	case XK_Down:
		couper_lignes(nt);
		l = ligne_de(nt, nt->caret);
		x = MARGE + 2 + largeur(nt->texte, nt->debuts[l], nt->caret);
		l += ks == XK_Up ? -1 : 1;
		if (l < 0)
			deplacer(nt, 0, maj);
		else if (l >= nt->nlignes)
			deplacer(nt, (int)strlen(nt->texte), maj);
		else
			deplacer(nt, pos_en(nt, x, MARGE + l * haut_ligne() + 1), maj);
		break;
	case XK_Home:
		deplacer(nt, 0, maj);
		break;
	case XK_End:
		deplacer(nt, (int)strlen(nt->texte), maj);
		break;
	case XK_Return:
	case XK_KP_Enter:
		inserer(nt, "\n");
		break;
	default:
		if (utf8(ks, u))
			inserer(nt, u);
		else
			return;
	}
	enregistrer_tout();
}

static Note *
note_de(PtkWindow *w)
{
	int i;

	for (i = 0; i < nnotes; i++)
		if (notes[i]->w == w)
			return notes[i];
	return NULL;
}

static void
evenement(PtkWindow *w, PtkEvent *e, void *user)
{
	Note *nt = user;

	switch (e->type) {
	case PTK_EV_KEY:
		if (e->state & Mod1Mask)	/* ⌘ : raccourci de menu */
			return;
		clavier(nt, e);
		break;
	case PTK_EV_MOUSE_DOWN:
		deplacer(nt, pos_en(nt, e->x, e->y), (e->state & ShiftMask) != 0);
		nt->glisse = 1;
		break;
	case PTK_EV_MOUSE_MOVE:
		if (!nt->glisse)
			return;
		nt->caret = pos_en(nt, e->x, e->y);
		break;
	case PTK_EV_MOUSE_UP:
		nt->glisse = 0;
		return;
	case PTK_EV_RESIZE:
		enregistrer_tout();
		break;
	default:
		return;
	}
	ptk_window_invalidate(w);
}

static void
coller_recu(PtkWindow *w, const char *texte, void *user)
{
	Note *nt = note_de(w);
	char propre[MAX_TEXTE];
	size_t n = 0;

	(void)user;
	if (nt == NULL)
		return;
	for (; *texte && n < sizeof(propre) - 1; texte++)
		if (*texte != '\r')
			propre[n++] = *texte;
	propre[n] = '\0';
	inserer(nt, propre);
	enregistrer_tout();
	ptk_window_invalidate(w);
}

/* ---------- notes ---------- */

static void
menu_note(Note *nt)
{
	char m[1024];
	int i, n;

	n = snprintf(m, sizeof(m),
	    "Fichier|Nouvelle note\t⌘N|-|Exporter le texte…\t⌘E|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V|Effacer|-|Tout sélectionner\t⌘A\n"
	    "Couleur");
	for (i = 0; i < NC; i++)
		n += snprintf(m + n, sizeof(m) - (size_t)n, "|%s%s", i == nt->couleur ? "√ " : "",
		    couleurs[i].nom);
	snprintf(m + n, sizeof(m) - (size_t)n, "\nAide|À propos des Pense-bêtes");
	ptk_window_set_menu(nt->w, m);
}

static void
appliquer_couleur(Note *nt)
{
	char st[32];

	snprintf(st, sizeof(st), "pense-bete:%s", couleurs[nt->couleur].hex);
	XChangeProperty(app.dpy, nt->w->win, XInternAtom(app.dpy, "_PLATINE_STYLE", False),
	    XInternAtom(app.dpy, "UTF8_STRING", False), 8, PropModeReplace,
	    (unsigned char *)st, (int)strlen(st));
	menu_note(nt);
}

static int
exporter(Note *nt)
{
	char ch[1024], nom[64];
	FILE *f;
	int i;

	/* nom proposé : le début de la note */
	for (i = 0; nt->texte[i] && nt->texte[i] != '\n' && i < 30; i++)
		nom[i] = nt->texte[i] == '/' ? '-' : nt->texte[i];
	nom[i] = '\0';
	if (!ptk_enregistrer(&app, "Enregistrer le texte de la note sous :",
	    nom[0] ? nom : "Pense-bête", NULL, ch, sizeof(ch)))
		return 0;
	if ((f = fopen(ch, "w")) == NULL || fputs(nt->texte, f) < 0 || fclose(f) != 0) {
		ptk_alert(nt->w, "Impossible d’enregistrer le texte",
		    "Vérifiez les permissions du dossier.", "OK");
		return 0;
	}
	return 1;
}

static void
retirer(Note *nt)
{
	int i;

	for (i = 0; i < nnotes; i++)
		if (notes[i] == nt) {
			memmove(notes + i, notes + i + 1, (size_t)(nnotes - i - 1) * sizeof(notes[0]));
			nnotes--;
			break;
		}
}

/* Stickies : « Enregistrer cette note dans un fichier avant de la fermer ? » */
static int
fermer(PtkWindow *w, void *user)
{
	Note *nt = user;
	int choix;

	(void)w;
	/* Quitter : rien n'est supprimé, tout est enregistré tel quel */
	if (app.en_sortie) {
		if (!fermeture_globale)
			enregistrer_tout();
		fermeture_globale = 1;
		return 1;
	}
	if (nt->texte[0] != '\0') {
		choix = ptk_alert(nt->w, "Enregistrer cette note dans un fichier avant de la fermer ?",
		    "Une note fermée est supprimée.", "Ne pas enregistrer|Annuler|Enregistrer");
		if (choix == 1 || (choix == 2 && !exporter(nt)))
			return 0;
	}
	retirer(nt);
	enregistrer_tout();
	free(nt);
	return 1;
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	Note *nt = user;
	char t[MAX_TEXTE];
	int i;

	if (strncmp(cmd, "√ ", 4) == 0)
		cmd += 4;
	for (i = 0; i < NC; i++)
		if (strcmp(cmd, couleurs[i].nom) == 0) {
			nt->couleur = i;
			appliquer_couleur(nt);
			enregistrer_tout();
			ptk_window_invalidate(w);
			return;
		}
	if (strcmp(cmd, "Nouvelle note") == 0) {
		int x, y;

		position(nt, &x, &y);
		nouvelle_note(x + 20, y + 20, 200, 120, nt->couleur, "");
		enregistrer_tout();
	} else if (strcmp(cmd, "Exporter le texte…") == 0) {
		exporter(nt);
	} else if (strcmp(cmd, "Copier") == 0 || strcmp(cmd, "Couper") == 0) {
		selection(nt, t, sizeof(t));
		if (t[0] == '\0')
			return;
		ptk_copier(w, t);
		if (cmd[1] == 'o' && cmd[2] == 'u') {	/* Couper */
			effacer_selection(nt);
			enregistrer_tout();
		}
	} else if (strcmp(cmd, "Coller") == 0) {
		ptk_coller(w, coller_recu, NULL);
	} else if (strcmp(cmd, "Effacer") == 0) {
		effacer_selection(nt);
		enregistrer_tout();
	} else if (strcmp(cmd, "Tout sélectionner") == 0) {
		nt->ancre = 0;
		nt->caret = (int)strlen(nt->texte);
	} else if (strcmp(cmd, "À propos des Pense-bêtes") == 0) {
		ptk_alert(w, "Pense-bêtes",
		    "Des notes toujours à portée de main. Elles sont enregistrées "
		    "à chaque modification et rouvertes au lancement.", "OK");
	}
	ptk_window_invalidate(w);
}

static Note *
nouvelle_note(int x, int y, int w, int h, int couleur, const char *texte)
{
	Note *nt;
	XSizeHints hints;

	if (nnotes >= MAX_NOTES || (nt = calloc(1, sizeof(*nt))) == NULL)
		return NULL;
	snprintf(nt->texte, sizeof(nt->texte), "%s", texte);
	nt->caret = nt->ancre = (int)strlen(nt->texte);
	nt->couleur = couleur;
	if (w < 60)
		w = 200;
	if (h < 30)
		h = 120;
	nt->w = ptk_window_new(&app, "Pense-bête", x, y, w, h);
	if (nt->w == NULL) {
		free(nt);
		return NULL;
	}
	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | USPosition | PPosition;
	hints.min_width = 60;
	hints.min_height = 30;
	hints.x = x;
	hints.y = y;
	XSetWMNormalHints(app.dpy, nt->w->win, &hints);
	ptk_window_set_document(nt->w, NULL);
	appliquer_couleur(nt);
	ptk_window_set_draw(nt->w, dessiner, nt);
	ptk_window_set_event(nt->w, evenement, nt);
	ptk_window_set_command(nt->w, commande, nt);
	ptk_window_set_close(nt->w, fermer, nt);
	notes[nnotes++] = nt;
	ptk_window_show(nt->w);
	return nt;
}

int
main(void)
{
	char dossier[1024];
	const char *h = getenv("HOME");

	if (ptk_app_init(&app, "Pense-bêtes", "app-pense-betes") != 0)
		return 1;
	app.quit_on_last_close = 1;
	police = ptk_font(&app.ptk, "Geneva:pixelsize=12", "sans:pixelsize=12");
	if (police == NULL)
		police = app.ptk.font_menu;
	snprintf(dossier, sizeof(dossier), "%s/.config/platine", h ? h : ".");
	mkdir(dossier, 0755);
	if (charger_tout() <= 0) {
		nouvelle_note(430, 60, 250, 120, 0,
		    "Bienvenue dans les Pense-bêtes !\n\n"
		    "Tapez ici vos notes. Fichier, Nouvelle note, en ouvre une autre ; "
		    "le menu Couleur change la couleur de la note.");
		nouvelle_note(470, 200, 220, 90, 1,
		    "Les notes sont enregistrées toutes seules et reviennent au prochain lancement.");
		enregistrer_tout();
	}
	ptk_app_run(&app);
	return 0;
}
