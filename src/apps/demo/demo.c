/*
 * platine-demo — démonstration de la Toolbox applicative Platine.
 *
 * Reproduit les contrôles Mac OS 9.2 mesurés sur la VM : boutons arrondis,
 * bouton par défaut, cases 12×12, boutons radio, champs texte avec anneau
 * de focus, menus déroulants et barre de progression.
 *
 * La fenêtre principale (dont run-toolbox.sh connaît les coordonnées) montre
 * les contrôles courants ; Démo ▸ Autres contrôles… ouvre une fenêtre à onglets :
 * curseurs et groupes, boutons biseautés et états inactifs, animations (une copie
 * simulée façon Finder : préparation au pôle de barbier, puis progression).
 * Fichier ▸ Ouvrir… et Choisir un dossier… essaient les zones de dialogue
 * Standard File.
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../../toolbox/ptk_app.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Démo Toolbox", "pdmo", "application", PTK_VERSION, "");


typedef struct {
	PtkApp app;
	PtkWindow *win;
	PtkControl *name;
	PtkControl *show;
	PtkControl *radio_icons;
	PtkControl *radio_list;
	PtkControl *check;
	PtkControl *progress;
	PtkControl *status;
	PtkControl *taille;
	/* fenêtre « Autres contrôles » */
	PtkWindow *autres;
	PtkControl *onglets, *volume_val, *vitesse_val;
	PtkControl *copie_etat, *copie_barbier, *copie_barre, *copie_bouton;
	PtkControl *recherche, *figer;
	int copie;		/* 0 arrêtée, 1 préparation (barbier), 2 copie (progression) */
	double copie_t0;
} Demo;

static double
maintenant(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
update_status(Demo *d, const char *text)
{
	if (d->status != NULL) {
		/* journal pour les tests : statut sur la sortie standard */
		printf("statut : %s\n", text);
		fflush(stdout);
		ptk_control_set_text(d->status, text);
		ptk_window_invalidate(d->win);
	}
}

static void
ok_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	(void)c;
	ptk_alert(w, "Toolbox Platine",
	    "Fenêtres, contrôles, menus et alertes sont gérés par la Toolbox.",
	    "Annuler|OK");
	update_status(d, "Alerte affichée.");
}

static void
cancel_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	(void)w;
	(void)c;
	update_status(d, "Annulé.");
}

static void
check_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	(void)w;
	ptk_control_set_value(d->progress, c->checked ? 100 : 60);
	update_status(d, c->checked ? "Case cochée." : "Case décochée.");
}

static void
radio_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[128];
	(void)w;
	snprintf(buf, sizeof(buf), "%s : %s", c->group ? "Mode" : "Vue",
	    c->label);
	update_status(d, buf);
}

static void
liste_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[128];
	(void)w;
	snprintf(buf, sizeof(buf), "Ligne %d sur 8", c->value + 1);
	update_status(d, buf);
}

static void
defilement_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[128];
	(void)w;
	snprintf(buf, sizeof(buf), "Position %d sur 5", c->value);
	update_status(d, buf);
}

/* petites flèches : la taille suit, comme Apparence > Polices */
static void
taille_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[16];

	(void)w;
	snprintf(buf, sizeof(buf), "%d", c->value);
	ptk_control_set_text(d->taille, buf);
	snprintf(buf, sizeof(buf), "Taille : %d", c->value);
	update_status(d, buf);
}

static void
details_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	update_status(user, c->checked ? "Détails affichés." : "Détails masqués.");
}

static void
field_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[256];
	(void)w;
	snprintf(buf, sizeof(buf), "Nom : %s", c->text);
	update_status(d, buf);
}

static void
popup_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[256];
	(void)w;
	snprintf(buf, sizeof(buf), "Réglage : %s", c->text);
	ptk_control_set_value(d->progress, 80);
	update_status(d, buf);
}

/* --- fenêtre « Autres contrôles » --- */

static void command_window(PtkWindow *w, const char *cmd, void *user);

static void
curseur_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;
	char buf[64];

	if (c->checked) {
		snprintf(buf, sizeof(buf), "%d", c->value);
		ptk_control_set_text(d->volume_val, buf);
	} else {
		snprintf(buf, sizeof(buf), "%d", c->value);
		ptk_control_set_text(d->vitesse_val, buf);
	}
	ptk_window_invalidate(w);
}

/* contenu des boutons biseautés : un motif simple, inversé quand le bouton est enfoncé */
static void
biseau_contenu(PtkWindow *w, PtkControl *c, int x, int y, int W, int H, int presse)
{
	Ptk *p = w->p;
	PtkColor n = p->inactif ? ptk_color(p, "#888888") : presse ? p->white : p->black;
	PtkColor f = p->inactif ? p->face : ptk_color(p, "#ccccff");
	int cx = x + W / 2, cy = y + H / 2, i;

	switch (c->value) {
	case 0:	/* document */
		ptk_fill(p, w->surface, cx - 8, cy - 11, 16, 22, p->inactif ? p->face : p->white);
		ptk_rect(p, w->surface, cx - 8, cy - 11, 16, 22, n);
		for (i = 0; i < 4; i++)
			ptk_line(p, w->surface, cx - 5, cy - 6 + 4 * i, cx + 4, cy - 6 + 4 * i, n);
		break;
	case 1:	/* dossier */
		ptk_fill(p, w->surface, cx - 12, cy - 6, 24, 16, f);
		ptk_rect(p, w->surface, cx - 12, cy - 6, 24, 16, n);
		ptk_fill(p, w->surface, cx - 12, cy - 9, 9, 3, f);
		ptk_rect(p, w->surface, cx - 12, cy - 9, 9, 4, n);
		break;
	default:	/* écran */
		ptk_rect(p, w->surface, cx - 13, cy - 10, 26, 18, n);
		ptk_fill(p, w->surface, cx - 11, cy - 8, 22, 14, f);
		ptk_line(p, w->surface, cx - 6, cy + 11, cx + 5, cy + 11, n);
		ptk_line(p, w->surface, cx, cy + 8, cx, cy + 11, n);
		break;
	}
}

static void
biseau_action(PtkWindow *w, PtkControl *c, void *user)
{
	static const char *noms[] = { "Document", "Dossier", "Écran" };

	(void)w;
	update_status(user, noms[c->value % 3]);
}

/* la copie simulée ne montre que son contrôle de l'étape en cours (l'onglet
 * Animation doit être ouvert) */
static void
copie_afficher(Demo *d)
{
	int page = d->onglets->value == 2;

	d->copie_barbier->visible = page && d->copie == 1;
	d->copie_barre->visible = page && d->copie != 1;
	ptk_control_set_text(d->copie_bouton, d->copie ? "Arrêter" : "Démarrer");
	ptk_window_invalidate(d->autres);
}

static void
copie_tic(PtkApp *app, void *user)
{
	Demo *d = user;
	double t = maintenant() - d->copie_t0;
	char buf[96];

	(void)app;
	if (d->copie == 1 && t >= 1.8) {
		/* préparation terminée : la barre déterminée prend le relais */
		d->copie = 2;
		d->copie_t0 = maintenant();
		printf("copie : progression\n");
		fflush(stdout);
		ptk_control_set_value(d->copie_barre, 0);
		copie_afficher(d);
		return;
	}
	if (d->copie == 2) {
		int v = (int)(t / 4.0 * 100);

		if (v >= 100) {
			d->copie = 0;
			d->app.tic_periode = 0;
			ptk_control_set_value(d->copie_barre, 100);
			ptk_control_set_text(d->copie_etat, "Copie terminée.");
			printf("copie : terminée\n");
			fflush(stdout);
			copie_afficher(d);
			return;
		}
		ptk_control_set_value(d->copie_barre, v);
		snprintf(buf, sizeof(buf), "Éléments restants à copier : %d", 48 - 48 * v / 100);
		ptk_control_set_text(d->copie_etat, buf);
		ptk_window_invalidate(d->autres);
	}
}

static void
copie_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;

	(void)w;
	(void)c;
	if (d->copie) {
		d->copie = 0;
		d->app.tic_periode = 0;
		ptk_control_set_text(d->copie_etat, "Copie arrêtée.");
	} else {
		d->copie = 1;
		d->copie_t0 = maintenant();
		ptk_control_set_text(d->copie_etat, "Préparation de la copie…");
		printf("copie : préparation\n");
		fflush(stdout);
		d->app.tic = copie_tic;
		d->app.tic_user = d;
		d->app.tic_periode = 0.05;
		d->app.tic_t = 0;
	}
	copie_afficher(d);
}

/* figer : les contrôles animés désactivés prennent l'apparence inactive et s'arrêtent */
static void
figer_action(PtkWindow *w, PtkControl *c, void *user)
{
	Demo *d = user;

	(void)w;
	d->copie_barbier->enabled = !c->checked;
	d->recherche->enabled = !c->checked;
	ptk_window_invalidate(d->autres);
}

static void
onglets_action(PtkWindow *w, PtkControl *c, void *user)
{
	(void)w;
	(void)c;
	copie_afficher(user);
}

static int
autres_fermer(PtkWindow *w, void *user)
{
	Demo *d = user;

	(void)w;
	d->copie = 0;
	d->app.tic_periode = 0;
	d->autres = NULL;
	return 1;
}

static void
autres_dessin(PtkWindow *w, void *user)
{
	(void)user;
	ptk_fill(w->p, w->surface, 0, 0, w->w, w->h, w->p->face);
}

static PtkControl *
sur_page(PtkControl *c, int page, PtkControl *onglets)
{
	ptk_control_set_page(c, page);
	c->visible = page == onglets->value;
	return c;
}

static void
autres_ouvrir(Demo *d)
{
	PtkWindow *w;
	PtkControl *o, *c;
	int i;

	if (d->autres != NULL) {
		XRaiseWindow(d->app.dpy, d->autres->win);
		return;
	}
	w = d->autres = ptk_window_new(&d->app, "Autres contrôles", 120, 90, 440, 300);
	if (w == NULL)
		return;
	ptk_window_set_menu(w, "Fichier|Fermer\t⌘W|Quitter\t⌘Q\nAide|À propos de la Toolbox");
	ptk_window_set_draw(w, autres_dessin, d);
	ptk_window_set_command(w, command_window, d);
	ptk_window_set_close(w, autres_fermer, d);

	o = d->onglets = ptk_control_add(w, PTK_ONGLETS, 10, 10, 420, 280, "");
	ptk_control_set_items(o, "Curseurs|Boutons|Animation");
	o->page = -1;
	ptk_control_set_action(o, onglets_action, d);

	/* page 0 : curseurs dans leurs groupes */
	sur_page(ptk_control_add(w, PTK_GROUPE, 24, 46, 392, 76, "Volume"), 0, o);
	c = sur_page(ptk_control_add(w, PTK_CURSEUR, 40, 76, 300, 20, ""), 0, o);
	c->min = 0, c->max = 7, c->value = 4, c->checked = 1;	/* plat, sans graduations */
	ptk_control_set_action(c, curseur_action, d);
	d->volume_val = sur_page(ptk_control_add(w, PTK_STATIC, 356, 79, 40, 16, "4"), 0, o);
	sur_page(ptk_control_add(w, PTK_GROUPE, 24, 134, 392, 92, "Vitesse du pointeur"), 0, o);
	c = sur_page(ptk_control_add(w, PTK_CURSEUR, 40, 164, 300, 20, ""), 0, o);
	c->min = 0, c->max = 8, c->value = 3;	/* gradué, un cran par valeur */
	ptk_control_set_action(c, curseur_action, d);
	d->vitesse_val = sur_page(ptk_control_add(w, PTK_STATIC, 356, 167, 40, 16, "3"), 0, o);
	sur_page(ptk_control_add(w, PTK_STATIC, 40, 196, 60, 16, "Lente"), 0, o);
	sur_page(ptk_control_add(w, PTK_STATIC, 300, 196, 60, 16, "Rapide"), 0, o);
	sur_page(ptk_control_add(w, PTK_SEPARATEUR, 24, 240, 392, 2, ""), 0, o);
	sur_page(ptk_control_add(w, PTK_STATIC, 24, 252, 392, 16,
	    "Le curseur plat suit les volumes ; le gradué, la souris."), 0, o);

	/* page 1 : boutons biseautés, et chaque contrôle actif puis inactif */
	for (i = 0; i < 4; i++) {
		c = sur_page(ptk_control_add(w, PTK_BISEAU, 32 + 80 * i, 48, 64, 44, ""), 1, o);
		c->value = i % 3;
		c->contenu = biseau_contenu;
		if (i == 3)
			c->enabled = 0;
		else
			ptk_control_set_action(c, biseau_action, d);
	}
	sur_page(ptk_control_add(w, PTK_BUTTON, 32, 110, 100, 20, "Actif"), 1, o);
	c = sur_page(ptk_control_add(w, PTK_BUTTON, 150, 110, 100, 20, "Inactif"), 1, o);
	c->enabled = 0;
	c = sur_page(ptk_control_add(w, PTK_CHECKBOX, 32, 146, 180, 14, "Case active"), 1, o);
	c->checked = 1;
	c = sur_page(ptk_control_add(w, PTK_CHECKBOX, 222, 146, 180, 14, "Case inactive"), 1, o);
	c->checked = 1, c->enabled = 0;
	c = sur_page(ptk_control_add(w, PTK_RADIO, 32, 170, 180, 14, "Radio active"), 1, o);
	ptk_control_set_group(c, 5);
	c->checked = 1;
	c = sur_page(ptk_control_add(w, PTK_RADIO, 222, 170, 180, 14, "Radio inactive"), 1, o);
	ptk_control_set_group(c, 6);
	c->checked = 1, c->enabled = 0;
	c = sur_page(ptk_control_add(w, PTK_POPUP, 32, 196, 170, 20, ""), 1, o);
	ptk_control_set_items(c, "Menu local actif|Autre choix");
	ptk_control_set_text(c, "Menu local actif");
	c = sur_page(ptk_control_add(w, PTK_POPUP, 222, 196, 170, 20, ""), 1, o);
	ptk_control_set_items(c, "Menu local inactif");
	ptk_control_set_text(c, "Menu local inactif");
	c->enabled = 0;
	c = sur_page(ptk_control_add(w, PTK_TEXTFIELD, 32, 230, 170, 22, ""), 1, o);
	ptk_control_set_text(c, "Champ actif");
	c = sur_page(ptk_control_add(w, PTK_TEXTFIELD, 222, 230, 170, 22, ""), 1, o);
	ptk_control_set_text(c, "Champ inactif");
	c->enabled = 0;

	/* page 2 : une copie simulée comme celle du Finder, et les flèches de recherche */
	d->copie_etat = sur_page(ptk_control_add(w, PTK_STATIC, 24, 46, 392, 16,
	    "Copie de « Documents » vers « Sauvegarde »"), 2, o);
	d->copie_barbier = sur_page(ptk_control_add(w, PTK_BARBIER, 24, 70, 392, 14, ""), 2, o);
	d->copie_barre = sur_page(ptk_control_add(w, PTK_PROGRESS, 24, 70, 392, 14, ""), 2, o);
	d->copie_bouton = sur_page(ptk_control_add(w, PTK_BUTTON, 316, 98, 100, 20, "Démarrer"), 2, o);
	ptk_control_set_action(d->copie_bouton, copie_action, d);
	sur_page(ptk_control_add(w, PTK_SEPARATEUR, 24, 134, 392, 2, ""), 2, o);
	sur_page(ptk_control_add(w, PTK_STATIC, 24, 150, 300, 16, "Recherche en cours :"), 2, o);
	d->recherche = sur_page(ptk_control_add(w, PTK_RECHERCHE, 24, 172, 92, 16, ""), 2, o);
	sur_page(ptk_control_add(w, PTK_BARBIER, 128, 173, 288, 14, ""), 2, o);
	d->figer = sur_page(ptk_control_add(w, PTK_CHECKBOX, 24, 210, 300, 14,
	    "Figer les animations (contrôles inactifs)"), 2, o);
	ptk_control_set_action(d->figer, figer_action, d);
	sur_page(ptk_control_add(w, PTK_STATIC, 24, 240, 392, 16,
	    "Une fenêtre en arrière-plan fige aussi ses animations."), 2, o);

	ptk_onglets_afficher(w, o);
	copie_afficher(d);
	ptk_window_show(w);
}

static void
draw_window(PtkWindow *w, void *user)
{
	Ptk *p = w->p;
	(void)user;

	/* fond de dialogue */
	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);
	/* plaque de titre interne, comme les dialogues Mac OS 9 */
	ptk_text(p, w->surface, p->font_title, 20, 28,
	    "Toolbox Platine", p->text);
	ptk_text(p, w->surface, p->font_menu, 20, 46,
	    "Contrôles Mac OS 9.2 — mesures VM", p->text);
	p->inactif = !w->active;
	ptk_separateur(p, w->surface, 20, 54, w->w - 40, 2);
	p->inactif = 0;
}

static void
command_window(PtkWindow *w, const char *cmd, void *user)
{
	Demo *d = user;

	if (strcmp(cmd, "Quitter") == 0) {
		ptk_window_close(w);
		ptk_app_quit(&d->app);
	} else if (strcmp(cmd, "Autres contrôles…") == 0) {
		autres_ouvrir(d);
	} else if (strcmp(cmd, "Ouvrir…") == 0) {
		/* zone de dialogue d'ouverture (StandardGetFile), tous les documents */
		char ch[4096], t[4200];

		if (ptk_ouvrir(&d->app, NULL, NULL, ch, sizeof(ch))) {
			snprintf(t, sizeof(t), "Ouvert : %s", ch);
			update_status(d, t);
		} else
			update_status(d, "Ouverture annulée.");
	} else if (strcmp(cmd, "Choisir un dossier…") == 0) {
		char ch[4096], t[4200];

		if (ptk_choisir_dossier(&d->app, "Choisissez un dossier :", NULL, ch, sizeof(ch))) {
			snprintf(t, sizeof(t), "Dossier : %s", ch);
			update_status(d, t);
		} else
			update_status(d, "Choix annulé.");
	} else if (strcmp(cmd, "Enregistrer…") == 0) {
		/* zone de dialogue d'enregistrement de la Toolbox (Standard File) */
		char ch[4096];

		if (ptk_enregistrer(&d->app, "Enregistrer le document sous :",
		    "Démo sans titre", NULL, ch, sizeof(ch))) {
			char t[4200];

			snprintf(t, sizeof(t), "Enregistré : %s", ch);
			update_status(d, t);
		} else
			update_status(d, "Enregistrement annulé.");
	} else if (strcmp(cmd, "À propos") == 0 ||
	    strcmp(cmd, "À propos de la Toolbox") == 0) {
		ptk_alert(w, "À propos de la Toolbox",
		    "Platine — fenêtres, menus, contrôles, événements.",
		    "OK");
	} else {
		update_status(d, cmd);
	}
}

int
main(void)
{
	Demo d;
	PtkControl *b, *ok;

	memset(&d, 0, sizeof(d));
	if (ptk_app_init(&d.app, "Démo Toolbox", "app") != 0)
		return 1;
	d.win = ptk_window_new(&d.app, "Démo Toolbox", 60, 70, 560, 380);
	if (d.win == NULL)
		return 1;
	ptk_window_set_menu(d.win,
	    "Fichier|Nouveau\t⌘N|Ouvrir…\t⌘O|Choisir un dossier…|Enregistrer…\t⌘S|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V\n"
	    "Démo|Autres contrôles…\t⌘K\n"
	    "Aide|À propos de la Toolbox");
	ptk_window_set_draw(d.win, draw_window, &d);
	ptk_window_set_command(d.win, command_window, &d);

	/* ligne 1 : icône + nom */
	d.name = ptk_control_add(d.win, PTK_TEXTFIELD, 92, 66, 260, 22, "");
	ptk_control_set_text(d.name, "Sans titre");
	ptk_control_set_action(d.name, field_action, &d);

	/* ligne Show */
	ptk_control_add(d.win, PTK_STATIC, 20, 100, 60, 20, "Afficher :");
	d.show = ptk_control_add(d.win, PTK_POPUP, 92, 100, 190, 20, "");
	ptk_control_set_items(d.show,
	    "Informations générales|Mémoire|Partage|Application");
	ptk_control_set_text(d.show, "Informations générales");
	ptk_control_set_action(d.show, popup_action, &d);

	/* boutons radio */
	d.radio_icons = ptk_control_add(d.win, PTK_RADIO, 20, 134, 150, 14,
	    "Icônes");
	ptk_control_set_action(d.radio_icons, radio_action, &d);
	d.radio_list = ptk_control_add(d.win, PTK_RADIO, 180, 134, 150, 14,
	    "Liste");
	ptk_control_set_action(d.radio_list, radio_action, &d);
	d.radio_icons->checked = 1;
	b = ptk_control_add(d.win, PTK_RADIO, 340, 134, 70, 14, "Auto");
	ptk_control_set_group(b, 1);
	ptk_control_set_action(b, radio_action, &d);
	b->checked = 1;
	b = ptk_control_add(d.win, PTK_RADIO, 420, 134, 70, 14, "Manuel");
	ptk_control_set_group(b, 1);
	ptk_control_set_action(b, radio_action, &d);

	/* champ à petites flèches (Apparence > Polices > Taille) */
	ptk_control_add(d.win, PTK_STATIC, 312, 100, 50, 20, "Taille :");
	d.taille = ptk_control_add(d.win, PTK_TEXTFIELD, 368, 99, 36, 22, "");
	ptk_control_set_text(d.taille, "12");
	b = ptk_control_add(d.win, PTK_PETITES_FLECHES, 410, 100, 0, 0, "");
	b->min = 9;
	b->max = 24;
	b->value = 12;
	ptk_control_set_action(b, taille_action, &d);

	/* triangle de détail */
	b = ptk_control_add(d.win, PTK_TRIANGLE, 300, 158, 180, 14,
	    "Afficher les détails");
	ptk_control_set_action(b, details_action, &d);

	/* cases à cocher */
	d.check = ptk_control_add(d.win, PTK_CHECKBOX, 20, 158, 260, 14,
	    "Toujours au premier plan");
	ptk_control_set_action(d.check, check_action, &d);

	/* barre de progression */
	ptk_control_add(d.win, PTK_STATIC, 20, 186, 80, 16, "Progression :");
	d.progress = ptk_control_add(d.win, PTK_PROGRESS, 20, 202, 300, 14,
	    "");
	ptk_control_set_value(d.progress, 60);
	b = ptk_control_add(d.win, PTK_BUTTON, 360, 202, 110, 20,
	    "Indisponible");
	b->enabled = 0;

	/* progression indéterminée et flèches de recherche, qui s'animent
	 * d'eux-mêmes (pôle de barbier de Mac OS 9, chevrons de Sherlock) */
	ptk_control_add(d.win, PTK_STATIC, 462, 100, 80, 16, "Animation :");
	b = ptk_control_add(d.win, PTK_BARBIER, 462, 124, 88, 14, "");
	b = ptk_control_add(d.win, PTK_RECHERCHE, 462, 148, 88, 16, "");

	/* statut */
	d.status = ptk_control_add(d.win, PTK_STATIC, 20, 232, 440, 16,
	    "Prêt.");
	ptk_control_set_text(d.status, "Prêt.");

	b = ptk_control_add(d.win, PTK_LISTE, 20, 254, 200, 70, "");
	ptk_control_set_items(b, "Un|Deux|Trois|Quatre|Cinq|Six|Sept|Huit");
	ptk_control_set_action(b, liste_action, &d);
	b = ptk_control_add(d.win, PTK_TEXTAREA, 240, 254, 200, 70, "");
	ptk_control_set_text(b, "Un\nDeux\nTrois");
	b = ptk_control_add(d.win, PTK_SCROLLBAR, 223, 254, 16, 70, "");
	b->max = 5;
	b->defil = 3;
	ptk_control_set_action(b, defilement_action, &d);
	b = ptk_control_add(d.win, PTK_SCROLLBAR, 20, 326, 200, 16, "");
	b->max = 5;
	b->defil = 3;
	b->checked = 1;
	ptk_control_set_action(b, defilement_action, &d);

	/* boutons */
	b = ptk_control_add(d.win, PTK_BUTTON, 250, 332, 90, 20, "Annuler");
	ptk_control_set_action(b, cancel_action, &d);
	ok = ptk_control_add(d.win, PTK_DEFAULT_BUTTON, 352, 332, 90, 20,
	    "OK");
	ptk_control_set_action(ok, ok_action, &d);

	ptk_window_show(d.win);
	ptk_app_run(&d.app);
	return 0;
}
