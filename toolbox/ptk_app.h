/*
 * ptk_app.h — Toolbox applicative Platine.
 *
 * API de haut niveau pour écrire une application Mac OS 9-like :
 * fenêtres, contrôles, menus, événements, alertes. Le WM garde les cadres,
 * la barre de menus et le focus ; le Toolbox s'occupe du contenu.
 */
#ifndef PTK_APP_H
#define PTK_APP_H

#include "ptk.h"

typedef struct PtkApp PtkApp;
typedef struct PtkWindow PtkWindow;
typedef struct PtkControl PtkControl;

#define PTK_TEXT_MAX 16384

enum {
	PTK_BUTTON,
	PTK_DEFAULT_BUTTON,
	PTK_CHECKBOX,
	PTK_RADIO,
	PTK_TEXTFIELD,
	PTK_STATIC,
	PTK_PROGRESS,
	PTK_POPUP,
	PTK_TEXTAREA,
	PTK_GROUPE,	/* cadre gravé et son titre (label) */
	PTK_CURSEUR,	/* value entre min et max ; checked : curseur plat sans graduations (volume) */
	PTK_ONGLETS,	/* items « A|B|C », value = onglet ; le rectangle couvre tout le panneau */
	PTK_LISTE,	/* items « Nom\tType|… » ; label « Colonne\tColonne » : en-têtes ; value = ligne ;
		 * « \x02icône\x02Nom » : icône 16×16 des menus ; « ~Nom » : grisé, non sélectionnable */
	PTK_SCROLLBAR,	/* min/max/value : étendue ; defil : page ; checked : horizontal */
	PTK_SEPARATEUR,	/* trait gravé ; horizontal si w >= h */
	PTK_PETITES_FLECHES,	/* 22×20, value entre min et max, répète au maintien */
	PTK_TRIANGLE,	/* triangle de détail : checked = déplié ; label à droite */
	PTK_BISEAU,	/* bouton biseauté (Appearance) ; contenu dessiné par c->contenu */
	PTK_BARBIER,	/* progression indéterminée : rayures bleues qui défilent (phase dans defil) */
	PTK_RECHERCHE	/* flèches de recherche de Sherlock : chevrons qui se poursuivent */
};

enum {
	PTK_EV_NONE,
	PTK_EV_MOUSE_DOWN,
	PTK_EV_MOUSE_UP,
	PTK_EV_MOUSE_MOVE,
	PTK_EV_KEY,
	PTK_EV_EXPOSE,
	PTK_EV_RESIZE,
	PTK_EV_CLOSE,
	PTK_EV_COMMAND,
	PTK_EV_ACTIVATE,
	PTK_EV_KEY_UP	/* touche relâchée (keycode, keysym) : Key Caps */
};

typedef struct {
	int type;
	int x, y;
	int button;
	unsigned state;
	KeySym keysym;
	int keycode;	/* touche physique (PTK_EV_KEY, PTK_EV_KEY_UP) */
	char text[32];
	int width, height;
} PtkEvent;

struct PtkControl {
	int kind;
	int x, y, w, h;
	char label[128];
	char text[PTK_TEXT_MAX];
	char items[8192];	/* « A|B|C » ; liste : « \x02icône\x02Nom », « ~ » : inactif */
	int value, min, max;
	int checked;
	int enabled;
	int visible;
	int pressed;
	int dedans;		/* bouton maintenu : le pointeur est dans sa zone */
	int focused;
	int caret;
	int caret_on;
	int tout_selectionne;	/* champ : texte sur fond de sélection, remplacé à la frappe */
	XftFont *police;	/* NULL : police des menus */
	int page;		/* onglet où ce contrôle apparaît, -1 : tous */
	int group;		/* radios : 0 partage le groupe historique de la fenêtre */
	int defil;		/* liste : première ligne visible */
	int double_clic;	/* liste : l'action vient d'un double-clic sur la rangée */
	int col2;		/* liste : largeur de la seconde colonne (0 : 72 px) */
	void (*action)(PtkWindow *, PtkControl *, void *);
	void *user;
	/* bouton biseauté : dessine l'icône dans (x, y, w, h), décalée si enfoncé */
	void (*contenu)(PtkWindow *, PtkControl *, int x, int y, int w, int h, int presse);
	PtkControl *next;
};

struct PtkWindow {
	PtkApp *app;
	Window win;
	Pixmap back;
	int back_w, back_h;	/* taille du tampon : réutilisé tant qu'elle ne change pas */
	Ptk *p;
	Drawable surface;
	int x, y, w, h;
	int mapped;
	int active;
	int disposed;
	char title[256];
	char icon[32];
	char menu[4096];
	PtkControl *controls;
	PtkControl *focus;
	PtkControl *popup;
	void (*draw)(PtkWindow *, void *);
	void (*draw_apres)(PtkWindow *);	/* par-dessus les contrôles : textes d'un panneau d'onglets */
	void (*event)(PtkWindow *, PtkEvent *, void *);
	void (*command)(PtkWindow *, const char *, void *);
	/* confirmation de fermeture : retour 0 = la fenêtre reste ouverte (document
	 * modifié, Annuler dans l'alerte) ; NULL : fermeture immédiate */
	int (*close)(PtkWindow *, void *);
	void *close_user;
	void *user;
	PtkWindow *next;
};

struct PtkApp {
	Ptk ptk;
	Display *dpy;
	int screen;
	Window root;
	Atom a_menu, a_app, a_icon, a_cmd, a_context, a_protocols, a_delete;
	Atom a_clip, a_utf8, a_targets;	/* presse-papiers texte (Copier/Couper/Coller) */
	Window clip_win;		/* fenêtre propriétaire de CLIPBOARD */
	char clip_text[PTK_TEXT_MAX];	/* texte publié, servi aux SelectionRequest */
	PtkControl *coller_cible;	/* contrôle qui attend la réponse du collage */
	/* collage demandé par ptk_coller : rappel de l'application */
	void (*coller_rappel)(PtkWindow *, const char *, void *);
	PtkWindow *coller_fenetre;
	void *coller_user;
	Pixmap icon_pix, icon_mask;
	int icon_w, icon_h;
	PtkWindow *windows;
	PtkWindow *active;
	int running;
	int quit_on_last_close;
	int en_sortie;		/* Quitter en cours : les fonctions de fermeture le voient */
	const char *name;
	const char *icon;
	/* PLATINE_DOCUMENT, l'élément du Finder qui a lancé l'application, rangé selon sa
	 * nature : un programme (lui-même ou son alias) va dans « lanceur », jamais dans
	 * « document » — un programme ne s'ouvre jamais lui-même comme document ; un
	 * document (non exécutable) va dans « document », à ouvrir au démarrage */
	char lanceur[1024];
	char document[1024];
	/* minuteries de ptk_app_run : clignotement du curseur texte, et
	 * répétition au maintien (flèches et rail d'ascenseur, petites flèches) */
	double clign_t;
	PtkWindow *rep_w;
	PtkControl *rep_c;
	double rep_t;
	int rep_x, rep_y;	/* dernier point du pointeur pendant le maintien */
	/* événements hors des fenêtres de l'application (racine, fenêtres X
	 * propres) : retour 1 si traité */
	int (*hors)(PtkApp *, XEvent *, void *);
	void *hors_user;
	/* descripteur surveillé par la boucle (sortie d'un processus fils) : -1 aucun */
	int fd_ext;
	void (*fd_pret)(PtkApp *, void *);
	void *fd_user;
	/* minuterie : tic appelé toutes les tic_periode secondes (0 : arrêtée) */
	double tic_periode, tic_t;
	void (*tic)(PtkApp *, void *);
	void *tic_user;
	/* pôle de barbier et flèches de recherche : les contrôles
	 * indéterminés de la fenêtre avancent d'eux-mêmes, ici toutes les
	 * BARBIER_PAS secondes ; barbier_t, échéance ancienne ou future */
	double barbier_t;
	/* messages client hors Toolbox (XDND : Enter, Position, Drop,
	 * Selection) et réponse de sélection XdndSelection : retour 1 si
	 * traités par l'application */
	int (*xdnd)(PtkApp *, XEvent *, void *);
	void *xdnd_user;
};

int ptk_app_init(PtkApp *app, const char *name, const char *icon);
void ptk_app_run(PtkApp *app);
/* traite les événements en attente sans bloquer : pendant un long travail
 * (installation, copie), la fenêtre se redessine et ses boutons répondent */
void ptk_app_traiter(PtkApp *app);
/* boucle d'événements jusqu'à *fini (fenêtre modale) */
void ptk_app_run_modal(PtkApp *app, const int *fini);
/* Zone de dialogue d'enregistrement, à l'image de Standard File sous Mac OS 9 :
 * menu local du dossier (pour remonter), disque, liste du dossier (dossiers
 * ouvrables, fichiers grisés), Éjecter, Bureau, Nouveau dossier, Annuler,
 * Enregistrer, invite et nom du document. Modale. dossier : dossier de départ
 * (NULL : Documents du volume). 1 et le chemin choisi, 0 si Annuler. */
int ptk_enregistrer(PtkApp *app, const char *invite, const char *nom,
    const char *dossier, char *chemin, size_t n);
/* Zone de dialogue d'ouverture (StandardGetFile) : dossiers et documents dont l'extension
 * figure dans types (« mov mp4 … », NULL : tous) ; 1 et le chemin choisi, 0 si Annuler. */
int ptk_ouvrir(PtkApp *app, const char *types, const char *dossier, char *chemin, size_t n);
/* Zone de dialogue de choix de dossier (« Autre… » de l'Installateur) : seuls les
 * dossiers sont montrés, on entre par double-clic, Choisir renvoie le dossier affiché
 * ou choisi ; 1 et le chemin du dossier, 0 si Annuler. Modale. */
int ptk_choisir_dossier(PtkApp *app, const char *invite, const char *dossier,
    char *chemin, size_t n);
/* Une seule copie d'un programme, comme sous Mac OS 9 : si une autre copie tourne déjà
 * (même signature), elle repasse au premier plan et reçoit la commande « Ouvrir le
 * document\t<chemin> » quand document n'est pas NULL ; retour 1, l'appelant s'arrête.
 * Sinon, w devient la fenêtre qui reçoit ces demandes ; retour 0. */
int ptk_instance_unique(PtkApp *app, PtkWindow *w, const char *signature, const char *document);
void ptk_app_quit(PtkApp *app);

PtkWindow *ptk_window_new(PtkApp *app, const char *title,
    int x, int y, int w, int h);
void ptk_window_set_draw(PtkWindow *w,
    void (*draw)(PtkWindow *, void *), void *user);
void ptk_window_set_event(PtkWindow *w,
    void (*event)(PtkWindow *, PtkEvent *, void *), void *user);
void ptk_window_set_command(PtkWindow *w,
    void (*command)(PtkWindow *, const char *, void *), void *user);
void ptk_window_set_menu(PtkWindow *w, const char *menu);
void ptk_window_set_icon(PtkWindow *w, const char *icon);
void ptk_window_set_title(PtkWindow *w, const char *title);
void ptk_window_set_close(PtkWindow *w,
    int (*close)(PtkWindow *, void *), void *user);
/* Règle de la toolbox : toute fenêtre qui représente un fichier en publie le chemin
 * (_PLATINE_CHEMIN) ; le WM rend alors l'icône du titre saisissable, et la déposer dans le
 * Finder y crée un alias. Par défaut : l'élément qui a lancé l'application, ou l'élément du
 * volume (Tableaux de bord, Applications) qui porte le titre de la fenêtre. NULL : aucun. */
void ptk_window_set_document(PtkWindow *w, const char *path);
void ptk_window_show(PtkWindow *w);
void ptk_window_close(PtkWindow *w);
void ptk_window_invalidate(PtkWindow *w);

PtkControl *ptk_control_add(PtkWindow *w, int kind,
    int x, int y, int ww, int hh, const char *label);
void ptk_control_set_action(PtkControl *c,
    void (*action)(PtkWindow *, PtkControl *, void *), void *user);
void ptk_control_set_text(PtkControl *c, const char *text);
void ptk_control_set_items(PtkControl *c, const char *items);
void ptk_control_set_value(PtkControl *c, int value);
/* contrôle réservé à un onglet ; ptk_onglets_afficher montre ceux de l'onglet courant */
void ptk_control_set_page(PtkControl *c, int page);
void ptk_control_set_group(PtkControl *c, int group);
void ptk_onglets_afficher(PtkWindow *w, PtkControl *onglets);
int ptk_control_value(PtkControl *c);

/* Presse-papiers texte pour une application qui dessine elle-même son texte (Pense-bêtes) :
 * ptk_copier publie le texte UTF-8 dans CLIPBOARD ; ptk_coller le demande et appelle recu
 * avec le texte reçu (bip s'il n'y a rien à coller). */
void ptk_copier(PtkWindow *w, const char *texte);
void ptk_coller(PtkWindow *w, void (*recu)(PtkWindow *, const char *, void *), void *user);

int ptk_alert(PtkWindow *parent, const char *message,
    const char *detail, const char *buttons);
void ptk_beep(PtkApp *app);

#endif
