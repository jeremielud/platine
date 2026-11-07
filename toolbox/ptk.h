/*
 * libptk — briques de dessin Platine (Phase 1).
 *
 * Bibliothèque minimale partagée par le WM et les applications de test.
 * Elle ne dépend que de Xlib et Xft. Les widgets complets viendront ensuite.
 */
#ifndef PTK_H
#define PTK_H

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <limits.h>

typedef struct {
	unsigned long pixel;
	unsigned short r, g, b;
	XftColor xft;
	int xft_ok;
} PtkColor;

typedef struct {
	Display *dpy;
	int screen;
	Window root;
	GC gc;
	Pixmap root_pattern;
	char assets_dir[PATH_MAX];
	Visual *visual;
	Colormap cmap;
	XftFont *font_small;
	XftFont *font_small_italic;
	XftFont *font_views;
	XftFont *font_views_italic;
	XftFont *font_menu;
	XftFont *font_menu_bold;
	XftFont *font_shortcut;
	XftFont *font_title;
	PtkColor window;
	PtkColor menu;
	PtkColor titlebar;
	PtkColor text;
	PtkColor text_dim;
	PtkColor text_inactif;	/* #777 : libellés d'une fenêtre inactive ou d'un contrôle désactivé */
	PtkColor light;
	PtkColor shadow;
	PtkColor dark_shadow;
	PtkColor selection;
	PtkColor selection_text;
	PtkColor face;
	PtkColor focus;
	PtkColor field_shadow;
	PtkColor field_select;
	PtkColor desktop;
	PtkColor desktop2;
	PtkColor desktop_light;
	PtkColor desktop_dot;
	PtkColor folder;
	PtkColor folder_dark;
	PtkColor paper;
	PtkColor paper_shadow;
	PtkColor disk;
	PtkColor trash;
	PtkColor trash_dark;
	PtkColor black;
	PtkColor white;
	PtkColor gray[9];
	PtkColor aa[16];
	/* état de dessin, comme le GrafPort : 1 pendant le dessin d'un contrôle
	 * désactivé ou d'une fenêtre en arrière-plan ; les primitives des
	 * contrôles prennent alors l'apparence inactive relevée sur Mac OS 9.2.1
	 * (Apparence en arrière-plan) */
	int inactif;
	int titre_icone_presse;	/* icône du titre foncée pendant qu'on la presse */
	int titre_sans_zoom;	/* fenêtre de taille fixe : pas de case de zoom */
	int menubar_h;
	int titlebar_h;
	int clip_on;
	XRectangle clip;
} Ptk;

int ptk_init(Ptk *p, Display *dpy, int screen);
void ptk_fini(Ptk *p);
PtkColor ptk_color(Ptk *p, const char *name);
XftFont *ptk_font(Ptk *p, const char *name, const char *fallback);
XftFont *ptk_police_bitmap(Ptk *p, const char *fichier, int px);
void ptk_fill(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c);
void ptk_rect(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c);
void ptk_line(Ptk *p, Drawable d, int x1, int y1, int x2, int y2, PtkColor c);
void ptk_clip(Ptk *p, int x, int y, int w, int h);
void ptk_unclip(Ptk *p);
void ptk_bevel(Ptk *p, Drawable d, int x, int y, int w, int h, int sunken);
void ptk_round_frame(Ptk *p, Drawable d, int x, int y, int w, int h,
    PtkColor c);
void ptk_button(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *label, int pressed, int is_default, PtkColor text);
void ptk_field(Ptk *p, Drawable d, int x, int y, int w, int h, int focused);
void ptk_checkbox(Ptk *p, Drawable d, int x, int y, int checked);
void ptk_radio(Ptk *p, Drawable d, int x, int y, int checked);
void ptk_popup(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *text, int open);
void ptk_popup_police(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *text, int open, XftFont *police);
void ptk_assombrir_masque(Ptk *p, Drawable d, Pixmap masque, int x, int y, int w, int h);
void ptk_fill_alpha(Ptk *p, Drawable d, int x, int y, int w, int h, PtkColor c, unsigned short alpha);
void ptk_progress(Ptk *p, Drawable d, int x, int y, int w, int h,
    int value, int max);
/* Barre de progression indéterminée, à la Mac OS 9 : les rayures diagonales
 * bleues défilent vers la droite ; phase entre 0 et 5 (le pas de la trame).
 * Le creux et les lueurs sont ceux de ptk_progress. */
void ptk_barbier(Ptk *p, Drawable d, int x, int y, int w, int h, int phase);
/* Flèches de recherche (Sherlock) : trois chevrons bleus qui se poursuivent
 * dans un creux de champ ; phase entre 0 et 35 (le pas du cycle), actif :
 * 0 en gris. Taille : le creux est posé sur (x, y, w, h). */
void ptk_recherche(Ptk *p, Drawable d, int x, int y, int w, int h, int phase,
    int actif);
/* Séparateur gravé (Apparence > Polices) : trait #888 puis trait blanc
 * décalé d'un pixel ; horizontal si w >= h. */
void ptk_separateur(Ptk *p, Drawable d, int x, int y, int w, int h);
/* Petites flèches 22×20 relevées pixel par pixel ; presse : 0 aucune,
 * 1 la moitié haute, 2 la moitié basse ; actif : 0 grisées. */
#define PTK_PETITES_FLECHES_L 22
#define PTK_PETITES_FLECHES_H 20
void ptk_petites_fleches(Ptk *p, Drawable d, int x, int y, int presse,
    int actif);
/* Triangle de détail 12×12 (vue liste du Finder) : pointe à droite, ou en
 * bas si ouvert ; presse : assombri pendant le clic. */
void ptk_triangle(Ptk *p, Drawable d, int x, int y, int ouvert, int presse);

typedef struct {
	int x, y, w, h;
	int horizontal;
	int value, min, max;
	int page;
	int active;
	int pressed;
} PtkScrollbar;

enum {
	PTK_SB_RIEN,
	PTK_SB_RECUL,
	PTK_SB_AVANCE,
	PTK_SB_PAGE_RECUL,
	PTK_SB_PAGE_AVANCE,
	PTK_SB_POUCE
};

void ptk_scrollbar_draw(Ptk *p, Drawable d, const PtkScrollbar *s);
int ptk_scrollbar_zone(const PtkScrollbar *s, int x, int y);
void ptk_scrollbar_thumb(const PtkScrollbar *s, int *pos, int *len);
int ptk_scrollbar_value(const PtkScrollbar *s, int x, int y, int offset);
/* Sons d'interface (codes Appearance : mnuo, wcls…) : asynchrones, jamais bloquants.
 * PLATINE_SONS=1 les active ; PLATINE_SONS_JOURNAL=fichier note chaque code joué. */
int ptk_son(Ptk *p, const char *code);
/* charge du processeur en % depuis l'appel précédent (-1 au premier) ; mémoire en Ko */
int ptk_cpu_charge(void);
void ptk_memoire(long *utilise_ko, long *total_ko);
long ptk_memoire_installee_mo(long total_ko);
/* clé entière de ~/.config/platine/reglages */
int ptk_reglage_int(const char *cle, int def);
int ptk_fuseau_valide(const char *zone);
void ptk_fuseau_suivre(void);
void ptk_son_stop(int pid);
/* SIGCHLD : enfants ramassés sans zombie, sans SIG_IGN hérité par les programmes lancés */
void ptk_ramasser_enfants(void);
void ptk_title_button(Ptk *p, Drawable d, int x, int y, int glyph, int enfonce);
void ptk_titlebar(Ptk *p, Drawable d, int x, int y, int w, int h,
    const char *title, int active, Pixmap icon, Pixmap icon_mask);
void ptk_text(Ptk *p, Drawable d, XftFont *font, int x, int baseline,
    const char *text, PtkColor c);
int ptk_text_width(Ptk *p, XftFont *font, const char *text);
void ptk_fit_text(Ptk *p, XftFont *font, const char *src, char *dst,
    size_t dstsz, int maxw);
int ptk_text_height(Ptk *p, XftFont *font);
void ptk_set_root_pattern(Ptk *p);
void ptk_icon(Ptk *p, Drawable d, int x, int y, int size, const char *kind);
int ptk_load_xpm(Ptk *p, const char *name, Pixmap *pix, Pixmap *mask);
/* icône 32 bits avec alpha (ombre douce de Mac OS 9), fichier assets/icones/NOM.argb ; NULL si absente */
typedef struct { Picture pic; int w, h; unsigned char *alpha; } PtkArgb;
/* Retour animé d'un élément déposé au mauvais endroit, commun à tout Platine : le contour pointillé de
 * chaque élément (forme de l'icône, cadre du nom) repart vers sa place d'origine avec une traînée. */
typedef struct { XPoint *pts; int n, cap; } PtkZoom;

void ptk_zoom_icone(PtkZoom *z, const PtkArgb *a, int x, int y);
void ptk_zoom_rect(PtkZoom *z, int x, int y, int w, int h);
void ptk_zoom_retour(Ptk *p, PtkZoom *z, int dx, int dy);
/* rectangles d'ouverture et de fermeture (ZoomRects de Mac OS), d'un rectangle de l'écran à un autre */
void ptk_zoom_rects(Ptk *p, int x0, int y0, int w0, int h0, int x1, int y1, int w1, int h1);
/* icône dont le dossier est ouvert : silhouette noire, trame lavande */
void ptk_draw_argb_ouverte(Ptk *p, Drawable d, const PtkArgb *a, int x, int y, int assombri);
/* fantôme d'un glisser : translucide sans compositeur (image de l'écran prise au départ, composition
 * à chaque déplacement), au-dessus de tout et transparent aux clics */
typedef struct { Window win; Pixmap ecran, tampon; int w, h; } PtkFantome;
int ptk_fantome_debut(Ptk *p, PtkFantome *f, int w, int h);
void ptk_fantome_poser(Ptk *p, PtkFantome *f, int x, int y, void (*dessin)(Ptk *, Drawable, void *), void *user);
void ptk_fantome_fin(Ptk *p, PtkFantome *f);
PtkArgb *ptk_load_argb(Ptk *p, const char *name);
void ptk_draw_argb(Ptk *p, Drawable d, const PtkArgb *a, int x, int y, int assombri);
Pixmap ptk_load_ppm(Ptk *p, const char *name, int *w, int *h);
Pixmap ptk_tile_pixmap(Ptk *p, Pixmap tile, int tw, int th, int w, int h);
void ptk_draw_pixmap(Ptk *p, Drawable d, Pixmap pix, Pixmap mask,
    int x, int y, int w, int h);

/* --- Identité des programmes et arborescence du volume (ptk_identite.c) --- */
typedef struct {
	char nom[64];
	char signature[8];
	char icone[48];
	char version[32];
	char types[128];
} PtkIdentite;

/* racine du volume « Eee PC 701 » : PLATINE_VOLUME, sinon la racine du disque si
 * Platine y est installé (/System/Finder), sinon $HOME (développement) */
const char *ptk_volume(void);
/* nom à montrer pour l'élément « nom » du dossier « dossier » : les dossiers du
 * Macintosh ont un nom anglais sur le disque et un nom français à l'écran
 * (System → Système, Desktop → Bureau…) ; sinon « nom » lui-même */
const char *ptk_nom_affiche(const char *dossier, const char *nom);
/* l'élément est-il masqué au Finder ? Noms en point, et à la racine du disque les
 * dossiers BSD (bin, etc, usr…) et ceux de /.hidden */
int ptk_masque(const char *dossier, const char *nom);
/* dossier du dossier personnel (« Documents », « Movies »…) ; et le dossier du
 * bureau (XDG_DESKTOP_DIR, ~/Desktop, ~/Bureau) : 1 s'il existe */
int ptk_perso(const char *rel, char *out, size_t n);
int ptk_bureau(char *out, size_t n);
/* identité embarquée (section .platine) ; 0 si le fichier n'en a pas */
int ptk_identite_lire(const char *chemin, PtkIdentite *id);
int ptk_identite_ouvre(const PtkIdentite *id, const char *extension);
/* base de bureau : programme installé (Applications, System) par
 * signature, ou qui ouvre une extension ; 1 et son chemin si trouvé */
int ptk_programme(const char *signature, char *out, size_t outsz);
int ptk_programme_pour(const char *extension, char *out, size_t outsz);
/* « @chemin/dans/le/volume|commande de repli » ou « #signature|repli » →
 * chemin cité pour sh -c s'il est installé, sinon la commande de repli ; une
 * référence sans @ ni # est rendue telle quelle */
const char *ptk_resoudre(const char *ref, char *out, size_t outsz);
int ptk_dans_volume(const char *rel, char *out, size_t outsz);
/* programme du volume pour exec : son chemin s'il est installé (retour 1),
 * sinon le nom de repli cherché dans le PATH (retour 0) */
int ptk_chemin_programme(const char *rel, const char *repli, char *out, size_t outsz);

#endif
