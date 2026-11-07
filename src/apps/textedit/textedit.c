/* TextEdit : édition de fichiers texte simples, UTF-8, sans mise en forme.
 * L'Édition (Couper, Copier, Coller, Tout sélectionner) vient de la Toolbox ;
 * l'alerte de fermeture suit Mac OS 9 : Ne pas enregistrer | Annuler | Enregistrer. */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../toolbox/ptk_app.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("TextEdit", "ttxt", "app-texte", PTK_VERSION, "txt md c h sh conf log");


typedef struct {
	PtkApp app;
	PtkWindow *win;
	PtkControl *texte;
	char path[PATH_MAX];
	char sauve[PTK_TEXT_MAX];	/* contenu tel qu'enregistré ou chargé */
} TextEdit;

static const char *
base_name(const char *path)
{
	const char *s = strrchr(path, '/');

	return s != NULL ? s + 1 : path;
}

static void
titre(TextEdit *t)
{
	ptk_window_set_title(t->win, t->path[0] != '\0' ? base_name(t->path) :
	    "Sans titre");
	ptk_window_set_document(t->win, t->path[0] != '\0' ? t->path : NULL);
}

static int
enregistrer(TextEdit *t)
{
	FILE *f;
	size_t n = strlen(t->texte->text);
	const char *home;

	if (t->path[0] == '\0') {
		home = getenv("HOME");
		if (home == NULL || snprintf(t->path, sizeof(t->path),
		    "%s/Sans titre.txt", home) >= (int)sizeof(t->path))
			return -1;
	}
	f = fopen(t->path, "w");
	if (f == NULL)
		return -1;
	if (n > 0 && fwrite(t->texte->text, 1, n, f) != n) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	snprintf(t->sauve, sizeof(t->sauve), "%s", t->texte->text);
	titre(t);
	return 0;
}

static int
charger(TextEdit *t, const char *path)
{
	FILE *f;
	long taille;
	size_t n;

	if (snprintf(t->path, sizeof(t->path), "%s", path) >= (int)sizeof(t->path))
		return -1;
	f = fopen(path, "r");
	if (f == NULL)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (taille = ftell(f)) < 0 ||
	    taille >= PTK_TEXT_MAX || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	n = fread(t->texte->text, 1, (size_t)taille, f);
	if (ferror(f) || memchr(t->texte->text, '\0', n) != NULL) {
		fclose(f);
		return -1;
	}
	t->texte->text[n] = '\0';
	fclose(f);
	t->texte->caret = (int)n;
	snprintf(t->sauve, sizeof(t->sauve), "%s", t->texte->text);
	titre(t);
	ptk_window_invalidate(t->win);
	return 0;
}

/* retour 1 : on peut continuer (modifications enregistrées ou abandonnées) ;
 * 0 : Annuler, la fenêtre reste ouverte */
static int
confirmer(TextEdit *t)
{
	int choix;

	if (strcmp(t->texte->text, t->sauve) == 0)
		return 1;
	choix = ptk_alert(t->win, "Voulez-vous enregistrer les modifications ?",
	    "Les modifications apportées au document seront perdues si vous "
	    "ne les enregistrez pas.",
	    "Ne pas enregistrer|Annuler|Enregistrer");
	if (choix == 2 && enregistrer(t) != 0) {
		ptk_alert(t->win, "Impossible d’enregistrer le document",
		    "Vérifiez le nom du fichier et les permissions du dossier.", "OK");
		return 0;
	}
	return choix != 1;
}

static int
fermer(PtkWindow *w, void *user)
{
	(void)w;
	return confirmer(user);
}

static void
vider(TextEdit *t)
{
	t->path[0] = '\0';
	t->sauve[0] = '\0';
	ptk_control_set_text(t->texte, "");
	t->texte->caret = 0;
	titre(t);
}

static void
evenement(PtkWindow *w, PtkEvent *e, void *user)
{
	(void)user;
	if (e->type == PTK_EV_RESIZE) {
		w->controls->w = w->w;
		w->controls->h = w->h;
		ptk_window_invalidate(w);
	}
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	TextEdit *t = user;

	(void)w;
	if (strcmp(cmd, "Nouveau") == 0) {
		if (confirmer(t))
			vider(t);
	} else if (strcmp(cmd, "Enregistrer") == 0) {
		if (enregistrer(t) != 0)
			ptk_alert(t->win, "Impossible d’enregistrer le document",
			    "Vérifiez le nom du fichier et les permissions du dossier.", "OK");
	} else if (strcmp(cmd, "À propos de TextEdit") == 0) {
		ptk_alert(t->win, "TextEdit",
		    "Éditeur de texte simple de Platine. Documents UTF-8, 16 Kio maximum.",
		    "OK");
	}
}

int
main(int argc, char **argv)
{
	TextEdit t;

	memset(&t, 0, sizeof(t));
	if (argc > 2 || ptk_app_init(&t.app, "TextEdit", "document-texte") != 0)
		return 1;
	t.win = ptk_window_new(&t.app, "Sans titre", 60, 60, 480, 340);
	if (t.win == NULL)
		return 1;
	ptk_window_set_document(t.win, NULL);
	ptk_window_set_menu(t.win,
	    "Fichier|Nouveau\t⌘N|-|Enregistrer\t⌘S|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|Couper\t⌘X|Copier\t⌘C|Coller\t⌘V|Tout sélectionner\t⌘A\n"
	    "Aide|À propos de TextEdit");
	t.texte = ptk_control_add(t.win, PTK_TEXTAREA, 0, 0, t.win->w, t.win->h, "");
	t.win->focus = t.texte;
	t.texte->focused = 1;
	t.texte->caret_on = 1;
	ptk_window_set_event(t.win, evenement, &t);
	ptk_window_set_command(t.win, commande, &t);
	ptk_window_set_close(t.win, fermer, &t);
	if (argc == 2 && charger(&t, argv[1]) != 0) {
		ptk_alert(t.win, "Impossible d’ouvrir le document",
		    "Le fichier doit être un texte de moins de 16 Kio, sans octet nul.", "OK");
		vider(&t);
	}
	ptk_window_show(t.win);
	ptk_app_run(&t.app);
	return 0;
}
