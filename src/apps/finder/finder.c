/*
 * platine-finder — bureau et fenêtres spatiales de Platine.
 *
 * Finder natif : bureau iconique, fenêtres de dossiers, sélection multiple,
 * glisser-déposer spatial et entre dossiers, corbeille et menus.
 */
#define _XOPEN_SOURCE 700

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/cursorfont.h>

#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <strings.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "../../../toolbox/ptk_sys.h"
#include "../../../toolbox/ptk.h"
#include "fsops.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Finder", "MACS", "app-finder", PTK_VERSION, "");


#define MAX_ICONS 512
#define LISTE_ENTETE 20	/* l'en-tête couvre le trait du bandeau et 20 rangées de plus */
#define LISTE_Y0 (CONTENT_TOP + LISTE_ENTETE)
#define LISTE_RANG 19
#define CELL_W 128	/* grille de Mac OS 9, relevée en fenêtre et sur le bureau */
#define CELL_H 64
#define ICON_SIZE 32
#define DESKTOP_ICON_X(f) ((f)->w - 65)	/* glyphe à x=735 sur 800 px, comme la VM */
#define WIN_W 480
#define WIN_H 380
#define TOOLBAR_H 26
#define CONTENT_TOP 47	/* bandeau 46 px (barre d'adresse + informations), trait noir */
#define BARRE 16	/* barres de défilement : trait + 15 px, comme Mac OS 9 */

/* Disposition des icônes, comme les Options de présentation de Mac OS 9 */
enum { RANGE_AUCUNE, RANGE_GRILLE, RANGE_TRIEE };
enum { RP_NOM, RP_DATE_MODIF, RP_DATE_CREATION, RP_TAILLE, RP_TYPE, RP_ETIQUETTE, RP_NB };
static const char *rp_noms[RP_NB] = {
	"par nom", "par date de modification", "par date de création", "par taille",
	"par type", "par étiquette"
};
/* Platine ne connaît ni date de création ni étiquettes : ces clés restent grisées */
#define RP_ACTIF(k) ((k) != RP_DATE_CREATION && (k) != RP_ETIQUETTE)

typedef struct {
	char name[256];
	char vu[256];	/* nom montré : un dossier du Macintosh a un nom anglais sur le
			 * disque et un nom français à l'écran (ptk_nom_affiche) */
	char path[PATH_MAX];
	int is_dir;
	int is_alias;
	int x, y;
	int selected;
	int kind; /* 0 dossier, 1 document, 2 disque, 3 corbeille */
	time_t date;
	long long taille;
	int niveau, ouvert;	/* vue liste : profondeur et triangle déplié */
} Icon;

typedef struct FWin {
	Window win;
	char path[PATH_MAX];
	int x, y, w, h;
	int view;
	char hist[64][PATH_MAX];
	int hist_n, hist_pos;
	int addr_focus;
	char addr[PATH_MAX];
	int addr_len;
	int is_info;
	char info_path[PATH_MAX];
	char info_name[256];
	char info_comment[1024];
	int info_focus;
	int scroll, scroll_x;	/* défilement en pixels, sur les deux axes */
	int n;
	Icon icons[MAX_ICONS];
	int selected;
	char chemin_pose[PATH_MAX];
	int tri, tri_inv;	/* vue liste : 0 nom, 1 date, 2 taille, 3 type */
	/* disposition des icônes (Options de présentation de Mac OS 9) :
	 * RANGE_AUCUNE, RANGE_GRILLE (toujours aligner), RANGE_TRIEE (toujours
	 * ranger, par range_par : RP_NOM…) */
	int range, range_par;
	int is_desktop;
	char menu_publie[6144];
	Pixmap tampon;
	int tampon_w, tampon_h;
	struct FWin *next;
} FWin;

static Display *dpy;
static int screen;
static Window root;
static int sw, sh;
static Ptk ptk;
static Cursor cursor;
static Cursor copy_cursor;

static PtkArgb *arg_dossier16, *arg_document16, *arg_dossier, *arg_document, *arg_disque, *arg_corbeille, *arg_corbeille_pleine;
enum { DOC_GENERIC, DOC_TEXTE, DOC_IMAGE, DOC_SON, DOC_FILM, DOC_ARCHIVE,
    DOC_WEB, DOC_SCRIPT, DOC_NB };
static const char *doc_noms[DOC_NB] = {
	"document", "document-texte", "document-image", "document-wav",
	"document-film", "document-archive", "document-web", "document-script"
};
static PtkArgb *doc32[DOC_NB], *doc16[DOC_NB];

/* Icônes propres aux tableaux de bord : les fichiers du dossier « Tableaux de bord »
 * sont des scripts, mais doivent apparaître comme les panneaux de Mac OS 9. */
enum { TAB_APPARENCE, TAB_CLAVIER, TAB_DICTION, TAB_SON, TAB_SOURIS, TAB_ECRAN,
    TAB_CONTROLE, TAB_DATE, TAB_ENERGIE, TAB_FOLDER, TAB_NB };
static const char *tab_noms[TAB_NB] = {
	"Apparence", "Clavier", "Diction", "Son", "Souris", "Écran",
	"Control Strip", "Date et heure", "Économiseur d'énergie", "Tableaux de bord"
};
static const char *tab_fichiers[TAB_NB] = {
	"tableau-apparence", "tableau-clavier", "tableau-diction", "tableau-son",
	"tableau-souris", "tableau-moniteurs", "tableau-control-strip",
	"tableau-date", "tableau-economiseur", "dossier-tableaux"
};
static PtkArgb *tab32[TAB_NB], *tab16[TAB_NB];
static FWin *wins;
static FWin *desktop;
static FWin *active;
/* premier plan ailleurs : autre application, ou bureau (_PLATINE_BUREAU, posé par le WM) */
static int focus_ailleurs, bureau_devant;
#define fen_active(w) ((w) == active && !focus_ailleurs && !bureau_devant)
static int cascade_x = 40, cascade_y = 40;
static FWin *bt_win;	/* bouton de barre suivi tant que le bouton de la souris est appuyé */
static int bt_x, bt_glyphe, bt_dedans;
static FWin *sb_drag;
static int sb_drag_y;
static int sb_drag_scroll;
static int sb_drag_axe, sb_repeat_zone;
static FWin *sb_repeat_win;
static int sb_repeat_dir;
static int sb_repeat_grabbed;
static long long sb_repeat_next;
static int drag_copy_shown;

static Atom a_utf8, a_menu, a_app, a_icon, a_cmd, a_protocols, a_delete;
static Atom a_context, a_geometry, a_clip, a_uri_list, a_targets;
static Atom a_xdnd_aware, a_xdnd_enter, a_xdnd_position, a_xdnd_status;
static Atom a_xdnd_leave, a_xdnd_drop, a_xdnd_selection, a_xdnd_finished;
static Atom a_xdnd_action_copy, a_xdnd_type_list;
/* Marge de sécurité : ces racines sont concaténées avec des suffixes. */
#define PATH_SAFE (PATH_MAX - 64)

static char home_dir[PATH_SAFE];
/* le volume « Eee PC 701 » : la racine du disque une fois installé (D11) */
static char vol_dir[PATH_SAFE];
#define NOM_VU(ic) ((ic)->vu[0] != '\0' ? (ic)->vu : (ic)->name)
static char trash_dir[PATH_SAFE];
/* dossier du bureau (le « Desktop Folder » de Mac OS 9) : ce qu'il contient est posé
 * sur le bureau, avec le disque et la Corbeille ; invisible dans la fenêtre du disque */
static char bureau_dir[PATH_SAFE];
static time_t bureau_mtime;
static long long bureau_verif_at;
#define BUREAU_FIXES 2	/* disque et Corbeille, en tête de desktop->icons */
static char trash_info_dir[PATH_SAFE];
static char state_dir[PATH_SAFE];

typedef struct WGeom {
	char path[PATH_MAX];
	int x, y, w, h, view;
	int tri, tri_inv;	/* vue liste : colonne et sens du tri */
	int range, range_par;	/* disposition des icônes */
	struct WGeom *next;
} WGeom;

typedef struct IPos {
	char folder[PATH_MAX];
	char name[256];
	int x, y;
	struct IPos *next;
} IPos;

/* métadonnées type/créateur (codes 4 lettres à la Mac OS) par chemin */
typedef struct TPos {
	char path[PATH_MAX];
	char type[8];
	char crea[8];
	struct TPos *next;
} TPos;

static WGeom *geoms;
static IPos *positions;
static TPos *types_db;

static FWin *drag_win;
static int drag_idx;
static int drag_start_x, drag_start_y;
static int drag_icon_x, drag_icon_y;
static int drag_moved;
static int drag_group_n;
static int drag_group_idx[512];
static int drag_group_x[512];
static int drag_group_y[512];
static int drag_grab_x, drag_grab_y;
static int drag_pointer_grabbed;
static int drag_last_x, drag_last_y;
static char spring_path[PATH_MAX];
static long long spring_at;
static int spring_armed;
/* presse-papiers du Finder : chemins absolus, pp_couper = déplacer au collage */
#define PP_MAX 64
static char pp_chemins[PP_MAX][PATH_MAX];
static int pp_n;
static int pp_couper;
static char xdnd_chemins[PP_MAX][PATH_MAX];
static int xdnd_n;
static Window xdnd_out;
static int xdnd_out_accept;
static Window xdnd_in;
static int xdnd_in_accept;
static FWin *drop_hover_win;
static int drop_hover_idx = -1;
static int mq_active;
static int mq_x0, mq_y0, mq_x1, mq_y1;
static FWin *mq_win;
static GC mq_gc;
static Pixmap mq_stipple;

static FWin *rename_win;
static int rename_idx;
static char rename_buf[256];
static int rename_len;
static int rename_select_all;

static Pixmap dim_stipple;
static GC dim_gc;
static Pixmap pix_folder_dim, pix_doc_dim, pix_disk_dim;
static Pixmap pix_trash_dim, pix_trash_full_dim;
static Pixmap pix_folder, pix_folder_mask;
static Pixmap pix_doc, pix_doc_mask;
static Pixmap pix_disk, pix_disk_mask;
static Pixmap pix_trash, pix_trash_mask;
static Pixmap pix_trash_full, pix_trash_full_mask;
static Pixmap pix_folder16, pix_folder16_mask;
static Pixmap pix_doc16, pix_doc16_mask;
static Pixmap fond_tile;
static Pixmap fond_bureau;
static int fond_w, fond_h;

#define COCHE "\xe2\x88\x9a "
#define SOUS "\x1f"
#define ET "\x1e"

static void update_menu(FWin *w);
static int pp_interne(void);
static void motif_du_reglage(void);
static void charger_motif(const char *nom);
static unsigned long double_clic_ms(void);
static void mq_retracer(FWin *w);
static int zoom_src_ok, zoom_sx, zoom_sy, zoom_st;
static void zoom_source_de(FWin *w, int idx);
static int icone_ouverte(const Icon *ic);
static PtkColor gris(int v);
static void changer_vue(FWin *w, int vue);
static void draw_window(FWin *w);
static void open_path(const char *path);
static void navigate_to(FWin *w, const char *path);
static void set_addr(FWin *w);
static void draw_toolbar(FWin *w);
static void send_context(FWin *w, int x, int y, const char *menu);
static const char *path_base(const char *path);
static int label_max(FWin *w);
static void open_info_for(FWin *parent, int idx);
static const char *kind_de(const Icon *ic);
static int document_family(const Icon *ic);
static void load_types(void);
static void draw_info_window(FWin *w);
static int cell_w(FWin *w);
static int cell_h(FWin *w);
static int content_cols(FWin *w, int sb);
static void assign_icon_positions(FWin *w);
static int grille_x0(const FWin *w);
static const char *kind_de(const Icon *ic);
static void close_window(FWin *w);
static void refresh_window(FWin *w);
static FWin *find_win(Window win);
static void finish_drag(int xr, int yr, unsigned int state);
static void restore_drag(void);
static void set_drop_hover(FWin *w, int idx);
static int alias_lire(const char *path, char *cible, size_t taille);
static void spring_fire(void);
static void remap_metadata(const char *old, const char *new, int copy);
static int protected_source(const char *path);
static void refresh_after_drop(void);
static int resolve_drop(int xr, int yr, FWin **target, int *target_idx, int *local_x, int *local_y, char *dest, size_t destsz, int *on_folder);
static void restore_selection(FWin *w);
static void scroll_by(FWin *w, int delta);
static int finder_alert(const char *message, const char *cancel,
    const char *ok, Pixmap icon, Pixmap mask);
static void scroll_repeat_stop(void);
static void trash_display_name(const char *entry, char *out, size_t outsz);
static void drop_error(const char *message, const char *path);
static int path_within(const char *dir, const char *path);

static void
set_utf8(Window w, Atom prop, const char *value)
{
	XChangeProperty(dpy, w, prop, a_utf8, 8, PropModeReplace,
	    (const unsigned char *)value, (int)strlen(value));
}

static void
set_xdnd_aware(Window win)
{
	long version = 5;

	XChangeProperty(dpy, win, a_xdnd_aware, XA_ATOM, 32, PropModeReplace,
	    (unsigned char *)&version, 1);
}

static void
join_path(char *dst, size_t dstsz, const char *dir, const char *name)
{
	size_t dn = strlen(dir), nn = strlen(name);
	int slash = !(dn > 0 && dir[dn - 1] == '/');

	if (dn + nn + (size_t)slash + 1 > dstsz) {
		errno = ENAMETOOLONG;
		dst[0] = '\0';
		return;
	}
	memcpy(dst, dir, dn);
	if (slash)
		dst[dn++] = '/';
	memcpy(dst + dn, name, nn + 1);
}

/* Concaténation bornée, sans avertissement de troncature du compilateur. */
static int
path_concat(char *dst, size_t dstsz, const char *base, const char *suffix)
{
	size_t bn = strlen(base), sn = strlen(suffix);

	if (bn + sn + 1 > dstsz) {
		errno = ENAMETOOLONG;
		dst[0] = '\0';
		return -1;
	}
	memcpy(dst, base, bn);
	memcpy(dst + bn, suffix, sn + 1);
	return 0;
}

static const char *
display_name(const char *path)
{
	static char parent[PATH_MAX];

	if (strcmp(path, vol_dir) == 0)
		return "Eee PC 701";
	if (strcmp(path, trash_dir) == 0)
		return "Corbeille";
	{
		const char *s = strrchr(path, '/');

		if (s == NULL || s[1] == '\0')
			return path;
		snprintf(parent, sizeof(parent), "%.*s", s == path ? 1 : (int)(s - path), path);
		return ptk_nom_affiche(parent, s + 1);
	}
}

static int
icon_kind(const char *path, int is_dir)
{
	const char *ext;

	if (is_dir)
		return strcmp(path, trash_dir) == 0 ? 3 : 0;
	ext = strrchr(path, '.');
	if (ext == NULL)
		return 1;
	ext++;
	if (strcasecmp(ext, "app") == 0)
		return 1;
	return 1;
}

static int
cmp_icons(const void *a, const void *b)
{
	const Icon *ia = a, *ib = b;

	if (ia->is_dir != ib->is_dir)
		return ib->is_dir - ia->is_dir;
	/* l'ordre de ce qu'on voit : « Éléments du menu Pomme », pas « Apple Menu Items » */
	return strcoll(NOM_VU(ia), NOM_VU(ib));
}

static const char *
folder_key(FWin *w)
{
	return w->is_desktop ? "__bureau__" : w->path;
}

static void
save_state(void)
{
	char path[PATH_MAX];
	FILE *f;
	WGeom *g;
	IPos *p;

	path_concat(path, sizeof(path), state_dir, "/fenetres");
	f = fopen(path, "w");
	if (f != NULL) {
		for (g = geoms; g; g = g->next)
			fprintf(f, "W\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", g->path,
			    g->x, g->y, g->w, g->h, g->view, g->tri, g->tri_inv,
			    g->range, g->range_par);
		fclose(f);
	}
	path_concat(path, sizeof(path), state_dir, "/icones");
	f = fopen(path, "w");
	if (f != NULL) {
		for (p = positions; p; p = p->next)
			fprintf(f, "I\t%s\t%s\t%d\t%d\n", p->folder,
			    p->name, p->x, p->y);
		fclose(f);
	}
}

static void
load_state(void)
{
	char path[PATH_MAX], line[PATH_MAX * 2];
	FILE *f;

	path_concat(path, sizeof(path), state_dir, "/fenetres");
	f = fopen(path, "r");
	if (f != NULL) {
		while (fgets(line, sizeof(line), f) != NULL) {
			char *p[12];
			char *save;
			int n = 0;
			char *tok = strtok_r(line, "\t\n", &save);
			while (tok != NULL && n < 12) {
				p[n++] = tok;
				tok = strtok_r(NULL, "\t\n", &save);
			}
			if (n >= 7 && p[0][0] == 'W') {
				WGeom *g = calloc(1, sizeof(*g));
				if (g == NULL)
					break;
				snprintf(g->path, sizeof(g->path), "%s", p[1]);
				g->x = atoi(p[2]);
				g->y = atoi(p[3]);
				g->w = atoi(p[4]);
				g->h = atoi(p[5]);
				g->view = atoi(p[6]);
				g->tri = n >= 9 ? atoi(p[7]) : 0;
				g->tri_inv = n >= 9 ? atoi(p[8]) : 0;
				g->range = n >= 11 ? atoi(p[9]) : RANGE_AUCUNE;
				g->range_par = n >= 11 ? atoi(p[10]) : RP_NOM;
				if (g->range < RANGE_AUCUNE || g->range > RANGE_TRIEE)
					g->range = RANGE_AUCUNE;
				if (g->range_par < 0 || g->range_par >= RP_NB || !RP_ACTIF(g->range_par))
					g->range_par = RP_NOM;
				g->next = geoms;
				geoms = g;
			}
		}
		fclose(f);
	}
	load_types();
	path_concat(path, sizeof(path), state_dir, "/icones");
	f = fopen(path, "r");
	if (f != NULL) {
		while (fgets(line, sizeof(line), f) != NULL) {
			char *p[8];
			char *save;
			int n = 0;
			char *tok = strtok_r(line, "\t\n", &save);
			while (tok != NULL && n < 8) {
				p[n++] = tok;
				tok = strtok_r(NULL, "\t\n", &save);
			}
			if (n >= 5 && p[0][0] == 'I') {
				IPos *ip = calloc(1, sizeof(*ip));
				if (ip == NULL)
					break;
				snprintf(ip->folder, sizeof(ip->folder), "%s",
				    p[1]);
				snprintf(ip->name, sizeof(ip->name), "%s", p[2]);
				ip->x = atoi(p[3]);
				ip->y = atoi(p[4]);
				ip->next = positions;
				positions = ip;
			}
		}
		fclose(f);
	}
}

static WGeom *
get_geom(const char *path, int *x, int *y, int *w, int *h, int *view,
    int *tri, int *tri_inv)
{
	WGeom *g;

	for (g = geoms; g; g = g->next) {
		if (strcmp(g->path, path) == 0) {
			*x = g->x;
			*y = g->y;
			*w = g->w;
			*h = g->h;
			*view = g->view;
			*tri = g->tri;
			*tri_inv = g->tri_inv;
			return g;
		}
	}
	return NULL;
}

static void
set_geom(FWin *w)
{
	WGeom *g;
	const char *key = folder_key(w);

	for (g = geoms; g; g = g->next) {
		if (strcmp(g->path, key) == 0) {
			g->x = w->x;
			g->y = w->y;
			g->w = w->w;
			g->h = w->h;
			g->view = w->view;
			g->tri = w->tri;
			g->tri_inv = w->tri_inv;
			g->range = w->range;
			g->range_par = w->range_par;
			save_state();
			return;
		}
	}
	g = calloc(1, sizeof(*g));
	if (g == NULL)
		return;
	snprintf(g->path, sizeof(g->path), "%s", key);
	g->x = w->x;
	g->y = w->y;
	g->w = w->w;
	g->h = w->h;
	g->view = w->view;
	g->tri = w->tri;
	g->tri_inv = w->tri_inv;
	g->range = w->range;
	g->range_par = w->range_par;
	g->next = geoms;
	geoms = g;
	save_state();
}

/* disposition enregistrée d'un dossier (ou du bureau, « __bureau__ ») */
static void
geom_range(const char *key, int *range, int *par)
{
	WGeom *g;

	*range = RANGE_AUCUNE;
	*par = RP_NOM;
	for (g = geoms; g; g = g->next)
		if (strcmp(g->path, key) == 0) {
			*range = g->range;
			*par = g->range_par;
			return;
		}
}

/* les positions du bureau sont rangées sous « __bureau__ », qu'on les désigne par le
 * bureau lui-même ou par le chemin de son dossier (dépôt depuis une fenêtre) */
static const char *
cle_pos(const char *folder)
{
	return bureau_dir[0] != '\0' && strcmp(folder, bureau_dir) == 0 ? "__bureau__" : folder;
}

static IPos *
get_pos(const char *folder, const char *name, int *x, int *y)
{
	IPos *p;

	folder = cle_pos(folder);
	for (p = positions; p; p = p->next) {
		if (strcmp(p->folder, folder) == 0 &&
		    strcmp(p->name, name) == 0) {
			*x = p->x;
			*y = p->y;
			return p;
		}
	}
	return NULL;
}

static void
set_pos(const char *folder, const char *name, int x, int y)
{
	IPos *p;

	folder = cle_pos(folder);
	for (p = positions; p; p = p->next) {
		if (strcmp(p->folder, folder) == 0 &&
		    strcmp(p->name, name) == 0) {
			p->x = x;
			p->y = y;
			return;
		}
	}
	p = calloc(1, sizeof(*p));
	if (p == NULL)
		return;
	snprintf(p->folder, sizeof(p->folder), "%s", folder);
	snprintf(p->name, sizeof(p->name), "%s", name);
	p->x = x;
	p->y = y;
	p->next = positions;
	positions = p;
}

static void
move_pos(const char *folder, const char *from, const char *to)
{
	IPos *p;

	folder = cle_pos(folder);
	for (p = positions; p; p = p->next) {
		if (strcmp(p->folder, folder) == 0 &&
		    strcmp(p->name, from) == 0) {
			snprintf(p->name, sizeof(p->name), "%s", to);
			return;
		}
	}
}

static void
remove_pos(const char *folder, const char *name)
{
	IPos **pp;

	folder = cle_pos(folder);
	for (pp = &positions; *pp != NULL; ) {
		IPos *p = *pp;

		if (strcmp(p->folder, folder) == 0 &&
		    strcmp(p->name, name) == 0) {
			*pp = p->next;
			free(p);
			return;
		}
		pp = &p->next;
	}
}

static int
slot_free(FWin *w, int x, int y, int upto)
{
	int i;

	for (i = 0; i < upto; i++) {
		if (w->icons[i].x >= 0 && abs(w->icons[i].x - x) < cell_w(w) &&
		    abs(w->icons[i].y - y) < cell_h(w))
			return 0;
	}
	return 1;
}

/*
 * La grille des icônes, celle de Mac OS 9 (cases de 128 × 64) :
 *  - en fenêtre, elle part du coin haut gauche du contenu, et les nouveaux
 *    éléments la remplissent rangée par rangée, de gauche à droite ;
 *  - sur le bureau, elle part du coin haut droit (le disque de démarrage en
 *    tête), et la remplit colonne par colonne, de haut en bas puis vers la
 *    gauche.
 * Une case se repère par sa colonne et sa rangée ; la position d'une icône
 * est le coin de sa case (fenêtre) ou de son image (bureau, x = DESKTOP_ICON_X).
 */
static int
grille_haut(const FWin *w)
{
	return w->is_desktop ? 24 : CONTENT_TOP + 3;	/* icône à 4 px sous le trait, comme la VM */
}

static int
grille_cols(FWin *w)
{
	if (w->is_desktop) {
		int c = DESKTOP_ICON_X(w) / cell_w(w) + 1;

		return c > 0 ? c : 1;
	}
	if (w->view == 0) {
		/* par icônes : tant que l'icône et 40 px d'étiquette tiennent */
		int c = (w->w - BARRE - 60 - (grille_x0(w) + 22)) / cell_w(w) + 1;

		return c > 0 ? c : 1;
	}
	return content_cols(w, 1);
}

/* une position enregistrée reste-t-elle dans la fenêtre ? */
static int
grille_tient(FWin *w, int x)
{
	if (w->is_desktop)	/* l'image tient à l'écran */
		return x + 32 <= w->w;
	if (w->view == 0)
		return x + 22 + 40 <= w->w - BARRE;
	return x + cell_w(w) <= w->w - BARRE;
}

/* rangées du bureau : jusqu'au bas de l'écran ; une fenêtre défile, sans limite */
static int
grille_rangs(FWin *w)
{
	if (w->is_desktop) {
		int r = (w->h - grille_haut(w) - 8) / cell_h(w);

		return r > 0 ? r : 1;
	}
	return MAX_ICONS;
}

/* bord gauche de la première colonne en fenêtre. Par icônes, l'image est à
 * x + 6 : centre de la première icône à 57 px du contenu (VM : 58 px du bord de
 * la fenêtre), l'étiquette d'un long nom tient dans la fenêtre. Par boutons,
 * le bouton est centré dans sa case. */
static int
grille_x0(const FWin *w)
{
	return w->view == 1 ? 10 : 35;
}

static void
grille_xy(FWin *w, int col, int row, int *x, int *y)
{
	*x = w->is_desktop ? DESKTOP_ICON_X(w) - col * cell_w(w) : grille_x0(w) + col * cell_w(w);
	*y = grille_haut(w) + row * cell_h(w);
}

/* k-ième case dans l'ordre de remplissage */
static void
grille_case(FWin *w, int k, int *x, int *y)
{
	if (w->is_desktop)
		grille_xy(w, k / grille_rangs(w), k % grille_rangs(w), x, y);
	else
		grille_xy(w, k % grille_cols(w), k / grille_cols(w), x, y);
}

/* la case libre la plus proche de (x, y) ; « occupe » marque les icônes déjà
 * posées (toutes si NULL, sauf « sauf ») */
static void
grille_libre_proche(FWin *w, int x, int y, const char *occupe, int sauf, int *gx, int *gy)
{
	int cw = cell_w(w), ch = cell_h(w), cols = grille_cols(w), rangs = grille_rangs(w);
	int c0, r0, rayon, best = INT_MAX;

	c0 = w->is_desktop ? (DESKTOP_ICON_X(w) - x + cw / 2) / cw : (x - grille_x0(w) + cw / 2) / cw;
	r0 = (y - grille_haut(w) + ch / 2) / ch;
	if (c0 < 0)
		c0 = 0;
	if (c0 >= cols)
		c0 = cols - 1;
	if (r0 < 0)
		r0 = 0;
	if (r0 >= rangs)
		r0 = rangs - 1;
	*gx = -1;
	for (rayon = 0; rayon < MAX_ICONS && *gx < 0; rayon++) {
		int c, r;

		for (r = r0 - rayon; r <= r0 + rayon; r++)
			for (c = c0 - rayon; c <= c0 + rayon; c++) {
				int tx, ty, i, libre = 1, d;

				if (r < 0 || c < 0 || c >= cols || r >= rangs ||
				    (abs(r - r0) != rayon && abs(c - c0) != rayon))
					continue;
				grille_xy(w, c, r, &tx, &ty);
				for (i = 0; i < w->n && libre; i++)
					if (i != sauf && (occupe == NULL || occupe[i]) &&
					    abs(w->icons[i].x - tx) < cw && abs(w->icons[i].y - ty) < ch)
						libre = 0;
				if (!libre)
					continue;
				d = (tx - x) * (tx - x) + (ty - y) * (ty - y);
				if (d < best) {
					best = d;
					*gx = tx;
					*gy = ty;
				}
			}
		if (rayon > cols + rangs && rayon > 64)
			break;
	}
	if (*gx < 0)
		grille_case(w, 0, gx, gy);
}

/* ordre de « Toujours ranger » et de « Réorganiser » : sur le bureau, les
 * disques d'abord et la Corbeille à la fin, comme Mac OS 9 */
static int range_cle, range_bureau;
static FWin *range_win;

static int
cmp_range(const void *a, const void *b)
{
	const Icon *x = &range_win->icons[*(const int *)a], *y = &range_win->icons[*(const int *)b];
	int r = 0;

	if (range_bureau) {
		int px = x->kind == 2 ? 0 : x->kind == 3 ? 2 : 1;
		int py = y->kind == 2 ? 0 : y->kind == 3 ? 2 : 1;

		if (px != py)
			return px - py;
	}
	switch (range_cle) {
	case RP_DATE_MODIF: r = x->date < y->date ? 1 : x->date > y->date ? -1 : 0; break;
	case RP_TAILLE: r = x->taille < y->taille ? 1 : x->taille > y->taille ? -1 : 0; break;
	case RP_TYPE: r = strcoll(kind_de(x), kind_de(y)); break;
	}
	return r != 0 ? r : strcoll(x->name, y->name);
}

/* range les icônes dans les cases, dans l'ordre de la clé */
static void
ranger_par(FWin *w, int cle)
{
	int ordre[MAX_ICONS], i;

	for (i = 0; i < w->n; i++)
		ordre[i] = i;
	range_cle = cle;
	range_bureau = w->is_desktop;
	range_win = w;
	qsort(ordre, (size_t)w->n, sizeof(ordre[0]), cmp_range);
	for (i = 0; i < w->n; i++)
		grille_case(w, i, &w->icons[ordre[i]].x, &w->icons[ordre[i]].y);
}

/* aligne sur la grille les icônes marquées : chacune va dans la case libre la
 * plus proche, en commençant par la plus haut à gauche (droite sur le bureau),
 * comme le « Nettoyer » de Mac OS 9 ; les autres icônes restent en place */
static void
aligner(FWin *w, const char *bouge)
{
	char pose[MAX_ICONS];
	int ordre[MAX_ICONS], n = 0, i, j;

	for (i = 0; i < w->n; i++) {
		if (bouge == NULL || bouge[i]) {
			pose[i] = 0;
			ordre[n++] = i;
		} else
			pose[i] = w->icons[i].x >= 0;	/* pas encore placée : ne gêne pas */
	}
	for (i = 1; i < n; i++)	/* tri par insertion, rangée puis colonne */
		for (j = i; j > 0; j--) {
			Icon *a = &w->icons[ordre[j - 1]], *b = &w->icons[ordre[j]];
			int ka = a->y / cell_h(w) * 4096 + (w->is_desktop ? -a->x : a->x);
			int kb = b->y / cell_h(w) * 4096 + (w->is_desktop ? -b->x : b->x);
			int t;

			if (ka <= kb)
				break;
			t = ordre[j - 1];
			ordre[j - 1] = ordre[j];
			ordre[j] = t;
		}
	for (i = 0; i < n; i++) {
		Icon *ic = &w->icons[ordre[i]];

		grille_libre_proche(w, ic->x, ic->y, pose, ordre[i], &ic->x, &ic->y);
		pose[ordre[i]] = 1;
	}
}

static void
enregistrer_positions(FWin *w)
{
	int i;

	for (i = 0; i < w->n; i++)
		set_pos(folder_key(w), w->icons[i].name, w->icons[i].x, w->icons[i].y);
	save_state();
}

static void
assign_icon_positions(FWin *w)
{
	int i, k = 0;
	int top = grille_haut(w);
	const char *key = folder_key(w);

	/* Toujours ranger : l'ordre décide de tout, les positions enregistrées non */
	if (w->range == RANGE_TRIEE && w->view != 2) {
		ranger_par(w, w->range_par);
		enregistrer_positions(w);
		return;
	}
	/*
	 * Deux passes : d'abord réserver toutes les positions enregistrées, ensuite
	 * placer les nouveaux éléments dans une case encore libre. Sans cela, un
	 * nouvel élément placé avant un ancien dans l'ordre trié pouvait prendre sa
	 * case (Control Strip ajouté après coup dans « Tableaux de bord »).
	 */
	for (i = 0; i < w->n; i++) {
		int x, y;

		w->icons[i].x = -1;
		w->icons[i].y = -1;
		if (get_pos(key, w->icons[i].name, &x, &y) != NULL) {
			/* ancienne colonne du bureau (x = largeur - 113, rangées de 64 px
			 * depuis 23) : migrée ; une icône posée à la main n'est pas touchée */
			if (w->is_desktop && x == w->w - 113 && y >= 23 &&
			    ((y - 23) % 64 == 0 || (y - 24) % 64 == 0))
				x = DESKTOP_ICON_X(w);
			/* disposition « Aucune » : comme Mac OS 9, l'icône reste où on l'a
			 * posée, même par-dessus une autre ; les autres dispositions
			 * gardent une case par icône */
			if (x >= 0 && grille_tient(w, x) && y >= top &&
			    (w->range == RANGE_AUCUNE || slot_free(w, x, y, i))) {
				w->icons[i].x = x;
				w->icons[i].y = y;
			}
		}
	}
	/* Toujours aligner : une icône posée hors grille (déposée d'ailleurs,
	 * ou d'avant le réglage) rejoint la case libre la plus proche */
	if (w->range == RANGE_GRILLE) {
		char hors[MAX_ICONS];

		for (i = 0; i < w->n; i++)
			hors[i] = w->icons[i].x >= 0;
		aligner(w, hors);
	}
	/* la Corbeille sans place enregistrée va dans le coin bas droit, comme sous Mac OS 9 */
	if (w->is_desktop && w->n > 1 && w->icons[1].kind == 3 && w->icons[1].x < 0) {
		int x, y;

		grille_xy(w, 0, grille_rangs(w) - 1, &x, &y);
		if (slot_free(w, x, y, w->n)) {
			w->icons[1].x = x;
			w->icons[1].y = y;
		}
	}
	for (i = 0; i < w->n; i++) {
		int x, y;

		if (w->icons[i].x >= 0)
			continue;
		/* première case libre dans l'ordre de remplissage */
		for (; k < MAX_ICONS * 2; k++) {
			grille_case(w, k, &x, &y);
			if (slot_free(w, x, y, w->n))
				break;
		}
		if (k >= MAX_ICONS * 2)
			grille_case(w, 0, &x, &y);
		w->icons[i].x = x;
		w->icons[i].y = y;
	}
	enregistrer_positions(w);
}

/* rectangle à l'écran de l'icône d'où partent les zooms d'ouverture */

static void
zoom_source_de(FWin *w, int idx)
{
	Window r;
	int ox, oy, x, y;

	if (w->view == 2) {
		x = 21 + 16 * w->icons[idx].niveau - w->scroll_x;
		y = LISTE_Y0 + idx * LISTE_RANG - w->scroll + 1;
		zoom_st = 16;
	} else {
		int sx = w->is_desktop ? 0 : w->scroll_x, sy = w->is_desktop ? 0 : w->scroll;

		x = w->icons[idx].x - sx +
		    (w->is_desktop ? 0 : w->view == 1 ? (cell_w(w) - ICON_SIZE) / 2 : 6);
		y = w->icons[idx].y - sy + (w->view == 1 ? 9 : 1);
		zoom_st = ICON_SIZE;
	}
	XTranslateCoordinates(dpy, w->win, root, x, y, &ox, &oy, &r);
	zoom_sx = ox;
	zoom_sy = oy;
	zoom_src_ok = 1;
}

/* cette icône a-t-elle sa fenêtre ouverte ? */
static int
icone_ouverte(const Icon *ic)
{
	FWin *o;

	if (!ic->is_dir)
		return 0;
	for (o = wins; o != NULL; o = o->next)
		if (!o->is_desktop && !o->is_info && strcmp(o->path, ic->path) == 0)
			return 1;
	return 0;
}

/* --- Fantôme du glisser : icônes et noms du groupe, translucides, au-dessus de tout --- */
static PtkFantome fantome;
static int fantome_pret, fantome_x0, fantome_y0, drag_dx_final, drag_dy_final, drag_brut_dx, drag_brut_dy;

/* où est un élément du groupe glissé, dans sa fenêtre : icône, puis cadre du nom */
static void
item_geo(FWin *w, int k, int *ix, int *iy, int *taille, int *lx, int *ly, int *lw, char *t, size_t tn)
{
	Icon *ic = &w->icons[drag_group_idx[k]];

	if (w->view == 2) {
		int y = LISTE_Y0 + drag_group_idx[k] * LISTE_RANG - w->scroll;

		*taille = 16;
		*ix = 21 + 16 * ic->niveau - w->scroll_x;
		*iy = y + 1;
		ptk_fit_text(&ptk, ptk.font_views, NOM_VU(ic), t, tn, 214 - 46 - 16 * ic->niveau);
		*lx = *ix + 19;
		*ly = y + 2;
		*lw = ptk_text_width(&ptk, ptk.font_views, t) + 4;
	} else {
		int cw = cell_w(w), sx = w->is_desktop ? 0 : w->scroll_x, sy = w->is_desktop ? 0 : w->scroll;
		int x = drag_group_x[k] - sx, y = drag_group_y[k] - sy;

		*taille = ICON_SIZE;
		*ix = w->is_desktop ? x : w->view == 1 ? x + (cw - ICON_SIZE) / 2 : x + 6;
		*iy = y + (w->view == 1 ? 9 : 1);
		ptk_fit_text(&ptk, ptk.font_views, NOM_VU(ic), t, tn, label_max(w));
		*lw = ptk_text_width(&ptk, ptk.font_views, t) + 6;
		*lx = *ix + ICON_SIZE / 2 - *lw / 2;
		*ly = w->view == 1 ? y + 49 : *iy + ICON_SIZE;
	}
}

static int
tableau_index(const Icon *ic)
{
	int i;

	if (ic->is_dir)
		return strcmp(NOM_VU(ic), tab_noms[TAB_FOLDER]) == 0 ? TAB_FOLDER : -1;
	for (i = 0; i < TAB_NB; i++)
		if (strcmp(ic->name, tab_noms[i]) == 0)
			return i;
	return -1;
}

static PtkArgb *
tableau_image(const Icon *ic, int taille)
{
	int i = tableau_index(ic);

	if (i < 0)
		return NULL;
	return taille == 16 ? tab16[i] : tab32[i];
}

/* Programmes : un fichier exécutable. Son identité embarquée (section
 * .platine, comme la ressource d'une application Mac OS 9) donne son icône ;
 * sans identité, l'icône d'application générique. Mémorisé par chemin et
 * date de modification. */
static struct {
	char path[PATH_MAX];
	time_t mtime;
	int prog;
	char icone[48];
} prog_cache[128];
static int prog_cache_pos;

static int
est_programme(const Icon *ic, char *icone, size_t n)
{
	struct stat st;
	PtkIdentite id;
	int i, k;

	if (ic->is_dir || ic->is_alias || stat(ic->path, &st) != 0 ||
	    !S_ISREG(st.st_mode))
		return 0;
	for (i = 0; i < (int)(sizeof(prog_cache) / sizeof(prog_cache[0])); i++)
		if (prog_cache[i].mtime == st.st_mtime &&
		    strcmp(prog_cache[i].path, ic->path) == 0) {
			if (icone != NULL)
				snprintf(icone, n, "%s", prog_cache[i].icone);
			return prog_cache[i].prog;
		}
	k = prog_cache_pos;
	prog_cache_pos = (prog_cache_pos + 1) % (int)(sizeof(prog_cache) / sizeof(prog_cache[0]));
	snprintf(prog_cache[k].path, sizeof(prog_cache[k].path), "%s", ic->path);
	prog_cache[k].mtime = st.st_mtime;
	prog_cache[k].prog = access(ic->path, X_OK) == 0;
	prog_cache[k].icone[0] = '\0';
	if (prog_cache[k].prog && ptk_identite_lire(ic->path, &id) &&
	    id.icone[0] != '@')
		snprintf(prog_cache[k].icone, sizeof(prog_cache[k].icone), "%s", id.icone);
	if (icone != NULL)
		snprintf(icone, n, "%s", prog_cache[k].icone);
	return prog_cache[k].prog;
}

static PtkArgb *
icone_programme(const char *nom, int taille)
{
	static struct { char nom[48]; PtkArgb *a[2]; int charge[2]; } t[48];
	static int nt;
	int i, j = taille == 16;
	char f[80];

	for (i = 0; i < nt; i++)
		if (strcmp(t[i].nom, nom) == 0)
			break;
	if (i == nt) {
		if (nt == (int)(sizeof(t) / sizeof(t[0])))
			return NULL;
		memset(&t[nt], 0, sizeof(t[nt]));
		snprintf(t[nt].nom, sizeof(t[nt].nom), "%s", nom);
		nt++;
	}
	if (!t[i].charge[j]) {
		t[i].charge[j] = 1;
		snprintf(f, sizeof(f), "%s-%d", nom, taille);
		t[i].a[j] = ptk_load_argb(&ptk, f);
		if (t[i].a[j] == NULL && taille == 32)
			t[i].a[j] = ptk_load_argb(&ptk, nom);
	}
	return t[i].a[j];
}

static PtkArgb *
programme_image(const Icon *ic, int taille)
{
	char icone[48], cible[PATH_MAX];
	PtkArgb *a = NULL;
	Icon t;

	/* l'alias d'un programme montre l'icône du programme, comme sous Mac OS 9 */
	if (ic->is_alias && !ic->is_dir &&
	    (alias_lire(ic->path, cible, sizeof(cible)) || realpath(ic->path, cible) != NULL)) {
		t = *ic;
		snprintf(t.path, sizeof(t.path), "%s", cible);
		t.is_alias = 0;
		ic = &t;
	}
	if (!est_programme(ic, icone, sizeof(icone)))
		return NULL;
	if (icone[0] != '\0')
		a = icone_programme(icone, taille);
	return a != NULL ? a : icone_programme("application", taille);
}

/* dossiers spéciaux de l'arborescence Macintosh, reconnus par leur chemin
 * dans le volume, avec leur icône propre comme sous Mac OS 9 */
static const char *
dossier_special(const Icon *ic)
{
	static const struct { const char *rel, *icone; } t[] = {
		{ "/Applications", "dossier-applications" },
		{ "/System", "dossier-systeme" },
		{ "/System/Extensions", "dossier-extensions" },
		{ "/System/Fonts", "dossier-polices" },
		{ "/System/Preferences", "dossier-preferences" },
		{ "/System/Startup Items", "dossier-demarrage" },
	};
	/* le volume « / » : la comparaison part du chemin entier */
	size_t lh = strcmp(vol_dir, "/") == 0 ? 0 : strlen(vol_dir), i;
	char vrai[PATH_MAX];
	const char *chemin = ic->path;

	if (!ic->is_dir)
		return NULL;
	/* d'abord sa place (System/Fonts, alias vers les polices de X), puis, pour un
	 * alias, celle de son original (alias Applications du bureau) */
	if (strncmp(chemin, vol_dir, lh) == 0)
		for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
			if (strcmp(chemin + lh, t[i].rel) == 0)
				return t[i].icone;
	if (!ic->is_alias || realpath(ic->path, vrai) == NULL ||
	    strncmp(vrai, vol_dir, lh) != 0)
		return NULL;
	chemin = vrai;
	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (strcmp(chemin + lh, t[i].rel) == 0)
			return t[i].icone;
	return NULL;
}

static PtkArgb *
item_image(const Icon *ic, int taille)
{
	PtkArgb *a = tableau_image(ic, taille);
	int famille;
	const char *special;

	if (a != NULL)
		return a;
	if ((special = dossier_special(ic)) != NULL &&
	    (a = icone_programme(special, taille)) != NULL)
		return a;
	if ((a = programme_image(ic, taille)) != NULL)
		return a;
	if (taille == 16 && ic->is_dir)
		return arg_dossier16;
	if (taille != 16) {
		if (ic->kind == 3)
			return arg_corbeille;
		if (ic->kind == 2)
			return arg_disque;
		if (ic->is_dir)
			return arg_dossier;
	}
	famille = document_family(ic);
	a = taille == 16 ? doc16[famille] : doc32[famille];
	return a != NULL ? a : taille == 16 ? arg_document16 : arg_document;
}

static void
fantome_dessin(Ptk *p, Drawable d, void *user)
{
	FWin *w = user;
	int k, ox, oy;
	Window r;

	XTranslateCoordinates(dpy, w->win, root, 0, 0, &ox, &oy, &r);
	for (k = 0; k < drag_group_n; k++) {
		int ix, iy, ta, lx, ly, lw;
		char t[256];

		item_geo(w, k, &ix, &iy, &ta, &lx, &ly, &lw, t, sizeof(t));
		ptk_draw_argb(p, d, item_image(&w->icons[drag_group_idx[k]], ta), ox + ix - fantome_x0, oy + iy - fantome_y0, 2);
		ptk_fill_alpha(p, d, ox + lx - fantome_x0, oy + ly - fantome_y0, lw, 13, p->black, 0x7000);
		ptk_text(p, d, p->font_views, ox + lx + 3 - fantome_x0, oy + ly + 10 - fantome_y0, t, ptk_color(p, "#bbbbcc"));
	}
}

static void
fantome_suivre(int dx, int dy)
{
	if (drag_win == NULL || drag_group_n <= 0)
		return;
	drag_brut_dx = dx;
	drag_brut_dy = dy;
	if (!fantome_pret) {
		int k, ox, oy, x1 = INT_MIN, y1 = INT_MIN;
		Window r;

		fantome_x0 = fantome_y0 = INT_MAX;
		XTranslateCoordinates(dpy, drag_win->win, root, 0, 0, &ox, &oy, &r);
		for (k = 0; k < drag_group_n; k++) {
			int ix, iy, ta, lx, ly, lw;
			char t[256];

			item_geo(drag_win, k, &ix, &iy, &ta, &lx, &ly, &lw, t, sizeof(t));
#define BORNE(a, b) do { if ((a) < fantome_x0) fantome_x0 = (a); if ((b) < fantome_y0) fantome_y0 = (b); } while (0)
			BORNE(ox + ix, oy + iy);
			BORNE(ox + lx, oy + ly);
#undef BORNE
			if (ox + ix + ta > x1)
				x1 = ox + ix + ta;
			if (ox + lx + lw > x1)
				x1 = ox + lx + lw;
			if (oy + iy + ta > y1)
				y1 = oy + iy + ta;
			if (oy + ly + 13 > y1)
				y1 = oy + ly + 13;
		}
		ptk_fantome_debut(&ptk, &fantome, x1 - fantome_x0, y1 - fantome_y0);
		fantome_pret = 1;
	}
	ptk_fantome_poser(&ptk, &fantome, fantome_x0 + dx, fantome_y0 + dy, fantome_dessin, drag_win);
}

/* dépôt refusé : le contour de chaque élément revient à sa place (animation commune à Platine) */
static void
retour_anime(void)
{
	PtkZoom z = { NULL, 0, 0 };
	int k, ox, oy;
	Window r;

	if (drag_win == NULL || (drag_brut_dx == 0 && drag_brut_dy == 0))
		return;
	XTranslateCoordinates(dpy, drag_win->win, root, 0, 0, &ox, &oy, &r);
	for (k = 0; k < drag_group_n; k++) {
		int ix, iy, ta, lx, ly, lw;
		char t[256];

		item_geo(drag_win, k, &ix, &iy, &ta, &lx, &ly, &lw, t, sizeof(t));
		ptk_zoom_icone(&z, item_image(&drag_win->icons[drag_group_idx[k]], ta), ox + ix, oy + iy);
		ptk_zoom_rect(&z, ox + lx, oy + ly, lw, 14);
	}
	ptk_zoom_retour(&ptk, &z, drag_brut_dx, drag_brut_dy);
	drag_brut_dx = drag_brut_dy = 0;
}

/* --- Vue liste de Mac OS 9, relevée au pixel sur la VM --- */
static const struct { const char *nom; int w; } liste_cols[] = {
	{ "Nom", 214 }, { "Date de modification", 165 }, { "Taille", 66 }, { "Type", 0 },
};
/* largeur des colonnes de la vue liste (Type au moins 120 px, bouton de tri
 * compris) : au-delà de la fenêtre, la liste défile horizontalement */
#define LISTE_LARGEUR (214 + 165 + 66 + 120)
static int tri_col_q, tri_inv_q;

static const char *
liste_type(const Icon *ic)
{
	return kind_de(ic);
}

static int
cmp_liste(const void *a, const void *b)
{
	const Icon *x = a, *y = b;
	int r = 0;

	switch (tri_col_q) {
	case 1: r = x->date < y->date ? 1 : x->date > y->date ? -1 : 0; break;
	case 2: r = x->taille < y->taille ? 1 : x->taille > y->taille ? -1 : 0; break;
	case 3: r = strcoll(liste_type(x), liste_type(y)); break;
	}
	if (r == 0)
		r = strcoll(NOM_VU(x), NOM_VU(y));
	return tri_inv_q ? -r : r;
}

/* Alias Platine : petit fichier « <nom> alias » commençant par le magic
 * PLAL suivi du chemin absolu de la cible. Les liens symboliques restent
 * affichés en italique (alias visuel) mais s'ouvrent normalement. */
#define ALIAS_MAGIC "PLAL"

static int
alias_lire(const char *path, char *cible, size_t taille)
{
	FILE *f = fopen(path, "r");
	char mag[4];
	size_t n;

	if (f == NULL)
		return 0;
	if (fread(mag, 1, 4, f) != 4 || memcmp(mag, ALIAS_MAGIC, 4) != 0) {
		fclose(f);
		return 0;
	}
	n = fread(cible, 1, taille - 1, f);
	fclose(f);
	while (n > 0 && (cible[n - 1] == '\n' || cible[n - 1] == '\r'))
		n--;
	cible[n] = '\0';
	return n > 0;
}

static int
est_alias_fichier(const char *path)
{
	char cible[PATH_MAX];

	return alias_lire(path, cible, sizeof(cible));
}

static int
alias_ecrire(const char *cible, const char *dst)
{
	FILE *f = fopen(dst, "w");

	if (f == NULL)
		return -1;
	if (fwrite(ALIAS_MAGIC, 1, 4, f) != 4 ||
	    fputs(cible, f) == EOF) {
		fclose(f);
		unlink(dst);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	return 0;
}

/* retire les dépliages puis trie : Mac OS 9 ne range pas les dossiers à part */
static void
liste_trier(FWin *w)
{
	int i, n = 0;

	if (w->view != 2 || w->is_desktop)
		return;
	for (i = 0; i < w->n; i++)
		if (w->icons[i].niveau == 0) {
			if (n != i)
				w->icons[n] = w->icons[i];
			w->icons[n].ouvert = 0;
			n++;
		}
	w->n = n;
	tri_col_q = w->tri;
	tri_inv_q = w->tri_inv;
	qsort(w->icons, (size_t)w->n, sizeof(w->icons[0]), cmp_liste);
}

static void
liste_deselectionner(FWin *w)
{
	int i;

	for (i = 0; i < w->n; i++)
		w->icons[i].selected = 0;
	w->selected = -1;
}

/* triangle : déplie le dossier sur place (ses éléments suivent, décalés) ou le replie */
static void
liste_basculer(FWin *w, int idx)
{
	Icon *ic = &w->icons[idx];
	static Icon lus[MAX_ICONS];
	int n = 0, i;

	liste_deselectionner(w);
	if (ic->ouvert) {
		for (i = idx + 1; i < w->n && w->icons[i].niveau > ic->niveau; i++)
			;
		memmove(&w->icons[idx + 1], &w->icons[i], (size_t)(w->n - i) * sizeof(Icon));
		w->n -= i - idx - 1;
		ic->ouvert = 0;
		return;
	}
	{
		DIR *d = opendir(ic->path);
		struct dirent *de;

		if (d == NULL)
			return;
		while ((de = readdir(d)) != NULL && n < MAX_ICONS) {
			struct stat st;
			Icon *c = &lus[n];

			if (ptk_masque(ic->path, de->d_name))
				continue;
			memset(c, 0, sizeof(*c));
			snprintf(c->name, sizeof(c->name), "%s", de->d_name);
			join_path(c->path, sizeof(c->path), ic->path, de->d_name);
			if (stat(c->path, &st) != 0)
				continue;
			c->is_dir = S_ISDIR(st.st_mode);
			if (c->is_dir)
				snprintf(c->vu, sizeof(c->vu), "%s", ptk_nom_affiche(ic->path, c->name));
			c->date = st.st_mtime;
			c->taille = st.st_size;
			c->niveau = ic->niveau + 1;
			c->kind = icon_kind(c->path, c->is_dir);
			{
				struct stat l;

				if (lstat(c->path, &l) == 0 && S_ISLNK(l.st_mode))
					c->is_alias = 1;
				else if (!S_ISDIR(st.st_mode) &&
				    est_alias_fichier(c->path))
					c->is_alias = 1;
			}
			n++;
		}
		closedir(d);
	}
	if (w->n + n > MAX_ICONS)
		n = MAX_ICONS - w->n;
	tri_col_q = w->tri;
	tri_inv_q = w->tri_inv;
	qsort(lus, (size_t)n, sizeof(lus[0]), cmp_liste);
	memmove(&w->icons[idx + 1 + n], &w->icons[idx + 1], (size_t)(w->n - idx - 1) * sizeof(Icon));
	memcpy(&w->icons[idx + 1], lus, (size_t)n * sizeof(Icon));
	w->n += n;
	ic->ouvert = 1;
}

static void
load_dir(FWin *w)
{
	DIR *d;
	struct dirent *de;
	int n = 0;

	int fixes = 0, cache_bureau;
	char parent[PATH_MAX];

	w->n = 0;
	w->scroll = 0;
	w->scroll_x = 0;
	w->selected = -1;
	if (w->is_desktop) {
		/* le disque de démarrage et la Corbeille, toujours là */
		memset(&w->icons[0], 0, 2 * sizeof(Icon));
		snprintf(w->icons[0].name, sizeof(w->icons[0].name), "Eee PC 701");
		snprintf(w->icons[0].path, sizeof(w->icons[0].path), "%s", vol_dir);
		w->icons[0].is_dir = 1;
		w->icons[0].kind = 2;
		snprintf(w->icons[1].name, sizeof(w->icons[1].name), "Corbeille");
		snprintf(w->icons[1].path, sizeof(w->icons[1].path), "%s", trash_dir);
		w->icons[1].is_dir = 1;
		w->icons[1].kind = 3;
		n = fixes = BUREAU_FIXES;
		{
			struct stat bs;

			bureau_mtime = stat(bureau_dir, &bs) == 0 ? bs.st_mtime : 0;
		}
	}
	/* le dossier du bureau ne se montre pas dans la fenêtre de son parent, comme
	 * le Desktop Folder de Mac OS 9 */
	snprintf(parent, sizeof(parent), "%s", bureau_dir);
	if (strrchr(parent, '/') != NULL)
		*strrchr(parent, '/') = '\0';
	cache_bureau = strcmp(w->path, parent) == 0;
	d = opendir(w->path);
	if (d == NULL) {
		w->n = n;
		if (w->is_desktop)
			assign_icon_positions(w);
		return;
	}
	while ((de = readdir(d)) != NULL && n < MAX_ICONS) {
		struct stat st;
		Icon *ic;

		/* noms en point, et à la racine du disque les dossiers BSD */
		if (ptk_masque(w->path, de->d_name))
			continue;
		if (cache_bureau && strcmp(de->d_name, path_base(bureau_dir)) == 0)
			continue;
		ic = &w->icons[n];
		memset(ic, 0, sizeof(*ic));
		snprintf(ic->name, sizeof(ic->name), "%s", de->d_name);
		if (strcmp(w->path, trash_dir) == 0)
			trash_display_name(de->d_name, ic->name,
			    sizeof(ic->name));
		join_path(ic->path, sizeof(ic->path), w->path, de->d_name);
		if (stat(ic->path, &st) != 0)
			continue;
		{
			struct stat lst;
			if (lstat(ic->path, &lst) == 0 && S_ISLNK(lst.st_mode))
				ic->is_alias = 1;
			else if (!S_ISDIR(st.st_mode) && est_alias_fichier(ic->path))
				ic->is_alias = 1;
		}
		ic->is_dir = S_ISDIR(st.st_mode);
		if (ic->is_dir)
			snprintf(ic->vu, sizeof(ic->vu), "%s", ptk_nom_affiche(w->path, ic->name));
		ic->date = st.st_mtime;
		ic->taille = st.st_size;
		ic->kind = icon_kind(ic->path, ic->is_dir);
		n++;
	}
	closedir(d);
	w->n = n;
	qsort(w->icons + fixes, (size_t)(w->n - fixes), sizeof(w->icons[0]), cmp_icons);
	liste_trier(w);
	assign_icon_positions(w);
}

static void
free_window(FWin *w)
{
	FWin **pp;

	for (pp = &wins; *pp; pp = &(*pp)->next) {
		if (*pp == w) {
			*pp = w->next;
			break;
		}
	}
	if (active == w)
		active = wins;
	free(w);
}

static void
close_window(FWin *w)
{
	FWin *o;
	int fx = w != NULL ? w->x : 0, fy = w != NULL ? w->y : 0, fw = w != NULL ? w->w + 13 : 0, fh = w != NULL ? w->h + 29 : 0;
	int sur_icone = 0;

	if (w == NULL || w->is_desktop)
		return;
	/* zoom de fermeture vers l'icône du dossier, si elle est visible */
	for (o = wins; o != NULL && !sur_icone && !w->is_info; o = o->next) {
		int i;

		if (o == w || o->is_info)
			continue;
		for (i = 0; i < o->n; i++)
			if (strcmp(o->icons[i].path, w->path) == 0) {
				zoom_source_de(o, i);
				sur_icone = 1;
				break;
			}
	}
	XDestroyWindow(dpy, w->win);
	free_window(w);
	XSync(dpy, False);
	if (sur_icone) {
		ptk_zoom_rects(&ptk, fx, fy, fw, fh, zoom_sx, zoom_sy, zoom_st, zoom_st);
		zoom_src_ok = 0;
	}
	/* l'icône du dossier refermé reprend son aspect normal */
	for (o = wins; o != NULL; o = o->next)
		if (!o->is_info)
			draw_window(o);
	XFlush(dpy);
}

static void
open_path(const char *path)
{
	FWin *w;
	XSizeHints hints;

	for (w = wins; w; w = w->next) {
		if (!w->is_desktop && strcmp(w->path, path) == 0) {
			XRaiseWindow(dpy, w->win);
			active = w;
			return;
		}
	}
	w = calloc(1, sizeof(*w));
	if (w == NULL)
		return;
	snprintf(w->path, sizeof(w->path), "%s", path);
	{
		int gx, gy, gw, gh, gv, gt, gti;
		if (get_geom(path, &gx, &gy, &gw, &gh, &gv, &gt, &gti)) {
			w->x = gx;
			w->y = gy;
			w->w = gw;
			w->h = gh;
			w->view = gv;
			w->tri = gt;
			w->tri_inv = gti;
			geom_range(path, &w->range, &w->range_par);
		} else {
			w->x = cascade_x;
			w->y = cascade_y;
			cascade_x += 24;
			cascade_y += 24;
			if (cascade_x > sw - 260)
				cascade_x = 40;
			if (cascade_y > sh - 220)
				cascade_y = 40;
			w->w = WIN_W;
			w->h = WIN_H;
			w->view = 0;
		}
	}
	w->hist_n = 1;
	w->hist_pos = 0;
	snprintf(w->hist[0], sizeof(w->hist[0]), "%s", path);
	set_addr(w);
	if (w->x + w->w > sw)
		w->x = sw - w->w - 10;
	if (w->y + w->h > sh)
		w->y = sh - w->h - 10;
	if (w->y < ptk.menubar_h)
		w->y = ptk.menubar_h;
	load_dir(w);

	{
		XSetWindowAttributes a;
		a.background_pixel = ptk.white.pixel;
		a.event_mask = ExposureMask | FocusChangeMask | StructureNotifyMask |
		    PropertyChangeMask | ButtonPressMask | ButtonReleaseMask |
		    PointerMotionMask | KeyPressMask;
		w->win = XCreateWindow(dpy, root, w->x, w->y, (unsigned)w->w,
		    (unsigned)w->h, 0, CopyFromParent, InputOutput,
		    CopyFromParent, CWBackPixel | CWEventMask, &a);
	}
	XStoreName(dpy, w->win, display_name(path));
	memset(&hints, 0, sizeof(hints));
	hints.flags = PMinSize | USPosition | PPosition;
	hints.min_width = 260;
	hints.min_height = 180;
	hints.x = w->x;
	hints.y = w->y;
	XSetWMNormalHints(dpy, w->win, &hints);
	XSetWMProtocols(dpy, w->win, &a_delete, 1);
	set_xdnd_aware(w->win);
	set_utf8(w->win, a_app, "Finder");
	set_utf8(w->win, a_icon,
	    strcmp(path_base(path), "Tableaux de bord") == 0 ||
	    strcmp(path_base(path), "Control Panels") == 0 ?
	    "dossier-tableaux" : "folder");

	w->next = wins;
	wins = w;
	active = w;
	if (zoom_src_ok) {
		ptk_zoom_rects(&ptk, zoom_sx, zoom_sy, zoom_st, zoom_st, w->x, w->y, w->w + 13, w->h + 29);
		zoom_src_ok = 0;
	}
	XMapWindow(dpy, w->win);
	XFlush(dpy);
}

static void
navigate_to(FWin *w, const char *path)
{
	struct stat st;

	if (w == NULL || w->is_desktop)
		return;
	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	if (strcmp(path, w->path) == 0)
		return;
	if (w->hist_pos + 1 >= 64) {
		w->hist_pos = 0;
		w->hist_n = 1;
	}
	w->hist_pos++;
	snprintf(w->hist[w->hist_pos], sizeof(w->hist[0]), "%s", path);
	w->hist_n = w->hist_pos + 1;
	snprintf(w->path, sizeof(w->path), "%s", path);
	w->scroll = 0;
	w->scroll_x = 0;
	w->selected = -1;
	load_dir(w);
	set_addr(w);
	XStoreName(dpy, w->win, display_name(w->path));
	draw_window(w);
}

static void
history_go(FWin *w, int delta)
{
	int pos = w->hist_pos + delta;

	if (w == NULL || w->is_desktop || pos < 0 || pos >= w->hist_n)
		return;
	w->hist_pos = pos;
	snprintf(w->path, sizeof(w->path), "%s", w->hist[pos]);
	w->scroll = 0;
	w->scroll_x = 0;
	w->selected = -1;
	load_dir(w);
	set_addr(w);
	XStoreName(dpy, w->win, display_name(w->path));
	draw_window(w);
}

static int
trash_is_empty(void)
{
	DIR *d = opendir(trash_dir);
	struct dirent *de;
	int empty = 1;

	if (d == NULL)
		return 1;
	while ((de = readdir(d)) != NULL) {
		if (strcmp(de->d_name, ".") != 0 &&
		    strcmp(de->d_name, "..") != 0) {
			empty = 0;
			break;
		}
	}
	closedir(d);
	return empty;
}

static Pixmap
dim_pixmap(Pixmap src, int w, int h)
{
	XImage *img;
	Pixmap out;
	GC gc;
	int x, y;
	unsigned long rm = ptk.visual->red_mask;
	unsigned long gm = ptk.visual->green_mask;
	unsigned long bm = ptk.visual->blue_mask;
	int rs = 0, gs = 0, bs = 0;

	if (src == None)
		return None;
	while (!(rm & 1)) {
		rm >>= 1;
		rs++;
	}
	while (!(gm & 1)) {
		gm >>= 1;
		gs++;
	}
	while (!(bm & 1)) {
		bm >>= 1;
		bs++;
	}
	img = XGetImage(dpy, src, 0, 0, (unsigned)w, (unsigned)h,
	    AllPlanes, ZPixmap);
	if (img == NULL)
		return None;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			unsigned long p = XGetPixel(img, x, y);
			unsigned long r = (p >> rs) & 0xff;
			unsigned long g = (p >> gs) & 0xff;
			unsigned long b = (p >> bs) & 0xff;

			r = (r * 55) / 100;
			g = (g * 55) / 100;
			b = (b * 55) / 100;
			p = (r << rs) | (g << gs) | (b << bs);
			XPutPixel(img, x, y, p);
		}
	}
	out = XCreatePixmap(dpy, root, (unsigned)w, (unsigned)h,
	    (unsigned)DefaultDepth(dpy, screen));
	gc = XCreateGC(dpy, out, 0, NULL);
	XPutImage(dpy, out, gc, img, 0, 0, 0, 0, (unsigned)w,
	    (unsigned)h);
	XFreeGC(dpy, gc);
	XDestroyImage(img);
	return out;
}

static void
dim_icon(FWin *w, int x, int y, Pixmap mask)
{
	if (dim_stipple == None || dim_gc == NULL || mask == None)
		return;
	XSetClipOrigin(dpy, dim_gc, x, y);
	XSetClipMask(dpy, dim_gc, mask);
	XSetFillStyle(dpy, dim_gc, FillStippled);
	XSetStipple(dpy, dim_gc, dim_stipple);
	XSetForeground(dpy, dim_gc, ptk.black.pixel);
	XFillRectangle(dpy, w->win, dim_gc, x, y, ICON_SIZE, ICON_SIZE);
	XSetFillStyle(dpy, dim_gc, FillStippled);
	XSetStipple(dpy, dim_gc, dim_stipple);
	XSetClipMask(dpy, dim_gc, None);
}

static void
mq_rect(int *x, int *y, int *w, int *h)
{
	*x = mq_x0 < mq_x1 ? mq_x0 : mq_x1;
	*y = mq_y0 < mq_y1 ? mq_y0 : mq_y1;
	*w = (mq_x0 < mq_x1 ? mq_x1 : mq_x0) - *x;
	*h = (mq_y0 < mq_y1 ? mq_y1 : mq_y0) - *y;
}

/* Lasso : tracé par-dessus la fenêtre, effacé en recopiant ses bords depuis le tampon. Contrairement à un
 * second passage en XOR, cela reste juste même si la fenêtre s'est redessinée entre-temps. */
static int mq_trace, mq_tx, mq_ty, mq_tw, mq_th;

static void
mq_effacer(FWin *w)
{
	if (!mq_trace)
		return;
	mq_trace = 0;
	if (w->tampon == None)
		return;
	XCopyArea(dpy, w->tampon, w->win, ptk.gc, mq_tx, mq_ty, (unsigned)mq_tw + 1, 1, mq_tx, mq_ty);
	XCopyArea(dpy, w->tampon, w->win, ptk.gc, mq_tx, mq_ty + mq_th, (unsigned)mq_tw + 1, 1, mq_tx, mq_ty + mq_th);
	XCopyArea(dpy, w->tampon, w->win, ptk.gc, mq_tx, mq_ty, 1, (unsigned)mq_th + 1, mq_tx, mq_ty);
	XCopyArea(dpy, w->tampon, w->win, ptk.gc, mq_tx + mq_tw, mq_ty, 1, (unsigned)mq_th + 1, mq_tx + mq_tw, mq_ty);
}

static void
mq_draw(FWin *w)
{
	int x, y, rw, rh;

	if (!mq_active || mq_gc == NULL)
		return;
	mq_effacer(w);
	mq_rect(&x, &y, &rw, &rh);
	if (!w->is_desktop) {
		/* jamais sur la barre d'adresse ni sur les ascenseurs */
		int x1 = x + rw, y1 = y + rh;

		if (y < CONTENT_TOP)
			y = CONTENT_TOP;
		if (x1 > w->w - BARRE - 1)
			x1 = w->w - BARRE - 1;
		if (y1 > w->h - BARRE - 1)
			y1 = w->h - BARRE - 1;
		if (x1 < x || y1 < y)
			return;
		rw = x1 - x;
		rh = y1 - y;
	}
	XSetFunction(dpy, mq_gc, GXxor);
	XSetForeground(dpy, mq_gc, 0xffffffffUL);
	XSetFillStyle(dpy, mq_gc, FillStippled);
	XSetStipple(dpy, mq_gc, mq_stipple);
	XDrawRectangle(dpy, w->win, mq_gc, x, y, (unsigned)rw,
	    (unsigned)rh);
	XSetFillStyle(dpy, mq_gc, FillSolid);
	XSetFunction(dpy, mq_gc, GXcopy);
	mq_trace = 1;
	mq_tx = x;
	mq_ty = y;
	mq_tw = rw;
	mq_th = rh;
}

/* la fenêtre vient d'être recopiée : le lasso en cours est à reposer */
static void
mq_retracer(FWin *w)
{
	if (mq_active && w == mq_win) {
		mq_trace = 0;
		mq_draw(w);
	}
}

/* largeur maximale d'une étiquette : comme Mac OS 9, elle peut dépasser la cellule
 * (surtout sur le bureau) et reste centrée sous l'icône ; seule la fenêtre la coupe. */
static int
label_max(FWin *w)
{
	if (w->view == 1)
		return cell_w(w) - 8;
	if (w->is_desktop)
		return w->w - 8;
	return w->w - BARRE - 8;
}

static void
draw_icon(FWin *w, Icon *ic, int x, int y)
{
	char label[256];
	Pixmap used_mask = None;
	XftFont *font = ic->is_alias ? ptk.font_views_italic :
	    ptk.font_views;
	int cw = cell_w(w), ch = cell_h(w);
	int lw, maxw = label_max(w);
	int ix = w->is_desktop ? x : w->view == 1 ? x + (cw - ICON_SIZE) / 2 : x + 6;
	int iy = y + (w->view == 1 ? 9 : 1);
	int baseline = w->view == 1 ? y + 60 : iy + ICON_SIZE + 10;
	int box_w, box_h, box_x, box_y;
	int highlighted = ic->selected ||
	    (drop_hover_idx >= 0 && w == drop_hover_win &&
	    ic == &w->icons[drop_hover_idx]);

	(void)ch;
	if (w->view == 1) {
		/* bouton 48×48 relevé sur la VM : #66, #cc, blanc ; #99, #77, #33 ; fond #cc ou enfoncé */
		int bx = ix - 8, by = y + 1;

		ptk_fill(&ptk, w->win, bx, by, 48, 48, gris(0x66));
		ptk_fill(&ptk, w->win, bx + 1, by + 1, 47, 47, gris(0x33));
		ptk_fill(&ptk, w->win, bx + 1, by + 1, 46, 46, gris(0x77));
		ptk_fill(&ptk, w->win, bx + 1, by + 1, 45, 45, gris(0x99));
		ptk_fill(&ptk, w->win, bx + 1, by + 1, 44, 44, gris(0xcc));
		ptk_fill(&ptk, w->win, bx + 2, by + 2, 43, 43, ptk.white);
		ptk_fill(&ptk, w->win, bx + 3, by + 3, 41, 41, gris(highlighted ? 0x88 : 0xcc));
		ptk_fill(&ptk, w->win, bx + 47, by, 1, 1, gris(0x55));
		ptk_fill(&ptk, w->win, bx, by + 47, 1, 1, gris(0x55));
	}
	ptk_fit_text(&ptk, font, NOM_VU(ic), label, sizeof(label), maxw);
	lw = ptk_text_width(&ptk, font, label);
	box_w = lw + 6;
	box_h = font->ascent + font->descent + 2;
	box_x = ix + ICON_SIZE / 2 - box_w / 2;
	box_y = baseline - font->ascent - 1;

	if (highlighted) {
		/* Mac OS 9 : étiquette noire, texte blanc, icône assombrie */
		ptk_fill(&ptk, w->win, box_x, box_y, box_w, box_h,
		    ptk.black);
	} else if (w->is_desktop) {
		/* Sur le fond de bureau, les étiquettes ont un fond blanc. */
		/* sur le bureau : blanc translucide (relevé : #cdcdde sur le motif #63639c) */
		if (w->is_desktop)
			ptk_fill_alpha(&ptk, w->win, box_x, box_y, box_w, box_h, ptk.white, 0xae14);
		else
			ptk_fill(&ptk, w->win, box_x, box_y, box_w, box_h, ptk.white);
	}

	{
		PtkArgb *special = tableau_image(ic, 32);

		if (special != NULL) {
			ptk_draw_argb(&ptk, w->win, special, ix, iy, highlighted);
			goto icone_dessinee;
		}
	}
	if (ic->kind == 3) {
		PtkArgb *ac = trash_is_empty() ? arg_corbeille : arg_corbeille_pleine;

		if (ac != NULL) {
			ptk_draw_argb(&ptk, w->win, ac, ix, iy, highlighted);
			goto icone_dessinee;
		}
		Pixmap p = trash_is_empty() ? pix_trash : pix_trash_full;
		Pixmap m = trash_is_empty() ? pix_trash_mask :
		    pix_trash_full_mask;
		if (highlighted) {
			Pixmap dp = trash_is_empty() ? pix_trash_dim :
			    pix_trash_full_dim;
			if (dp != None)
				p = dp;
		}
		ptk_draw_pixmap(&ptk, w->win, p, m, ix, iy, ICON_SIZE,
		    ICON_SIZE);
		used_mask = m;
		if (p == None)
			ptk_icon(&ptk, w->win, ix, iy, ICON_SIZE, "trash");
	} else if (ic->kind == 2 && arg_disque != NULL) {
		ptk_draw_argb(&ptk, w->win, arg_disque, ix, iy, highlighted);
		goto icone_dessinee;
	} else if (ic->is_dir && arg_dossier != NULL) {
		/* dossier spécial de l'arborescence (Dossier Système, Extensions…) */
		const char *sp = dossier_special(ic);
		PtkArgb *ad = sp != NULL ? icone_programme(sp, 32) : NULL;

		if (ad == NULL)
			ad = arg_dossier;
		if (icone_ouverte(ic))
			ptk_draw_argb_ouverte(&ptk, w->win, ad, ix, iy, highlighted);
		else
			ptk_draw_argb(&ptk, w->win, ad, ix, iy, highlighted);
		goto icone_dessinee;
	} else if (!ic->is_dir && ic->kind != 2 && item_image(ic, 32) != NULL) {
		ptk_draw_argb(&ptk, w->win, item_image(ic, 32), ix, iy, highlighted);
		goto icone_dessinee;
	} else if (ic->kind == 2) {
		ptk_draw_pixmap(&ptk, w->win,
		    highlighted && pix_disk_dim != None ? pix_disk_dim :
		    pix_disk, pix_disk_mask, ix, iy, ICON_SIZE, ICON_SIZE);
		used_mask = pix_disk_mask;
		if (pix_disk == None)
			ptk_icon(&ptk, w->win, ix, iy, ICON_SIZE, "disk");
	} else if (ic->is_dir) {
		ptk_draw_pixmap(&ptk, w->win,
		    highlighted && pix_folder_dim != None ? pix_folder_dim :
		    pix_folder, pix_folder_mask, ix, iy, ICON_SIZE,
		    ICON_SIZE);
		used_mask = pix_folder_mask;
		if (pix_folder == None)
			ptk_icon(&ptk, w->win, ix, iy, ICON_SIZE, "folder");
	} else {
		ptk_draw_pixmap(&ptk, w->win,
		    highlighted && pix_doc_dim != None ? pix_doc_dim :
		    pix_doc, pix_doc_mask, ix, iy, ICON_SIZE, ICON_SIZE);
		used_mask = pix_doc_mask;
		if (pix_doc == None)
			ptk_icon(&ptk, w->win, ix, iy, ICON_SIZE, "doc");
	}
	if (highlighted && pix_folder_dim == None)
		dim_icon(w, ix, iy, used_mask);

icone_dessinee:
	ptk_text(&ptk, w->win, font, box_x + 3, baseline, label,
	    highlighted ? ptk.selection_text : ptk.text);
	if (rename_win == w && rename_idx >= 0 &&
	    ic == &w->icons[rename_idx]) {
		char field[256];
		int fw, fx;
		snprintf(field, sizeof(field), "%s", rename_buf);
		fw = ptk_text_width(&ptk, ptk.font_small, field) + 10;
		if (fw < CELL_W - 10)
			fw = CELL_W - 10;
		fx = x + (cw - fw) / 2;
		ptk_fill(&ptk, w->win, fx, box_y - 1, fw, box_h + 2,
		    ptk.white);
		ptk_rect(&ptk, w->win, fx, box_y - 1, fw, box_h + 2,
		    ptk.black);
		ptk_text(&ptk, w->win, ptk.font_small, fx + 4, baseline,
		    field, ptk.text);
		ptk_line(&ptk, w->win, fx + 4 + ptk_text_width(&ptk,
		    ptk.font_small, field), box_y + 1, fx + 4 +
		    ptk_text_width(&ptk, ptk.font_small, field),
		    box_y + box_h - 2, ptk.black);
	}
}

static void
short_path(const char *path, char *out, size_t outsz)
{
	size_t n = strlen(home_dir);

	if (strncmp(path, home_dir, n) == 0 &&
	    (path[n] == '\0' || path[n] == '/'))
		snprintf(out, outsz, "~%s", path + n);
	else
		snprintf(out, outsz, "%s", path);
}

static void
set_addr(FWin *w)
{
	short_path(w->path, w->addr, sizeof(w->addr));
	w->addr_len = (int)strlen(w->addr);
}

/* Gris par valeur : les mesures de la VM sont données en hexadécimal. */
static PtkColor
gris(int v)
{
	char n[8];

	snprintf(n, sizeof(n), "#%02x%02x%02x", v, v, v);
	return ptk_color(&ptk, n);
}

static void
tool_button(FWin *w, int x, int y, int glyph, int active, int pressed)
{
	/* bouton biseauté Platinum : bord #55, relief blanc/#aa ; enfoncé ou choisi : #99 */
	PtkColor fg = active ? ptk.black : gris(0x99);
	int i;

	ptk_fill(&ptk, w->win, x, y, 22, 18, pressed ? gris(0x99) : gris(0xdd));
	ptk_rect(&ptk, w->win, x, y, 22, 18, gris(0x55));
	ptk_fill(&ptk, w->win, x + 1, y + 1, 20, 1, pressed ? gris(0x77) : ptk.white);
	ptk_fill(&ptk, w->win, x + 1, y + 1, 1, 16, pressed ? gris(0x77) : ptk.white);
	ptk_fill(&ptk, w->win, x + 2, y + 16, 19, 1, pressed ? gris(0xbb) : gris(0xaa));
	ptk_fill(&ptk, w->win, x + 20, y + 2, 1, 15, pressed ? gris(0xbb) : gris(0xaa));
	if (glyph == 0) { /* précédent */
		for (i = 0; i < 5; i++)
			ptk_fill(&ptk, w->win, x + 8 + i, y + 9 - i, 1, 1 + 2 * i, fg);
	} else if (glyph == 1) { /* suivant */
		for (i = 0; i < 5; i++)
			ptk_fill(&ptk, w->win, x + 13 - i, y + 9 - i, 1, 1 + 2 * i, fg);
	} else if (glyph == 2) { /* dossier parent : flèche vers le haut */
		for (i = 0; i < 5; i++)
			ptk_fill(&ptk, w->win, x + 11 - i, y + 4 + i, 1 + 2 * i, 1, fg);
		ptk_fill(&ptk, w->win, x + 9, y + 9, 5, 5, fg);
	} else if (glyph == 3) { /* icônes : 4 carrés */
		ptk_fill(&ptk, w->win, x + 5, y + 4, 5, 5, fg);
		ptk_fill(&ptk, w->win, x + 12, y + 4, 5, 5, fg);
		ptk_fill(&ptk, w->win, x + 5, y + 11, 5, 5, fg);
		ptk_fill(&ptk, w->win, x + 12, y + 11, 5, 5, fg);
	} else if (glyph == 4) { /* boutons : un carré */
		ptk_rect(&ptk, w->win, x + 5, y + 4, 12, 11, fg);
		ptk_fill(&ptk, w->win, x + 8, y + 7, 6, 5, fg);
	} else if (glyph == 5) { /* liste */
		for (i = 0; i < 4; i++) {
			ptk_fill(&ptk, w->win, x + 5, y + 4 + i * 4,
			    2, 2, fg);
			ptk_line(&ptk, w->win, x + 9, y + 4 + i * 4,
			    x + 17, y + 4 + i * 4, fg);
		}
	} else if (glyph == 6) { /* menu de chemins */
		for (i = 0; i < 4; i++)
			ptk_line(&ptk, w->win, x + 8 - i, y + 7 + i,
			    x + 8 + i, y + 7 + i, fg);
	}
}

static void
draw_toolbar(FWin *w)
{
	int addr_x = 80;
	int addr_w = w->w - 180;
	int i, x;

	tool_button(w, 4, 4, 0, w->hist_pos > 0, 0);
	tool_button(w, 28, 4, 1, w->hist_pos < w->hist_n - 1, 0);
	{
		char parent[PATH_MAX];
		snprintf(parent, sizeof(parent), "%s", w->path);
		{
			char *slash = strrchr(parent, '/');
			if (slash != NULL && slash != parent)
				*slash = '\0';
			else if (slash == parent)
				parent[1] = '\0';
		}
		tool_button(w, 52, 4, 2, strcmp(parent, w->path) != 0, 0);
	}

	if (addr_w < 100)
		addr_w = 100;
	ptk_fill(&ptk, w->win, addr_x, 4, addr_w, 18, ptk.white);
	ptk_rect(&ptk, w->win, addr_x, 4, addr_w, 18, ptk.black);
	ptk_line(&ptk, w->win, addr_x + 1, 5, addr_x + addr_w - 2, 5,
	    ptk.shadow);
	{
		char shown[PATH_MAX];
		int maxw = addr_w - 8;
		int tw;
		ptk_fit_text(&ptk, ptk.font_small, w->addr, shown,
		    sizeof(shown), maxw);
		tw = ptk_text_width(&ptk, ptk.font_small, shown);
		ptk_text(&ptk, w->win, ptk.font_small, addr_x + 4, 16,
		    shown, ptk.text);
		if (w->addr_focus)
			ptk_line(&ptk, w->win, addr_x + 4 + tw, 7,
			    addr_x + 4 + tw, 17, ptk.black);
	}

	/* bouton de chemins */
	tool_button(w, w->w - 96, 4, 6, 1, 0);
	/* boutons de présentation */
	tool_button(w, w->w - 70, 4, 3, 1, w->view == 0);
	tool_button(w, w->w - 46, 4, 4, 1, w->view == 1);
	tool_button(w, w->w - 22, 4, 5, 1, w->view == 2);
	(void)i;
	(void)x;
}

static void
draw_info(FWin *w)
{
	char status[128];
	struct statvfs sv;

	if (statvfs(w->path, &sv) == 0) {
		double gb = (double)sv.f_bavail * (double)sv.f_frsize /
		    (1024.0 * 1024.0 * 1024.0);
		snprintf(status, sizeof(status),
		    "%d %s, %.2f Go disponibles", w->n, w->n > 1 ? "éléments" : "élément", gb);
	} else {
		snprintf(status, sizeof(status), "%d %s", w->n, w->n > 1 ? "éléments" : "élément");
	}
	ptk_text(&ptk, w->win, ptk.font_small,
	    (w->w - ptk_text_width(&ptk, ptk.font_small, status)) / 2,
	    TOOLBAR_H + 14, status, fen_active(w) ? ptk.text : ptk.text_dim);
}

static int
cell_w(FWin *w)
{
	return CELL_W;
}

static int
cell_h(FWin *w)
{
	return CELL_H;
}

static int
content_cols(FWin *w, int sb)
{
	int cw = w->w - BARRE;

	(void)sb;
	int c = (cw - 20) / cell_w(w);

	return c > 0 ? c : 1;
}

/* Étendue du contenu et de la vue, en pixels : suit les vraies positions des icônes. */
static int
vue_h(const FWin *w)
{
	return w->h - CONTENT_TOP - BARRE;
}

static int
vue_w(const FWin *w)
{
	return w->w - BARRE;
}

static int
contenu_h(FWin *w)
{
	int i, m = 0;

	if (w->view == 2)
		return w->n * LISTE_RANG + LISTE_ENTETE;
	if (w->view == 1) {
		int cols = content_cols(w, 1);

		return (w->n + cols - 1) / cols * cell_h(w) + 3;
	}
	for (i = 0; i < w->n; i++) {
		int b = w->icons[i].y - CONTENT_TOP + cell_h(w);

		if (b > m)
			m = b;
	}
	return m;
}

static int
contenu_w(FWin *w)
{
	int i, m = 0;

	if (w->view == 2)
		return vue_w(w) > LISTE_LARGEUR ? vue_w(w) : LISTE_LARGEUR;
	if (w->view != 0)
		return vue_w(w);
	for (i = 0; i < w->n; i++) {
		int b = w->icons[i].x + cell_w(w);

		if (b > m)
			m = b;
	}
	return m;
}

static int
max_scroll(FWin *w)
{
	int m = contenu_h(w) - vue_h(w);

	return m > 0 ? m : 0;
}

static int
max_scroll_x(FWin *w)
{
	int m = contenu_w(w) - vue_w(w);

	return m > 0 ? m : 0;
}

static int
has_scrollbar(FWin *w)
{
	return !w->is_desktop && max_scroll(w) > 0;
}

static int
has_hbar(FWin *w)
{
	return !w->is_desktop && max_scroll_x(w) > 0;
}

static long long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
scroll_by(FWin *w, int delta)
{
	int old = w->scroll;

	w->scroll += delta;
	if (w->scroll < 0)
		w->scroll = 0;
	if (w->scroll > max_scroll(w))
		w->scroll = max_scroll(w);
	if (w->scroll != old)
		draw_window(w);
}

static void
scroll_x_by(FWin *w, int delta)
{
	int old = w->scroll_x;

	w->scroll_x += delta;
	if (w->scroll_x < 0)
		w->scroll_x = 0;
	if (w->scroll_x > max_scroll_x(w))
		w->scroll_x = max_scroll_x(w);
	if (w->scroll_x != old)
		draw_window(w);
}

/* Smart Scrolling : maintien sur une flèche = défilement continu. */
static void
scroll_repeat_stop(void)
{
	if (sb_repeat_grabbed)
		XUngrabPointer(dpy, CurrentTime);
	sb_repeat_grabbed = 0;
	sb_repeat_win = NULL;
}

static void
finder_scrollbar(FWin *w, int axe, PtkScrollbar *s)
{
	memset(s, 0, sizeof(*s));
	s->horizontal = axe;
	s->active = fen_active(w);
	s->pressed = sb_repeat_win == w && sb_drag_axe == axe ? sb_repeat_zone : 0;
	if (axe) {
		s->y = w->h - BARRE;
		s->w = w->w - BARRE;
		s->h = BARRE;
		s->value = w->scroll_x;
		s->max = max_scroll_x(w);
		s->page = vue_w(w);
	} else {
		s->x = w->w - BARRE;
		s->y = CONTENT_TOP;
		s->w = BARRE;
		s->h = vue_h(w);
		s->value = w->scroll;
		s->max = max_scroll(w);
		s->page = vue_h(w);
	}
}

/* pouce le long de l'axe (0 vertical, 1 horizontal) : position et longueur */
static void
sb_pouce(FWin *w, int axe, int *pos, int *lon)
{
	PtkScrollbar s;

	finder_scrollbar(w, axe, &s);
	ptk_scrollbar_thumb(&s, pos, lon);
}

/* zones des barres : flèches et rail de part et d'autre du pouce */
enum { SB_RIEN, SB_RECUL, SB_AVANCE, SB_PAGE_RECUL, SB_PAGE_AVANCE, SB_POUCE };

static int
sb_zone(FWin *w, int x, int y, int *axe)
{
	PtkScrollbar s;
	int a;

	if (y < CONTENT_TOP)
		return SB_RIEN;
	if (x >= w->w - BARRE && y < w->h - BARRE)
		a = 0;
	else if (y >= w->h - BARRE && x < w->w - BARRE)
		a = 1;
	else
		return SB_RIEN;
	*axe = a;
	if (!(a ? has_hbar(w) : has_scrollbar(w)))
		return SB_RIEN;
	finder_scrollbar(w, a, &s);
	return ptk_scrollbar_zone(&s, x, y);
}

/* un pas : flèche = 16 px (une rangée en liste), rail = la vue moins 16 px */
static void
sb_agir(FWin *w, int axe, int zone)
{
	int pas = (w->view == 2 && axe == 0) ? LISTE_RANG : 16;
	int page = (axe ? vue_w(w) : vue_h(w)) - 16;
	int d = zone == SB_RECUL ? -pas : zone == SB_AVANCE ? pas :
	    zone == SB_PAGE_RECUL ? -page : page;

	if (axe)
		scroll_x_by(w, d);
	else
		scroll_by(w, d);
}

static void
sb_axe(FWin *w, int axe)
{
	PtkScrollbar s;

	finder_scrollbar(w, axe, &s);
	ptk_scrollbar_draw(&ptk, w->win, &s);
}

/*
 * Barres de défilement Platinum, relevées sur Mac OS 9.2 : trait noir, rail, puis les deux
 * flèches groupées au bout. Sans rien à faire défiler, ou dans une fenêtre inactive :
 * rail #ee, traits #55/#88, pas de pouce.
 */
static void
draw_scrollbar(FWin *w)
{
	int actif = fen_active(w);
	PtkColor trait = actif ? ptk.black : gris(0x55);

	ptk_fill(&ptk, w->win, w->w - BARRE, CONTENT_TOP, 1, vue_h(w), trait);
	ptk_fill(&ptk, w->win, 0, w->h - BARRE, w->w, 1, trait);
	/* coin sous la poignée de taille : jamais peint sinon, le tampon y
	 * gardait des pixels indéfinis, recopiés à l'écran à chaque dessin */
	ptk_fill(&ptk, w->win, w->w - BARRE + 1, w->h - BARRE + 1, BARRE - 1, BARRE - 1,
	    ptk.white);
	if (actif && !has_scrollbar(w))
		ptk_fill(&ptk, w->win, w->w - BARRE + 1, CONTENT_TOP - 1, 14, 1, gris(0x55));
	sb_axe(w, 0);
	sb_axe(w, 1);
}

/* triangle : celui de la Toolbox (ptk_triangle), rangée de départ +3 */
static void
liste_triangle(FWin *w, int x, int y, int ouvert)
{
	ptk_triangle(&ptk, w->win, x, y, ouvert, 0);
}

static void
liste_contenu(FWin *w)
{
	int bord = w->w - BARRE, x = 0, c, i;
	XftFont *f = ptk.font_views;

	/* colonnes : #dd pour celle du tri, #ee pour les autres, jusqu'en bas */
	for (c = 0; c < 4; c++) {
		int l = liste_cols[c].w ? liste_cols[c].w : bord - x;

		if (l > 0)
			ptk_fill(&ptk, w->win, x, LISTE_Y0, l, w->h - LISTE_Y0, gris(c == w->tri ? 0xdd : 0xee));
		x += l;
	}
	for (i = w->scroll / LISTE_RANG; i < w->n; i++) {
		Icon *ic = &w->icons[i];
		int y = LISTE_Y0 + i * LISTE_RANG - w->scroll, cx, indent = 16 * ic->niveau;
		char t[300], d[64];
		PtkArgb *a = item_image(ic, 16);

		if (y > w->h)
			break;
		ptk_fill(&ptk, w->win, 0, y + LISTE_RANG - 1, bord, 1, ptk.white);
		if (ic->is_dir)
			liste_triangle(w, 8 + indent, y + 3, ic->ouvert);
		if (a != NULL && icone_ouverte(ic))
			ptk_draw_argb_ouverte(&ptk, w->win, a, 21 + indent, y + 1, ic->selected);
		else if (a != NULL)
			ptk_draw_argb(&ptk, w->win, a, 21 + indent, y + 1, ic->selected ||
			    (w == drop_hover_win && i == drop_hover_idx));
		ptk_fit_text(&ptk, f, NOM_VU(ic), t, sizeof(t), 214 - 46 - indent);
		cx = 42 + indent;
		if (ic->selected) {
			int tw = ptk_text_width(&ptk, f, t);

			ptk_fill(&ptk, w->win, cx - 2, y + 2, tw + 4, 14, ptk.black);
		}
		ptk_text(&ptk, w->win, ic->is_alias ? ptk.font_views_italic : f, cx, y + 13, t,
		    ic->selected ? ptk.white : ptk.text);
		ptk_fuseau_suivre();
		strftime(d, sizeof(d), "%a %e %b %Y, %H:%M", localtime(&ic->date));
		ptk_fit_text(&ptk, f, d, t, sizeof(t), 160);
		ptk_text(&ptk, w->win, f, 222, y + 13, t, ptk.text);
		if (ic->is_dir)
			snprintf(t, sizeof(t), "--");
		else if (ic->taille < 1024 * 1024)
			snprintf(t, sizeof(t), "%lld Ko", (ic->taille + 1023) / 1024);
		else
			snprintf(t, sizeof(t), "%.1f Mo", (double)ic->taille / (1024 * 1024));
		ptk_text(&ptk, w->win, f, 379 + 66 - 6 - ptk_text_width(&ptk, f, t), y + 13, t, ptk.text);
		ptk_text(&ptk, w->win, f, 379 + 66 + 6, y + 13, liste_type(ic), ptk.text);
	}
}

/* en-têtes : trait du haut, blanc, fond, #88/#aa puis sombre ; colonne du tri foncée */
static void
liste_entetes(FWin *w)
{
	int bord = w->w - BARRE, x = 0, c, Y = CONTENT_TOP - 1;
	int bx = bord - 15;

	for (c = 0; c < 4; c++) {
		int l = liste_cols[c].w ? liste_cols[c].w : bx - x;
		int tri = c == w->tri;
		int tx = c == 0 ? 43 : x + 8;

		if (l <= 0)
			continue;
		ptk_fill(&ptk, w->win, x, Y, l, 21, gris(tri ? 0x88 : 0xcc));
		ptk_fill(&ptk, w->win, x, Y, l, 1, gris(tri ? 0x11 : 0x66));
		ptk_fill(&ptk, w->win, x, Y + 1, l, 1, tri ? gris(0x55) : ptk.white);
		ptk_fill(&ptk, w->win, x, Y + 19, l, 1, gris(tri ? 0xaa : 0x88));
		ptk_fill(&ptk, w->win, x, Y + 20, l, 1, gris(tri ? 0x44 : 0x33));
		if (tri) {
			ptk_fill(&ptk, w->win, x, Y, 1, 21, gris(0x11));
			ptk_fill(&ptk, w->win, x + 1, Y + 1, 1, 19, gris(0x55));
			ptk_fill(&ptk, w->win, x + l - 2, Y + 1, 1, 19, gris(0xaa));
			ptk_fill(&ptk, w->win, x + l - 1, Y, 1, 21, gris(0x44));
		} else {
			ptk_fill(&ptk, w->win, x, Y + 1, 1, 19, ptk.white);
			ptk_fill(&ptk, w->win, x + l - 1, Y, 1, 21, gris(0x66));
		}
		ptk_text(&ptk, w->win, ptk.font_small, tx, Y + 14, liste_cols[c].nom, ptk.text);
		x += l;
	}
}

/* bouton du sens de tri, fixé au bout visible de l'en-tête (au-dessus de
 * l'ascenseur), même quand la liste défile : quatre barres qui s'élargissent */
static void
liste_bouton_tri(FWin *w)
{
	int Y = CONTENT_TOP - 1, bx = w->w - BARRE - 15, r;

	ptk_fill(&ptk, w->win, bx, Y, 15, 21, gris(0xcc));
	ptk_fill(&ptk, w->win, bx, Y, 15, 1, gris(0x66));
	ptk_fill(&ptk, w->win, bx, Y + 20, 15, 1, gris(0x33));
	ptk_fill(&ptk, w->win, bx, Y, 1, 21, gris(0x66));
	ptk_fill(&ptk, w->win, bx + 1, Y + 1, 13, 1, ptk.white);
	ptk_fill(&ptk, w->win, bx + 1, Y + 1, 1, 18, ptk.white);
	ptk_fill(&ptk, w->win, bx + 14, Y + 1, 1, 19, gris(0x88));
	ptk_fill(&ptk, w->win, bx + 1, Y + 19, 14, 1, gris(0x88));
	for (r = 0; r < 4; r++) {
		int l = 2 + 2 * r;
		int yy = w->tri_inv ? Y + 12 - 2 * r : Y + 6 + 2 * r;

		ptk_fill(&ptk, w->win, bx + 7 - r - 1, yy, l, 1, ptk.black);
	}
}

/* Vue liste plus large que la fenêtre : rangées ou en-têtes dessinés à la
 * largeur des colonnes dans un pixmap, puis la partie visible recopiée avec
 * le défilement horizontal ; le dessin de la liste reste en coordonnées de
 * colonnes. */
static void
liste_large(FWin *w, void (*dessin)(FWin *), int y0, int h)
{
	int L = contenu_w(w), vw = vue_w(w), sw = w->w;
	Drawable cible = w->win;
	Pixmap pm;

	if (h <= 0)
		return;
	if (L <= vw && w->scroll_x == 0) {
		dessin(w);
		return;
	}
	pm = XCreatePixmap(dpy, cible, (unsigned)(L + BARRE), (unsigned)w->h,
	    (unsigned)DefaultDepth(dpy, screen));
	w->win = pm;
	w->w = L + BARRE;
	ptk_clip(&ptk, 0, y0, L, h);
	dessin(w);
	ptk_unclip(&ptk);
	w->win = cible;
	w->w = sw;
	XCopyArea(dpy, pm, cible, ptk.gc, w->scroll_x, y0, (unsigned)vw, (unsigned)h, 0, y0);
	XFreePixmap(dpy, pm);
}

static void
draw_window(FWin *w)
{
	int i, cols, x, y;
	Window reel;

	if (!w->is_desktop && !w->is_info && strcmp(w->chemin_pose, w->path) != 0) {
		/* dit au WM que cette fenêtre a une icône de titre glissable */
		snprintf(w->chemin_pose, sizeof(w->chemin_pose), "%s", w->path);
		set_utf8(w->win, XInternAtom(dpy, "_PLATINE_CHEMIN", False), w->path);
	}
	if (!w->is_info)
		update_menu(w);
	/* la barre affiche le menu de la fenêtre de dossier : il suit la sélection du bureau */
	if (w->is_desktop && bureau_devant) {
		FWin *o;

		for (o = wins; o != NULL; o = o->next)
			if (!o->is_desktop && !o->is_info)
				update_menu(o);
	}
	if (w->is_info) {
		draw_info_window(w);
		return;
	}
	if (w->is_desktop && fond_bureau != None) {
		/* fond et icônes composés en mémoire, puis affichés d'un bloc */
		if (w->tampon == None)
			w->tampon = XCreatePixmap(dpy, w->win, (unsigned)w->w, (unsigned)w->h,
			    (unsigned)DefaultDepth(dpy, screen));
		w->tampon_w = w->w;
		w->tampon_h = w->h;
		XCopyArea(dpy, fond_bureau, w->tampon, ptk.gc, 0, 0, (unsigned)w->w, (unsigned)w->h, 0, 0);
		reel = w->win;
		w->win = w->tampon;
		for (i = 0; i < w->n; i++)
			draw_icon(w, &w->icons[i], w->icons[i].x, w->icons[i].y);
		w->win = reel;
		XCopyArea(dpy, w->tampon, reel, ptk.gc, 0, 0, (unsigned)w->w, (unsigned)w->h, 0, 0);
		mq_retracer(w);
		XFlush(dpy);
		return;
	}
	if (w->is_desktop) {
		XClearWindow(dpy, w->win);
		for (i = 0; i < w->n; i++)
			draw_icon(w, &w->icons[i], w->icons[i].x,
			    w->icons[i].y);
		XFlush(dpy);
		return;
	}

	/* la fenêtre a pu grandir, ou des icônes partir : le défilement reste dans les limites */
	if (w->scroll > max_scroll(w))
		w->scroll = max_scroll(w);
	if (w->scroll_x > max_scroll_x(w))
		w->scroll_x = max_scroll_x(w);
	/* tout se dessine en mémoire, puis d'un bloc à l'écran : pas de scintillement */
	if (w->tampon == None || w->tampon_w != w->w || w->tampon_h != w->h) {
		if (w->tampon != None)
			XFreePixmap(dpy, w->tampon);
		w->tampon = XCreatePixmap(dpy, w->win, (unsigned)w->w, (unsigned)w->h,
		    (unsigned)DefaultDepth(dpy, screen));
		w->tampon_w = w->w;
		w->tampon_h = w->h;
	}
	reel = w->win;
	w->win = w->tampon;
	ptk_fill(&ptk, w->win, 0, CONTENT_TOP, w->w - BARRE, w->h - CONTENT_TOP - BARRE,
	    ptk.white);
	ptk_clip(&ptk, 0, CONTENT_TOP, w->w - BARRE, w->h - CONTENT_TOP - BARRE);

	if (w->view == 2) {
		liste_large(w, liste_contenu, CONTENT_TOP, w->h - CONTENT_TOP - BARRE);
	} else if (w->view == 1) {
		cols = content_cols(w, has_scrollbar(w));
		for (i = 0; i < w->n; i++) {
			int row = i / cols, col = i % cols;
			x = 10 + col * cell_w(w);
			y = CONTENT_TOP + 3 + row * cell_h(w) - w->scroll;
			if (y + cell_h(w) < CONTENT_TOP || y > w->h)
				continue;
			draw_icon(w, &w->icons[i], x, y);
		}
	} else {
		for (i = 0; i < w->n; i++) {
			y = w->icons[i].y - w->scroll;
			if (y + cell_h(w) < CONTENT_TOP || y > w->h)
				continue;
			draw_icon(w, &w->icons[i], w->icons[i].x - w->scroll_x, y);
		}
	}
	ptk_unclip(&ptk);
	/* après le contenu : une icône masquée ne respecte pas le découpage */
	/* bandeau en relief (VM : blanc en haut et à gauche, #aa en bas et à droite),
	 * puis trait noir et zone blanche jusqu'aux barres de défilement */
	{
		int H = CONTENT_TOP - 1;
		int actif = fen_active(w);

		ptk_fill(&ptk, w->win, 0, 0, w->w, H, actif ? gris(0xdd) : gris(0xee));
		if (actif) {
			ptk_fill(&ptk, w->win, 0, 0, w->w - 1, 1, ptk.white);
			ptk_fill(&ptk, w->win, 0, 0, 1, H - 1, ptk.white);
			ptk_fill(&ptk, w->win, w->w - 1, 1, 1, H, gris(0xaa));
			ptk_fill(&ptk, w->win, 1, H - 1, w->w - 1, 1, gris(0xaa));
		}
		ptk_fill(&ptk, w->win, 0, H, w->w, 1, actif ? ptk.black : gris(0x55));
	}
	if (w->view == 2) {
		liste_large(w, liste_entetes, CONTENT_TOP - 1, LISTE_ENTETE + 1);
		liste_bouton_tri(w);
	}
	draw_toolbar(w);
	draw_info(w);
	draw_scrollbar(w);
	w->win = reel;
	XCopyArea(dpy, w->tampon, reel, ptk.gc, 0, 0, (unsigned)w->w, (unsigned)w->h, 0, 0);
	mq_retracer(w);
	XFlush(dpy);
}

/* comme sur Mac : seuls l'image et le nom réagissent, pas toute la case de la grille */
static int
dans_icone(FWin *w, Icon *ic, int x, int y, int px, int py)
{
	int cw = cell_w(w), tw;
	int ix = w->is_desktop ? x : w->view == 1 ? x + (cw - ICON_SIZE) / 2 : x + 6;
	char t[256];

	if (px >= ix && px < ix + ICON_SIZE && py > y && py <= y + 1 + ICON_SIZE)
		return 1;
	ptk_fit_text(&ptk, ptk.font_views, NOM_VU(ic), t, sizeof(t), label_max(w));
	tw = ptk_text_width(&ptk, ptk.font_views, t);
	return py > y + ICON_SIZE && py <= y + ICON_SIZE + 14 &&
	    px >= ix + ICON_SIZE / 2 - tw / 2 - 3 &&
	    px <= ix + ICON_SIZE / 2 + tw / 2 + 3;
}

/* Lasso : comme sous OS 9, une icône est prise dès que le rectangle touche son image ou son nom. Renvoie 1 si la sélection change. */
static int
mq_selection(FWin *w)
{
	int mx, my, mw, mh, i, first = -1, change = 0, cw = cell_w(w), ch = cell_h(w);

	mq_rect(&mx, &my, &mw, &mh);
	for (i = 0; i < w->n; i++) {
		Icon *ic = &w->icons[i];
		char t[256];
		int x, y, ix, iy, ly, lw, pris;

		if (w->is_desktop) {
			x = ic->x;
			y = ic->y;
		} else if (w->view == 1) {
			int cols = content_cols(w, has_scrollbar(w));

			x = 10 + (i % cols) * cw;
			y = CONTENT_TOP + 3 + (i / cols) * ch - w->scroll;
		} else {
			x = ic->x - w->scroll_x;
			y = ic->y - w->scroll;
		}
		ix = w->is_desktop ? x : w->view == 1 ? x + (cw - ICON_SIZE) / 2 : x + 6;
		iy = y + (!w->is_desktop && w->view == 1 ? 9 : 1);
		ly = iy + ICON_SIZE + (!w->is_desktop && w->view == 1 ? 8 : 0);
		ptk_fit_text(&ptk, ptk.font_views, NOM_VU(ic), t, sizeof(t), label_max(w));
		lw = ptk_text_width(&ptk, ptk.font_views, t) + 6;
		pris = (ix <= mx + mw && ix + ICON_SIZE > mx && iy <= my + mh && iy + ICON_SIZE > my) ||
		    (ix + ICON_SIZE / 2 - lw / 2 <= mx + mw &&
		    ix + ICON_SIZE / 2 + lw / 2 > mx && ly <= my + mh && ly + 14 > my);
		if (ic->selected != pris) {
			ic->selected = pris;
			change = 1;
		}
		if (pris && first < 0)
			first = i;
	}
	w->selected = first;
	return change;
}

/* pendant un dépôt, les icônes glissées (sélectionnées de drag_win) ont déjà pris
 * leur nouvelle place : elles ne sont pas la cible, celle qui est dessous l'est */
static int icon_at_sauter_glisse;

static int
icon_at(FWin *w, int px, int py, int *out)
{
	int i, cols, x, y;
	int sauter = icon_at_sauter_glisse && w == drag_win;

	if (w->is_desktop) {
		for (i = 0; i < w->n; i++) {
			if (sauter && w->icons[i].selected)
				continue;
			x = w->icons[i].x;
			y = w->icons[i].y;
			if (dans_icone(w, &w->icons[i], x, y, px, py)) {
				*out = i;
				return 1;
			}
		}
		return 0;
	}
	if (w->view == 2) {
		int row = (py - LISTE_Y0 + w->scroll) / LISTE_RANG;
		if (py < LISTE_Y0 || px < 0 || px >= w->w - BARRE ||
		    row < 0 || row >= w->n)
			return 0;
		*out = row;
		return 1;
	}
	if (w->view == 0) {
		for (i = 0; i < w->n; i++) {
			if (sauter && w->icons[i].selected)
				continue;
			x = w->icons[i].x - w->scroll_x;
			y = w->icons[i].y - w->scroll;
			if (dans_icone(w, &w->icons[i], x, y, px, py)) {
				*out = i;
				return 1;
			}
		}
		return 0;
	}
	cols = content_cols(w, has_scrollbar(w));
	for (i = 0; i < w->n; i++) {
		int row = i / cols, col = i % cols;
		x = 10 + col * cell_w(w);
		y = CONTENT_TOP + 3 + row * cell_h(w) - w->scroll;
		/* le bouton et son nom ; le reste de la case ne réagit pas */
		{
			int bx = x + (cell_w(w) - 48) / 2;

			if ((px >= bx && px < bx + 48 && py > y && py <= y + 48) ||
			    (py > y + 48 && py <= y + 62 && px >= x && px < x + cell_w(w))) {
				*out = i;
				return 1;
			}
		}
	}
	return 0;
}

static int
programme_trouve(const char *nom)
{
	const char *c = getenv("PATH");
	char f[PATH_MAX];

	/* comme execlp : un nom qui contient un slash est un chemin, pas une recherche dans PATH */
	if (strchr(nom, '/') != NULL)
		return access(nom, X_OK) == 0;
	while (c != NULL && *c != '\0') {
		size_t d = strcspn(c, ":");

		snprintf(f, sizeof(f), "%.*s/%s", (int)d, c, nom);
		if (access(f, X_OK) == 0)
			return 1;
		c += d;
		if (*c == ':')
			c++;
	}
	return 0;
}

/* Éteindre / Redémarrer : le WM ferme les applications puis éteint (1) ou redémarre (2) */
static void
demander_arret(long mode)
{
	XEvent e;

	save_state();
	memset(&e, 0, sizeof(e));
	e.xclient.type = ClientMessage;
	e.xclient.window = root;
	e.xclient.message_type = XInternAtom(dpy, "_PLATINE_ARRET", False);
	e.xclient.format = 32;
	e.xclient.data.l[0] = mode;
	XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
	XFlush(dpy);
}

/* le gestionnaire de fenêtres affiche ce nom dans la barre jusqu'à l'arrivée de la fenêtre */
static void
annoncer_lancement(const char *nom)
{
	XEvent e;
	Atom a = XInternAtom(dpy, "_PLATINE_LANCE", False);
	char t[64];

	snprintf(t, sizeof(t), "%s", nom);
	if (t[0] >= 'a' && t[0] <= 'z')
		t[0] = (char)(t[0] - 'a' + 'A');
	set_utf8(root, a, t);
	memset(&e, 0, sizeof(e));
	e.xclient.type = ClientMessage;
	e.xclient.window = root;
	e.xclient.message_type = a;
	e.xclient.format = 32;
	XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
	XFlush(dpy);
}

/* premiers octets sans zéro ni caractère de contrôle autre qu'espacement : texte */
static int
est_texte(const char *path)
{
	unsigned char b[512];
	FILE *f = fopen(path, "rb");
	size_t n, i;

	if (f == NULL)
		return 0;
	n = fread(b, 1, sizeof(b), f);
	fclose(f);
	if (n == 0)
		return 0;
	for (i = 0; i < n; i++)
		if (b[i] == 0 || (b[i] < 32 && b[i] != '\n' && b[i] != '\r' && b[i] != '\t'))
			return 0;
	return 1;
}

static void
launch_file(const char *path)
{
	const char *ext = strrchr(path, '.');
	const char *prog = NULL;
	char bin[PATH_MAX] = "";
	pid_t pid;

	/* fichier exécutable (les éléments du dossier Tableaux de bord…) : il se lance lui-même */
	if (access(path, X_OK) == 0) {
		const char *nom = strrchr(path, '/');

		if (zoom_src_ok) {
			ptk_zoom_rects(&ptk, zoom_sx, zoom_sy, zoom_st, zoom_st, 0, 0, sw, sh);
			zoom_src_ok = 0;
		}
		annoncer_lancement(nom != NULL ? nom + 1 : path);
		pid = fork();
		if (pid == 0) {
			setenv("PLATINE_DOCUMENT", path, 1);	/* icône de titre de l'application */
			execl(path, path, (char *)NULL);
			_exit(127);
		}
		return;
	}
	/* base de bureau : le programme installé qui déclare ce type de document
	 * (identité embarquée), comme le BNDL d'une application Mac OS 9 */
	if (ext != NULL && ptk_programme_pour(ext + 1, bin, sizeof(bin))) {
		prog = bin;
		ext = NULL;
	}
	/* sans extension (« Lisez-moi ») : un texte s'ouvre avec le programme de
	 * texte, comme un document non typé avec SimpleText sous Mac OS 9 */
	if (ext == NULL && prog == NULL && est_texte(path)) {
		if (ptk_programme_pour("txt", bin, sizeof(bin)))
			prog = bin;
		else
			ext = ".txt";
	}
	if (ext != NULL) {
		ext++;
		if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0 ||
		    strcasecmp(ext, "c") == 0 || strcasecmp(ext, "h") == 0 ||
		    strcasecmp(ext, "sh") == 0 || strcasecmp(ext, "conf") == 0 ||
		    strcasecmp(ext, "log") == 0) {
			char self[PATH_SAFE];
			ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);

			if (n > 0) {
				char *slash;

				self[n] = '\0';
				slash = strrchr(self, '/');
				if (slash != NULL) {
					*slash = '\0';
					snprintf(bin, sizeof(bin), "%s/platine-textedit", self);
				}
			}
			if (n <= 0 || bin[0] == '\0')
				snprintf(bin, sizeof(bin), "platine-textedit");
			prog = bin;
		} else if (strcasecmp(ext, "png") == 0 ||
		    strcasecmp(ext, "jpg") == 0 ||
		    strcasecmp(ext, "jpeg") == 0 ||
		    strcasecmp(ext, "gif") == 0)
			prog = "gpicview";
		else if (strcasecmp(ext, "mp3") == 0 ||
		    strcasecmp(ext, "wav") == 0 ||
		    strcasecmp(ext, "ogg") == 0 ||
		    strcasecmp(ext, "flac") == 0 ||
		    strcasecmp(ext, "mp4") == 0 ||
		    strcasecmp(ext, "mkv") == 0 ||
		    strcasecmp(ext, "avi") == 0)
			prog = "mpv";
		else if (strcasecmp(ext, "pdf") == 0)
			prog = "mupdf";
	}
	if (prog == NULL)
		prog = "xdg-open";
	if (programme_trouve(prog)) {
		/* comme Mac OS 9 : rectangles de l'icône à l'écran entier, puis le nom seul dans la barre */
		if (zoom_src_ok) {
			ptk_zoom_rects(&ptk, zoom_sx, zoom_sy, zoom_st, zoom_st, 0, 0, sw, sh);
			zoom_src_ok = 0;
		}
		if (strcmp(prog, "xdg-open") != 0) {
			const char *b = strrchr(prog, '/');

			annoncer_lancement(b != NULL ? b + 1 : prog);
		}
	} else {
		zoom_src_ok = 0;
		ptk_son(&ptk, "lerr");
		return;
	}
	pid = fork();
	if (pid == 0) {
		execlp(prog, prog, path, (char *)NULL);
		_exit(127);
	}
}

static void
open_selected(FWin *w)
{
	Icon *ic;

	if (w == NULL || w->selected < 0 || w->selected >= w->n)
		return;
	ic = &w->icons[w->selected];
	{
		/* un alias Platine s'ouvre vers sa cible ; une cible manquante
		 * déclenche l'alerte, comme sous Mac OS 9 */
		char cible[PATH_MAX];

		if (alias_lire(ic->path, cible, sizeof(cible))) {
			struct stat st;

			if (stat(cible, &st) != 0) {
				char msg[600];

				snprintf(msg, sizeof(msg),
				    "L'élément d'origine de « %s » est\n"
				    "introuvable. Il a peut-être été déplacé\n"
				    "ou supprimé.", ic->name);
				ptk_son(&ptk, "lerr");
				finder_alert(msg, NULL, "OK", None, None);
				refresh_window(w);
				return;
			}
			if (S_ISDIR(st.st_mode)) {
				zoom_source_de(w, w->selected);
				open_path(cible);
				zoom_src_ok = 0;
				for (FWin *o = wins; o != NULL; o = o->next)
					if (!o->is_info)
						draw_window(o);
			} else {
				zoom_source_de(w, w->selected);
				launch_file(cible);
				zoom_src_ok = 0;
			}
			return;
		}
	}
	if (ic->is_dir) {
		/* le rectangle de départ des zooms d'ouverture : l'icône à l'écran */
		zoom_source_de(w, w->selected);
		open_path(ic->path);
		zoom_src_ok = 0;
		/* l'icône du dossier passe à son aspect « ouvert » */
		{
			FWin *o;

			for (o = wins; o != NULL; o = o->next)
				if (!o->is_info)
					draw_window(o);
		}
	} else {
		zoom_source_de(w, w->selected);
		launch_file(ic->path);
		zoom_src_ok = 0;
	}
}

static void
new_folder(FWin *w)
{
	ptk_son(&ptk, "fnew");
	char path[PATH_MAX], name[256];
	int i;

	if (w == NULL || w->is_info)
		return;
	for (i = 1; i < 1000; i++) {
		if (i == 1)
			snprintf(name, sizeof(name), "Nouveau dossier");
		else
			snprintf(name, sizeof(name), "Nouveau dossier %d", i);
		join_path(path, sizeof(path), w->path, name);
		if (mkdir(path, 0777) == 0) {
			refresh_window(w);
			return;
		}
		if (errno != EEXIST)
			return;
	}
}

static int
remove_tree(const char *path)
{
	struct stat st;
	DIR *d;
	struct dirent *de;
	int rc = 0;

	if (lstat(path, &st) != 0)
		return -1;
	if (!S_ISDIR(st.st_mode))
		return unlink(path);
	d = opendir(path);
	if (d == NULL)
		return -1;
	while ((de = readdir(d)) != NULL) {
		char child[PATH_MAX];

		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		join_path(child, sizeof(child), path, de->d_name);
		if (remove_tree(child) != 0)
			rc = -1;
	}
	closedir(d);
	if (rmdir(path) != 0)
		rc = -1;
	return rc;
}

static void
trash_url_encode(const char *in, char *out, size_t outsz)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;

	while (*in != '\0' && o + 1 < outsz) {
		unsigned char c = (unsigned char)*in++;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '~' || c == '/') {
			out[o++] = (char)c;
		} else if (o + 3 < outsz) {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		} else {
			break;
		}
	}
	out[o] = '\0';
}

static int
make_dirs(const char *path)
{
	char tmp[PATH_MAX];
	size_t i, n;

	if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	n = strlen(tmp);
	for (i = 1; i <= n; i++) {
		if (tmp[i] != '/' && tmp[i] != '\0')
			continue;
		tmp[i] = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
			return -1;
		if (i < n)
			tmp[i] = '/';
	}
	return 0;
}

static int
trash_one(const char *path, char *dest, size_t destsz,
    char *info, size_t infosz)
{
	char stamp[32], encoded[PATH_MAX * 3];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	int n;
	FILE *f;

	if (tm == NULL)
		return -1;
	if (make_dirs(trash_dir) != 0 || make_dirs(trash_info_dir) != 0)
		return -1;
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm);
	trash_url_encode(path, encoded, sizeof(encoded));
	for (n = 1; n < 10000; n++) {
		char suffix[64];
		struct stat st;

		if (n == 1)
			snprintf(suffix, sizeof(suffix), "%s", stamp);
		else
			snprintf(suffix, sizeof(suffix), "%s-%d", stamp, n);
		if (snprintf(dest, destsz, "%s/%s-%s", trash_dir,
		    display_name(path), suffix) >= (int)destsz ||
		    snprintf(info, infosz, "%s/%s-%s.trashinfo",
		    trash_info_dir, display_name(path), suffix) >= (int)infosz)
			return -1;
		if (lstat(dest, &st) == 0 || lstat(info, &st) == 0)
			continue;
		f = fopen(info, "w");
		if (f == NULL)
			return -1;
		fprintf(f, "[Trash Info]\nPath=%s\nDeletionDate=%s\n",
		    encoded, stamp);
		if (fclose(f) != 0) {
			unlink(info);
			return -1;
		}
		if (fsops_move_tree(path, dest) == 0)
			return 0;
		unlink(info);
		return -1;
	}
	errno = EEXIST;
	return -1;
}

static void
trash_selection(FWin *w)
{
	int i, moved = 0;

	if (w == NULL || strcmp(w->path, trash_dir) == 0)
		return;
	for (i = 0; i < w->n; i++) {
		char src[PATH_MAX], dest[PATH_MAX], info[PATH_MAX];
		char name[256];

		if (!w->icons[i].selected || protected_source(w->icons[i].path))
			continue;
		snprintf(src, sizeof(src), "%s", w->icons[i].path);
		snprintf(name, sizeof(name), "%s", w->icons[i].name);
		if (trash_one(src, dest, sizeof(dest), info, sizeof(info)) != 0) {
			fprintf(stderr,
			    "platine-finder: impossible de mettre %s à la corbeille: %s\n",
			    src, strerror(errno));
			ptk_son(&ptk, "lerr");
			continue;
		}
		remap_metadata(src, dest, 0);
		remove_pos(w->path, name);
		moved++;
	}
	if (moved > 0)
		refresh_after_drop();
}

static void
move_to_trash(FWin *w)
{
	ptk_son(&ptk, "fdrp");
	trash_selection(w);
}

/* Chemin du .trashinfo d'une entrée de la corbeille. */
static void
trash_info_name(const char *entry, char *out, size_t outsz)
{
	size_t n, en = strlen(entry);

	path_concat(out, outsz, trash_info_dir, "/");
	n = strlen(out);
	if (n + en + 11 > outsz) {
		out[0] = '\0';
		return;
	}
	memcpy(out + n, entry, en);
	memcpy(out + n + en, ".trashinfo", 11);
}

static void
trash_url_decode(const char *in, char *out, size_t outsz)
{
	size_t o = 0;

	while (*in != '\0' && o + 1 < outsz) {
		if (in[0] == '%' && isxdigit((unsigned char)in[1]) &&
		    isxdigit((unsigned char)in[2])) {
			char hex[3];

			hex[0] = in[1];
			hex[1] = in[2];
			hex[2] = '\0';
			out[o++] = (char)strtol(hex, NULL, 16);
			in += 3;
		} else {
			out[o++] = *in++;
		}
	}
	out[o] = '\0';
}

/* Lit le chemin d'origine enregistré dans le .trashinfo. */
static int
trash_restore_path(const char *entry, char *out, size_t outsz)
{
	char info[PATH_MAX], line[PATH_MAX * 3];
	FILE *f;

	out[0] = '\0';
	trash_info_name(entry, info, sizeof(info));
	if (info[0] == '\0')
		return -1;
	f = fopen(info, "r");
	if (f == NULL)
		return -1;
	while (fgets(line, sizeof(line), f) != NULL) {
		size_t l;

		if (strncmp(line, "Path=", 5) != 0)
			continue;
		l = strlen(line);
		while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
			line[--l] = '\0';
		trash_url_decode(line + 5, out, outsz);
		fclose(f);
		return out[0] != '\0' ? 0 : -1;
	}
	fclose(f);
	return -1;
}

/* Reconnaît « -AAAAMMJJ-HHMMSS » et son éventuel « -N ». */
static int
trash_suffix_at(const char *p)
{
	int i;

	if (p[0] != '-' || strlen(p) < 16)
		return 0;
	for (i = 1; i <= 8; i++)
		if (!isdigit((unsigned char)p[i]))
			return 0;
	if (p[9] != '-')
		return 0;
	for (i = 10; i <= 15; i++)
		if (!isdigit((unsigned char)p[i]))
			return 0;
	if (p[16] == '\0')
		return 1;
	if (p[16] == '-' && isdigit((unsigned char)p[17])) {
		for (i = 17; p[i] != '\0'; i++)
			if (!isdigit((unsigned char)p[i]))
				return 0;
		return 1;
	}
	return 0;
}

/* Nom affiché dans la corbeille : le nom d'origine, sans l'horodatage. */
static void
trash_display_name(const char *entry, char *out, size_t outsz)
{
	char path[PATH_MAX];
	const char *slash;
	size_t l;

	if (trash_restore_path(entry, path, sizeof(path)) == 0 &&
	    (slash = strrchr(path, '/')) != NULL && slash[1] != '\0') {
		snprintf(out, outsz, "%s", slash + 1);
		return;
	}
	snprintf(out, outsz, "%s", entry);
	l = strlen(out);
	if (l > 17) {
		char *p;

		for (p = out + 1; *p != '\0'; p++) {
			if (!trash_suffix_at(p))
				continue;
			*p = '\0';
			return;
		}
	}
}

static void
empty_trash(void)
{
	ptk_son(&ptk, "ftrs");
	DIR *d;
	struct dirent *de;

	if (trash_is_empty()) {
		ptk_son(&ptk, "lerr");
		return;
	}
	{
		/* la formule du Finder : nombre d'éléments et place occupée */
		char msg[256], t[32];
		long long o = 0;
		int ne = 0;
		DIR *dn = opendir(trash_dir);

		if (dn != NULL) {
			while ((de = readdir(dn)) != NULL) {
				char c[PATH_MAX];
				struct stat st;

				if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
					continue;
				join_path(c, sizeof(c), trash_dir, de->d_name);
				if (lstat(c, &st) == 0)
					o += st.st_size;
				ne++;
			}
			closedir(dn);
		}
		if (o < 1024 * 1024)
			snprintf(t, sizeof(t), "%lld Ko", (o + 1023) / 1024);
		else
			snprintf(t, sizeof(t), "%.1f Mo", (double)o / (1024 * 1024));
		snprintf(msg, sizeof(msg), "La Corbeille contient %d élément%s, qui occupe%s\n"
		    "%s sur le disque. Voulez-vous vraiment\nle%s supprimer définitivement ?",
		    ne, ne > 1 ? "s" : "", ne > 1 ? "nt" : "", t, ne > 1 ? "s" : "");
		if (!finder_alert(msg, "Annuler", "OK", pix_trash_full, pix_trash_full_mask))
			return;
	}
	d = opendir(trash_dir);
	if (d == NULL)
		return;
	while ((de = readdir(d)) != NULL) {
		char child[PATH_MAX];
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		join_path(child, sizeof(child), trash_dir, de->d_name);
		remove_tree(child);
	}
	closedir(d);
	d = opendir(trash_info_dir);
	if (d != NULL) {
		while ((de = readdir(d)) != NULL) {
			char child[PATH_MAX];

			if (strcmp(de->d_name, ".") == 0 ||
			    strcmp(de->d_name, "..") == 0)
				continue;
			join_path(child, sizeof(child), trash_info_dir,
			    de->d_name);
			unlink(child);
		}
		closedir(d);
	}
	refresh_after_drop();
}

/* Alerte modale façon Mac OS 9, dessinée par libptk. cancel == NULL :
 * seul le bouton par défaut (OK) est présenté. */
static int
finder_alert(const char *message, const char *cancel, const char *ok,
    Pixmap icon, Pixmap mask)
{
	Window win;
	XSetWindowAttributes a;
	XEvent ev;
	int w = 410, h = 150, x, y, i, chosen = 0;
	int bx[2], bw[2];
	int lines = 1, line_h = 16;
	const char *p;

	for (p = message; *p != '\0'; p++)
		if (*p == '\n')
			lines++;
	if (31 + lines * line_h + 48 > h)
		h = 31 + lines * line_h + 48;
	x = (sw - w) / 2;
	y = (sh - h) / 3;
	memset(&a, 0, sizeof(a));
	a.override_redirect = True;
	a.background_pixel = ptk.face.pixel;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	    KeyPressMask;
	win = XCreateWindow(dpy, root, x, y, (unsigned)w, (unsigned)h, 0,
	    CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	XStoreName(dpy, win, "Alerte");
	XMapRaised(dpy, win);
	XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask,
	    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	XGrabKeyboard(dpy, win, False, GrabModeAsync, GrabModeAsync,
	    CurrentTime);
	/* relevé : bouton par défaut à droite, à 17 px du bord, l'autre 10 px avant */
	bw[1] = ptk_text_width(&ptk, ptk.font_menu, ok) + 34;
	if (bw[1] < 62)
		bw[1] = 62;
	if (cancel != NULL && cancel[0] != '\0') {
		bw[0] = ptk_text_width(&ptk, ptk.font_menu, cancel) + 22;
		if (bw[0] < 58)
			bw[0] = 58;
	} else {
		bw[0] = 0;
	}
	bx[1] = w - 17 - bw[1];
	bx[0] = bx[1] - 10 - bw[0];
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.window != win) {
			/* les fenêtres derrière l'alerte (le menu vient de se refermer) se redessinent */
			FWin *f = find_win(ev.xexpose.window);

			if (f != NULL && ev.xexpose.count == 0)
				draw_window(f);
		} else if (ev.type == Expose && ev.xexpose.count == 0) {
			int ty = 31;

			/* cadre relevé : noir, #bb, blanc ; à droite et en bas #99, #55, noir sur 2 px */
			ptk_fill(&ptk, win, 0, 0, w, h, ptk.black);
			ptk_fill(&ptk, win, 1, 1, w - 3, h - 3, gris(0xbb));
			ptk_fill(&ptk, win, 2, 2, w - 4, h - 4, gris(0x55));
			ptk_fill(&ptk, win, 2, 2, w - 5, h - 5, gris(0x99));
			ptk_fill(&ptk, win, 2, 2, w - 6, h - 6, ptk.white);
			ptk_fill(&ptk, win, 3, 3, w - 7, h - 7, ptk.face);
			if (arg_corbeille_pleine != NULL && icon == pix_trash_full)
				ptk_draw_argb(&ptk, win, arg_corbeille_pleine, 28, 19, 0);
			else if (icon != None)
				ptk_draw_pixmap(&ptk, win, icon, mask, 28, 19, 32, 32);
			else
				ptk_icon(&ptk, win, 28, 19, 32, "alert");
			for (p = message; *p != '\0'; ) {
				char buf[512];
				size_t n = 0;

				while (*p != '\0' && *p != '\n' &&
				    n + 1 < sizeof(buf))
					buf[n++] = *p++;
				if (*p == '\n')
					p++;
				buf[n] = '\0';
				ptk_text(&ptk, win, ptk.font_menu, 91, ty, buf, ptk.text);
				ty += line_h;
			}
			if (cancel != NULL && cancel[0] != '\0')
				ptk_button(&ptk, win, bx[0], h - 39, bw[0], 20,
				    cancel, 0, 0, ptk.text);
			ptk_button(&ptk, win, bx[1], h - 41, bw[1], 25, ok, 0, 1, ptk.text);
			XFlush(dpy);
		} else if (ev.type == ButtonRelease) {
			int hit = -1;

			for (i = 0; i < 2; i++) {
				if (cancel == NULL && i == 0)
					continue;
				if (ev.xbutton.x >= bx[i] &&
				    ev.xbutton.x < bx[i] + bw[i] &&
				    ev.xbutton.y >= h - 41 &&
				    ev.xbutton.y < h - 16) {
					hit = i;
					break;
				}
			}
			if (hit >= 0) {
				chosen = hit;
				break;
			}
		} else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);

			if (ks == XK_Return || ks == XK_KP_Enter) {
				chosen = 1;	/* le bouton par défaut, à droite */
				break;
			}
			if (ks == XK_Escape) {
				chosen = cancel != NULL && cancel[0] != '\0' ?
				    0 : 1;
				break;
			}
		}
	}
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XDestroyWindow(dpy, win);
	XFlush(dpy);
	return chosen == 1;
}

static void
restore_selection(FWin *w)
{
	int i, moved = 0;

	if (w == NULL || w->is_desktop || strcmp(w->path, trash_dir) != 0)
		return;
	/* Prévalidation : aucun élément ne doit être restauré à moitié. */
	for (i = 0; i < w->n; i++) {
		const char *entry;
		char dest[PATH_MAX], parent[PATH_MAX];
		struct stat st;
		char *slash;

		if (!w->icons[i].selected)
			continue;
		entry = strrchr(w->icons[i].path, '/');
		entry = entry != NULL ? entry + 1 : w->icons[i].path;
		if (trash_restore_path(entry, dest, sizeof(dest)) != 0) {
			drop_error("emplacement d'origine inconnu",
			    w->icons[i].path);
			return;
		}
		if (lstat(dest, &st) == 0 || errno != ENOENT) {
			drop_error("l'emplacement d'origine est déjà occupé",
			    dest);
			return;
		}
		snprintf(parent, sizeof(parent), "%s", dest);
		slash = strrchr(parent, '/');
		if (slash != NULL && slash != parent)
			*slash = '\0';
		else
			snprintf(parent, sizeof(parent), "/");
		if (stat(parent, &st) != 0 || !S_ISDIR(st.st_mode)) {
			drop_error("le dossier d'origine n'existe plus", parent);
			return;
		}
	}
	for (i = 0; i < w->n; i++) {
		const char *entry;
		char src[PATH_MAX], dest[PATH_MAX], info[PATH_MAX];
		char name[256];

		if (!w->icons[i].selected)
			continue;
		snprintf(src, sizeof(src), "%s", w->icons[i].path);
		snprintf(name, sizeof(name), "%s", w->icons[i].name);
		entry = strrchr(src, '/');
		entry = entry != NULL ? entry + 1 : src;
		if (trash_restore_path(entry, dest, sizeof(dest)) != 0)
			continue;
		if (fsops_move_tree(src, dest) != 0) {
			int error = errno;

			fprintf(stderr,
			    "platine-finder: impossible de restaurer %s: %s\n",
			    src, strerror(error));
			ptk_son(&ptk, "lerr");
			continue;
		}
		trash_info_name(entry, info, sizeof(info));
		if (info[0] != '\0')
			unlink(info);
		remap_metadata(src, dest, 0);
		remove_pos(trash_dir, name);
		moved++;
	}
	if (moved > 0) {
		save_state();
		refresh_after_drop();
	}
}

static void
refresh_window(FWin *w)
{
	if (w == NULL)
		return;
	load_dir(w);
	draw_window(w);
}

static void
expand_path(const char *in, char *out, size_t outsz)
{
	if (in[0] == '~' && (in[1] == '/' || in[1] == '\0'))
		snprintf(out, outsz, "%s%s", home_dir, in + 1);
	else
		snprintf(out, outsz, "%s", in);
}

static void
path_menu(FWin *w, int xr, int yr)
{
	char menu[4096];
	char work[PATH_MAX], prefix[PATH_MAX];
	char *p, *save;
	size_t off = 0;

	menu[0] = '\0';
	snprintf(work, sizeof(work), "%s", w->path);
	if (work[0] == '/') {
		snprintf(prefix, sizeof(prefix), "/");
		off += (size_t)snprintf(menu + off, sizeof(menu) - off,
		    "/\n");
	} else {
		prefix[0] = '\0';
	}
	for (p = strtok_r(work, "/", &save); p != NULL;
	    p = strtok_r(NULL, "/", &save)) {
		char tmp[PATH_MAX];
		if (prefix[0] == '\0')
			snprintf(tmp, sizeof(tmp), "%s", p);
		else if (strcmp(prefix, "/") == 0)
			snprintf(tmp, sizeof(tmp), "/%s", p);
		else
			snprintf(tmp, sizeof(tmp), "%s/%s", prefix, p);
		snprintf(prefix, sizeof(prefix), "%s", tmp);
		{
			char label[PATH_MAX];
			short_path(prefix, label, sizeof(label));
			off += (size_t)snprintf(menu + off,
			    sizeof(menu) - off, "%s\n", label);
		}
		if (off >= sizeof(menu) - 2)
			break;
	}
	if (menu[0] != '\0')
		send_context(w, xr, yr, menu);
}

/* bouton de la barre sous x : glyphe et position, ou -1 */
static int
bouton_barre(FWin *w, int x, int y, int *bx, int *glyphe)
{
	if (y < 4 || y >= 22)
		return -1;
	struct { int x, g; } t[] = { {4, 0}, {28, 1}, {52, 2}, {w->w - 96, 6},
	    {w->w - 70, 3}, {w->w - 46, 4}, {w->w - 22, 5} };

	for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (x >= t[i].x && x < t[i].x + 22) {
			*bx = t[i].x;
			*glyphe = t[i].g;
			return (int)i;
		}
	return -1;
}

static void
toolbar_click(FWin *w, int x, int y, int xr, int yr)
{
	char parent[PATH_MAX];
	char *slash;

	(void)y;
	if (x >= 4 && x < 26) {
		history_go(w, -1);
	} else if (x >= 28 && x < 50) {
		history_go(w, 1);
	} else if (x >= 52 && x < 74) {
		snprintf(parent, sizeof(parent), "%s", w->path);
		slash = strrchr(parent, '/');
		if (slash != NULL && slash != parent)
			*slash = '\0';
		else if (slash == parent)
			parent[1] = '\0';
		navigate_to(w, parent);
	} else if (x >= 80 && x < w->w - 110) {
		/* le texte reste : on complète ou on corrige le chemin affiché */
		w->addr_focus = 1;
		w->addr_len = (int)strlen(w->addr);
		draw_toolbar(w);
	} else if (x >= w->w - 96 && x < w->w - 74) {
		path_menu(w, xr, yr);
	} else if (x >= w->w - 70 && x < w->w - 48) {
		changer_vue(w, 0);
	} else if (x >= w->w - 46 && x < w->w - 24) {
		changer_vue(w, 1);
	} else if (x >= w->w - 22 && x < w->w - 2) {
		changer_vue(w, 2);
	}
}

static void
send_context(FWin *w, int x, int y, const char *menu)
{
	XEvent ev;

	/* rendre le pointeur : le WM suit le bouton enfoncé, comme MenuSelect */
	XUngrabPointer(dpy, CurrentTime);
	XFlush(dpy);
	set_utf8(w->win, a_context, menu);
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = w->win;
	ev.xclient.message_type = a_context;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = x;
	ev.xclient.data.l[1] = y;
	XSendEvent(dpy, root, False,
	    SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
}

/* « Nettoyer », « Réorganiser ▸ par … », « Options de présentation… » : actifs
 * en présentation par icônes ou par boutons ; Nettoyer et Réorganiser grisés
 * quand la fenêtre est toujours rangée, comme Mac OS 9 */
static void
menu_rangement(FWin *w, char *out, size_t n, char sep)
{
	int libre = w != NULL && w->view != 2 && w->range != RANGE_TRIEE, k;
	char sous[300] = "";

	for (k = 0; k < RP_NB; k++) {
		size_t l = strlen(sous);

		snprintf(sous + l, sizeof(sous) - l, "%s%s%s", k ? ET : "",
		    RP_ACTIF(k) ? "" : "~", rp_noms[k]);
	}
	if (libre)
		snprintf(out, n, "Nettoyer%cRéorganiser" SOUS "%s", sep, sous);
	else
		snprintf(out, n, "~Nettoyer%c~Réorganiser", sep);
}

/* « Options de présentation… » : par icônes ou par boutons */
#define OPT_PRES(w) ((w) != NULL && (w)->view != 2 ? "" : "~")

static void
context_menu(FWin *w, int idx, int x, int y)
{
	if (idx >= 0 && !w->is_desktop &&
	    strcmp(w->path, trash_dir) == 0)
		send_context(w, x, y,
		    "Aide\n-\nOuvrir\n"
		    "Lire les informations" SOUS "Général\t⌘I" ET "~Partage…" ET "~Mémoire…\n"
		    "~Étiquette\n-\nRanger");
	else if (idx >= 0)
		send_context(w, x, y,
		    "Aide\n-\nOuvrir\n"
		    "Lire les informations" SOUS "Général\t⌘I" ET "~Partage…" ET "~Mémoire…\n"
		    "~Étiquette\nDupliquer\n~Créer un alias\n~Ajouter aux favoris\n-\n"
		    "Placer dans la Corbeille");
	else {
		char m[1024], ro[400];

		menu_rangement(w, ro, sizeof(ro), '\n');
		if (w->is_desktop)
			snprintf(m, sizeof(m), "Aide\n-\nNouveau dossier\n-\n%s\n"
			    "%sOptions de présentation…\n-\n~Changer l'image du bureau…", ro, OPT_PRES(w));
		else
			snprintf(m, sizeof(m), "Aide\n-\nNouveau dossier\n-\n"
			    "Présentation" SOUS "par icônes" ET "par boutons" ET "par liste" "\n"
			    "%s\n%sOptions de présentation…\n-\nActualiser", ro, OPT_PRES(w));
		send_context(w, x, y, m);
	}
}

static void
comment_set(const char *path, const char *text)
{
	char file[PATH_MAX], tmp[PATH_MAX], line[2048];
	FILE *in, *out;
	int found = 0;

	path_concat(file, sizeof(file), state_dir, "/commentaires");
	path_concat(tmp, sizeof(tmp), state_dir, "/commentaires.tmp");
	in = fopen(file, "r");
	out = fopen(tmp, "w");
	if (out == NULL) {
		if (in)
			fclose(in);
		return;
	}
	if (in != NULL) {
		while (fgets(line, sizeof(line), in) != NULL) {
			char *tab = strchr(line, '\t');
			if (tab == NULL)
				continue;
			*tab = '\0';
			if (strcmp(line, path) == 0) {
				found = 1;
			} else {
				/* recopie la ligne d'origine */
				int i;
				for (i = 0; line[i]; i++)
					fputc(line[i], out);
				fputc('\t', out);
				for (i = 0; tab[1 + i]; i++) {
					if (tab[1 + i] == '\n')
						break;
					fputc(tab[1 + i], out);
				}
				fputc('\n', out);
			}
		}
		fclose(in);
	}
	(void)found;
	fprintf(out, "%s\t", path);
	{
		int i;
		for (i = 0; text[i]; i++) {
			if (text[i] == '\n')
				fputs("\\n", out);
			else if (text[i] != '\t')
				fputc(text[i], out);
		}
	}
	fputc('\n', out);
	fclose(out);
	rename(tmp, file);
}

static TPos *
type_cherche(const char *path)
{
	TPos *t;

	for (t = types_db; t != NULL; t = t->next)
		if (strcmp(t->path, path) == 0)
			return t;
	return NULL;
}

/* 1 si une métadonnée type/créateur existe pour ce chemin */
static int
type_get(const char *path, char *type, size_t tsz, char *crea, size_t csz)
{
	TPos *t = type_cherche(path);

	if (t == NULL) {
		if (tsz > 0)
			type[0] = '\0';
		if (csz > 0)
			crea[0] = '\0';
		return 0;
	}
	snprintf(type, tsz, "%s", t->type);
	snprintf(crea, csz, "%s", t->crea);
	return t->type[0] != '\0' || t->crea[0] != '\0';
}

static void
types_save(void)
{
	char file[PATH_MAX], tmp[PATH_MAX];
	FILE *f;
	TPos *t;

	path_concat(file, sizeof(file), state_dir, "/types");
	path_concat(tmp, sizeof(tmp), state_dir, "/types.tmp");
	f = fopen(tmp, "w");
	if (f == NULL)
		return;
	for (t = types_db; t != NULL; t = t->next)
		fprintf(f, "%s\t%s\t%s\n", t->path, t->type, t->crea);
	fclose(f);
	rename(tmp, file);
}

static void
load_types(void)
{
	char path[PATH_MAX], line[PATH_MAX * 2];
	FILE *f;

	path_concat(path, sizeof(path), state_dir, "/types");
	f = fopen(path, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *p[4], *save;
		int n = 0;
		char *tok = strtok_r(line, "\t\n", &save);

		while (tok != NULL && n < 4) {
			p[n++] = tok;
			tok = strtok_r(NULL, "\t\n", &save);
		}
		if (n >= 3 && p[0][0] != '\0') {
			TPos *t = calloc(1, sizeof(*t));

			if (t == NULL)
				break;
			snprintf(t->path, sizeof(t->path), "%s", p[0]);
			snprintf(t->type, sizeof(t->type), "%s", p[1]);
			snprintf(t->crea, sizeof(t->crea), "%s", p[2]);
			t->next = types_db;
			types_db = t;
		}
	}
	fclose(f);
}

static const struct { const char *code, *label; int family; } type_defs[] = {
	{ "TEXT", "Document texte", DOC_TEXTE },
	{ "ttro", "Document texte", DOC_TEXTE },
	{ "PICT", "Image", DOC_IMAGE }, { "PNG ", "Image", DOC_IMAGE },
	{ "JPEG", "Image", DOC_IMAGE }, { "GIFf", "Image", DOC_IMAGE },
	{ "PDF ", "Document PDF", DOC_GENERIC },
	{ "WAVE", "Son", DOC_SON }, { "AIFF", "Son", DOC_SON },
	{ "Mp3 ", "Son", DOC_SON }, { "MooV", "Film", DOC_FILM },
	{ "ZIP ", "Archive", DOC_ARCHIVE },
	{ NULL, NULL, DOC_GENERIC }
};

static const char *
type_label(const char *type)
{
	int i;

	for (i = 0; type_defs[i].code != NULL; i++)
		if (strcmp(type, type_defs[i].code) == 0)
			return type_defs[i].label;
	return NULL;
}

static const struct { const char *ext, *label; int family; } kind_ext[] = {
	{ "txt", "Document texte", DOC_TEXTE }, { "md", "Document texte", DOC_TEXTE },
	{ "log", "Document texte", DOC_TEXTE }, { "me", "Document texte", DOC_TEXTE },
	{ "png", "Image", DOC_IMAGE }, { "jpg", "Image", DOC_IMAGE },
	{ "jpeg", "Image", DOC_IMAGE }, { "gif", "Image", DOC_IMAGE },
	{ "bmp", "Image", DOC_IMAGE }, { "webp", "Image", DOC_IMAGE },
	{ "pdf", "Document PDF", DOC_GENERIC },
	{ "wav", "Son", DOC_SON }, { "mp3", "Son", DOC_SON },
	{ "ogg", "Son", DOC_SON }, { "flac", "Son", DOC_SON },
	{ "opus", "Son", DOC_SON },
	{ "avi", "Film", DOC_FILM }, { "mp4", "Film", DOC_FILM },
	{ "mkv", "Film", DOC_FILM }, { "mov", "Film", DOC_FILM },
	{ "webm", "Film", DOC_FILM },
	{ "zip", "Archive", DOC_ARCHIVE }, { "tar", "Archive", DOC_ARCHIVE },
	{ "gz", "Archive", DOC_ARCHIVE }, { "tgz", "Archive", DOC_ARCHIVE },
	{ "bz2", "Archive", DOC_ARCHIVE }, { "rar", "Archive", DOC_ARCHIVE },
	{ "7z", "Archive", DOC_ARCHIVE },
	{ "c", "Source C", DOC_SCRIPT }, { "h", "Source C", DOC_SCRIPT },
	{ "py", "Script Python", DOC_SCRIPT }, { "sh", "Script shell", DOC_SCRIPT },
	{ "pl", "Script Perl", DOC_SCRIPT }, { "js", "Script", DOC_SCRIPT },
	{ "html", "Page web", DOC_WEB }, { "htm", "Page web", DOC_WEB },
	{ "css", "Style web", DOC_WEB },
	{ NULL, NULL, DOC_GENERIC }
};

static int
document_family(const Icon *ic)
{
	char type[8], crea[8];
	const char *dot;
	int i;

	if (type_get(ic->path, type, sizeof(type), crea, sizeof(crea)) &&
	    type[0] != '\0')
		for (i = 0; type_defs[i].code != NULL; i++)
			if (strcmp(type, type_defs[i].code) == 0)
				return type_defs[i].family;
	dot = strrchr(ic->name, '.');
	if (dot != NULL)
		for (i = 0; kind_ext[i].ext != NULL; i++)
			if (strcasecmp(dot + 1, kind_ext[i].ext) == 0)
				return kind_ext[i].family;
	return DOC_GENERIC;
}

/* libellé « genre » (Kind de Mac OS) : métadonnée, puis extension,
 * puis dépôt générique. */
static const char *
kind_de(const Icon *ic)
{
	static char buf[64];
	const char *lab;
	char type[8], crea[8];
	const char *dot;
	int i;

	if (ic->is_alias)
		return "alias";
	if (ic->is_dir)
		return "dossier";
	if (est_programme(ic, NULL, 0))
		return "programme";
	if (type_get(ic->path, type, sizeof(type), crea, sizeof(crea)) &&
	    type[0] != '\0') {
		lab = type_label(type);
		if (lab != NULL)
			return lab;
		snprintf(buf, sizeof(buf), "type %.4s", type);
		return buf;
	}
	dot = strrchr(ic->name, '.');
	if (dot != NULL) {
		for (i = 0; kind_ext[i].ext != NULL; i++)
			if (strcasecmp(dot + 1, kind_ext[i].ext) == 0)
				return kind_ext[i].label;
	}
	return "document";
}

static void
launch_info(const char *path)
{
	char exe[PATH_SAFE], bin[PATH_MAX];
	ssize_t n;
	pid_t pid;

	/* Lire les informations du Dossier Système, sinon à côté du Finder */
	if (ptk_chemin_programme("System/Extensions/Lire les informations",
	    "", bin, sizeof(bin))) {
		pid = fork();
		if (pid == 0) {
			execl(bin, "Lire les informations", path, (char *)NULL);
			_exit(1);
		}
		return;
	}
	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n <= 0)
		return;
	exe[n] = '\0';
	{
		char *slash = strrchr(exe, '/');
		if (slash == NULL)
			return;
		*slash = '\0';
	}
	snprintf(bin, sizeof(bin), "%s/platine-info", exe);
	pid = fork();
	if (pid == 0) {
		execl(bin, "platine-info", path, (char *)NULL);
		_exit(1);
	}
}

static void
open_info_for(FWin *parent, int idx)
{
	if (parent == NULL || idx < 0 || idx >= parent->n)
		return;
	launch_info(parent->icons[idx].path);
}

static void
draw_info_window(FWin *w)
{
	struct stat st;
	char buf[256], date[64];
	int name_y = 10, y;

	memset(&st, 0, sizeof(st));
	stat(w->info_path, &st);
	int field_x = 70, field_w = w->w - 80;

	ptk_fill(&ptk, w->win, 0, 0, w->w, w->h, ptk.white);
	ptk_icon(&ptk, w->win, 16, 10, 32,
	    S_ISDIR(st.st_mode) ? "folder" : "doc");
	ptk_text(&ptk, w->win, ptk.font_small, 14, 54,
	    w->info_name, ptk.text);

	ptk_fill(&ptk, w->win, field_x, name_y, field_w, 18, ptk.white);
	ptk_bevel(&ptk, w->win, field_x, name_y, field_w, 18, 1);
	ptk_text(&ptk, w->win, ptk.font_small, field_x + 4, name_y + 13,
	    w->info_name, ptk.text);
	if (w->info_focus == 1) {
		int tw = ptk_text_width(&ptk, ptk.font_small, w->info_name);
		ptk_line(&ptk, w->win, field_x + 4 + tw, name_y + 3,
		    field_x + 4 + tw, name_y + 15, ptk.black);
	}

	y = 40;
	ptk_text(&ptk, w->win, ptk.font_small, 8, y + 12, "Show:", ptk.text);
	ptk_fill(&ptk, w->win, 52, y, 180, 18, ptk.white);
	ptk_bevel(&ptk, w->win, 52, y, 180, 18, 1);
	ptk_text(&ptk, w->win, ptk.font_small, 58, y + 13,
	    "General Information", ptk.text);

	if (stat(w->info_path, &st) == 0) {
		char itype[8], icrea[8];
		Icon ki;
		int a_type;

		memset(&ki, 0, sizeof(ki));
		ki.is_dir = S_ISDIR(st.st_mode);
		snprintf(ki.name, sizeof(ki.name), "%s", w->info_name);
		snprintf(ki.path, sizeof(ki.path), "%s", w->info_path);
		y = 68;
		ptk_text(&ptk, w->win, ptk.font_small, 8, y + 10, "Kind:",
		    ptk.text);
		ptk_text(&ptk, w->win, ptk.font_small, 8, y + 26,
		    kind_de(&ki), ptk.text);
		snprintf(buf, sizeof(buf), "%lld octets",
		    (long long)st.st_size);
		ptk_text(&ptk, w->win, ptk.font_small, 8, y + 44,
		    buf, ptk.text);
		ptk_text(&ptk, w->win, ptk.font_small, 8, y + 62,
		    "Where: " , ptk.text);
		ptk_text(&ptk, w->win, ptk.font_small, 52, y + 62,
		    w->info_path, ptk.text_dim);
		ptk_fuseau_suivre();
		strftime(date, sizeof(date), "%a %d %b %Y %H:%M",
		    localtime(&st.st_mtime));
		ptk_text(&ptk, w->win, ptk.font_small, 8, y + 86,
		    "Modified:", ptk.text);
		ptk_text(&ptk, w->win, ptk.font_small, 74, y + 86, date,
		    ptk.text);
		a_type = type_get(w->info_path, itype, sizeof(itype),
		    icrea, sizeof(icrea));
		if (a_type) {
			snprintf(buf, sizeof(buf), "Type: %s",
			    itype[0] != '\0' ? itype : "--");
			ptk_text(&ptk, w->win, ptk.font_small, 8, y + 104,
			    buf, ptk.text);
			snprintf(buf, sizeof(buf), "Creator: %s",
			    icrea[0] != '\0' ? icrea : "--");
			ptk_text(&ptk, w->win, ptk.font_small, 8, y + 120,
			    buf, ptk.text);
		}
	}

	y = 176;
	ptk_text(&ptk, w->win, ptk.font_small, 8, y, "Comments:", ptk.text);
	ptk_fill(&ptk, w->win, 8, y + 8, w->w - 16, 90, ptk.white);
	ptk_bevel(&ptk, w->win, 8, y + 8, w->w - 16, 90, 1);
	{
		char *copy = strdup(w->info_comment);
		char *line, *save;
		int ly = y + 24;
		for (line = strtok_r(copy, "\n", &save); line != NULL;
		    line = strtok_r(NULL, "\n", &save)) {
			ptk_text(&ptk, w->win, ptk.font_small, 14, ly, line,
			    ptk.text);
			ly += 16;
			if (ly > y + 90)
				break;
		}
		free(copy);
	}
	XFlush(dpy);
}

static void
start_rename(FWin *w)
{
	if (w == NULL || w->selected < 0 || w->selected >= w->n)
		return;
	/* Le nom affiché vient du .trashinfo : le renommage direct
	 * casserait la correspondance avec l'entrée réelle. */
	if (strcmp(w->path, trash_dir) == 0 ||
	    (w->is_desktop && w->selected < BUREAU_FIXES) ||
	    strcmp(NOM_VU(&w->icons[w->selected]), w->icons[w->selected].name) != 0) {
		/* ni la Corbeille, ni le disque, ni un dossier du Macintosh au nom traduit */
		/* le disque et la Corbeille ne se renomment pas ici */
		ptk_son(&ptk, "lerr");
		return;
	}
	rename_win = w;
	rename_idx = w->selected;
	snprintf(rename_buf, sizeof(rename_buf), "%s",
	    w->icons[w->selected].name);
	rename_len = (int)strlen(rename_buf);
	rename_select_all = 1;
	draw_window(w);
}

static void
commit_rename(void)
{
	FWin *w = rename_win;
	char oldpath[PATH_MAX], newpath[PATH_MAX];
	struct stat st;
	Icon *ic;

	if (w == NULL || rename_idx < 0 || rename_idx >= w->n) {
		rename_win = NULL;
		return;
	}
	ic = &w->icons[rename_idx];
	if (rename_len <= 0 || strchr(rename_buf, '/') != NULL ||
	    strcmp(rename_buf, ".") == 0 || strcmp(rename_buf, "..") == 0 ||
	    strcmp(rename_buf, ic->name) == 0) {
		rename_win = NULL;
		rename_idx = -1;
		draw_window(w);
		return;
	}
	join_path(oldpath, sizeof(oldpath), w->path, ic->name);
	join_path(newpath, sizeof(newpath), w->path, rename_buf);
	if (lstat(newpath, &st) == 0) {
		ptk_son(&ptk, "lerr");
	} else if (errno == ENOENT && fsops_move_tree(oldpath, newpath) == 0) {
		move_pos(folder_key(w), ic->name, rename_buf);
		remap_metadata(oldpath, newpath, 0);
		snprintf(ic->name, sizeof(ic->name), "%s", rename_buf);
		snprintf(ic->path, sizeof(ic->path), "%s", newpath);
		save_state();
	}
	rename_win = NULL;
	rename_idx = -1;
	rename_select_all = 0;
	refresh_window(w);
}

static void
arrange_icons(FWin *w)
{
	/* Nettoyer : chaque icône dans la case libre la plus proche ; la
	 * sélection seule si elle existe, comme « Nettoyer la sélection » */
	char bouge[MAX_ICONS];
	int i, sel = 0;

	if (w == NULL || w->view == 2 || w->range == RANGE_TRIEE)
		return;
	for (i = 0; i < w->n; i++)
		sel += w->icons[i].selected != 0;
	for (i = 0; i < w->n; i++)
		bouge[i] = sel == 0 || w->icons[i].selected;
	aligner(w, bouge);
	enregistrer_positions(w);
	draw_window(w);
}

/*
 * Options de présentation (⌘J), relevées sur Mac OS 9.2 (« Desktop View
 * Options », 310 × 352) : nom et présentation de la fenêtre, groupe « Options
 * de présentation par icônes » : disposition (aucune, toujours aligner sur la
 * grille, toujours ranger + menu local de la clé), taille des icônes ; Annuler,
 * OK. La petite taille n'existe pas encore dans Platine : grisée.
 */
#define OP_W 350	/* 310 sur la VM ; les libellés français sont plus longs */
#define OP_H 352
#define OP_PX 180	/* menu local de la clé */
#define OP_PW (OP_W - 26 - OP_PX)
#define OP_ANNULER(x, y) ((y) >= 314 && (y) < 339 && (x) >= OP_W - 150 && (x) < OP_W - 90)
#define OP_OK(x, y) ((y) >= 314 && (y) < 339 && (x) >= OP_W - 80 && (x) < OP_W - 16)

static void
op_dessiner(Window win, FWin *w, int range, int par, int presse)
{
	char nom[300];
	int y;

	ptk_fill(&ptk, win, 0, 0, OP_W, OP_H, ptk.face);
	ptk_rect(&ptk, win, 0, 0, OP_W, OP_H, ptk.black);
	ptk_titlebar(&ptk, win, 1, 1, OP_W - 2, 19, "Options de présentation", 1, None, None);
	ptk_line(&ptk, win, 1, 20, OP_W - 2, 20, ptk.black);
	if (w->is_desktop)
		ptk_draw_argb(&ptk, win, arg_disque, 24, 32, 0);
	else
		ptk_draw_argb(&ptk, win, arg_dossier, 24, 32, 0);
	ptk_fit_text(&ptk, ptk.font_menu, w->is_desktop ? "Bureau" : path_base(w->path), nom,
	    sizeof(nom), OP_W - 90);
	ptk_text(&ptk, win, ptk.font_menu, 67, 45, nom, ptk.text);
	ptk_text(&ptk, win, ptk.font_menu, 67, 59, "Présentation : par icônes", ptk.text);
	/* groupe : trait gravé, titre sur le cadre */
	ptk_rect(&ptk, win, 16, 84, OP_W - 34, 186, gris(0x88));
	ptk_rect(&ptk, win, 17, 85, OP_W - 34, 186, ptk.white);
	ptk_rect(&ptk, win, 16, 84, OP_W - 35, 185, gris(0x88));
	ptk_fill(&ptk, win, 22, 76, ptk_text_width(&ptk, ptk.font_menu,
	    "Options par icônes") + 8, 14, ptk.face);
	ptk_text(&ptk, win, ptk.font_menu, 26, 88, "Options par icônes", ptk.text);
	ptk_text(&ptk, win, ptk.font_menu, 28, 105, "Disposition des icônes :", ptk.text);
	for (y = 0; y < 3; y++) {
		static const char *lab[3] = { "Aucune", "Toujours aligner sur la grille",
		    "Toujours ranger :" };

		ptk_radio(&ptk, win, 52, 118 + 24 * y, range == y);
		ptk_text(&ptk, win, ptk.font_menu, 68, 128 + 24 * y, lab[y], ptk.text);
	}
	/* pictogrammes de la VM : grille, icônes rangées */
	for (y = 0; y < 3; y++) {
		ptk_line(&ptk, win, 26, 143 + 4 * y, 36, 143 + 4 * y, gris(0x88));
		ptk_line(&ptk, win, 27 + 4 * y, 142, 27 + 4 * y, 152, gris(0x88));
	}
	ptk_fill(&ptk, win, 26, 166, 3, 3, ptk.black);
	ptk_fill(&ptk, win, 32, 166, 3, 3, ptk.black);
	ptk_fill(&ptk, win, 26, 172, 3, 3, ptk.black);
	ptk_fill(&ptk, win, 32, 172, 3, 3, ptk.black);
	ptk_popup(&ptk, win, OP_PX, 162, OP_PW, 18, rp_noms[par], presse);
	if (range != RANGE_TRIEE)	/* menu local inactif : voilé */
		ptk_fill_alpha(&ptk, win, OP_PX, 162, OP_PW, 18, ptk.face, 0x9000);
	ptk_text(&ptk, win, ptk.font_menu, 28, 250, "Taille des icônes :", ptk.text);
	/* petite taille : pas encore dans Platine, grisée */
	ptk_draw_argb(&ptk, win, arg_document16, 154, 212, 0);
	ptk_radio(&ptk, win, 156, 240, 0);
	ptk_fill_alpha(&ptk, win, 150, 208, 24, 48, ptk.face, 0x9000);
	ptk_draw_argb(&ptk, win, arg_document, 206, 204, 0);
	ptk_radio(&ptk, win, 216, 240, 1);
	ptk_button(&ptk, win, OP_W - 150, 317, 60, 20, "Annuler", presse == 2, 0, ptk.text);
	ptk_button(&ptk, win, OP_W - 80, 314, 64, 25, "OK", presse == 3, 1, ptk.text);
	XFlush(dpy);
}

/* menu local de la clé de rangement, ouvert sous le bouton tant qu'on appuie */
static int
op_menu_local(Window parent, int px, int py, int par)
{
	XSetWindowAttributes a;
	Window m;
	XEvent ev;
	int mw = OP_PW + 30, ih = 18, mh = RP_NB * ih + 4, i, sel = par, x0, y0;
	Window r;

	XTranslateCoordinates(dpy, parent, root, px, py - par * ih - 2, &x0, &y0, &r);
	memset(&a, 0, sizeof(a));
	a.override_redirect = True;
	a.save_under = True;
	m = XCreateWindow(dpy, root, x0, y0, (unsigned)mw, (unsigned)mh, 0, CopyFromParent,
	    InputOutput, CopyFromParent, CWOverrideRedirect | CWSaveUnder, &a);
	XSelectInput(dpy, m, ExposureMask);
	XMapRaised(dpy, m);
	XGrabPointer(dpy, m, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
	    GrabModeAsync, None, None, CurrentTime);
	for (;;) {
		int dessine = 0;

		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.window == m)
			dessine = 1;
		else if (ev.type == MotionNotify) {
			int k = (ev.xmotion.y - 2) / ih;

			if (ev.xmotion.x < 0 || ev.xmotion.x >= mw || ev.xmotion.y < 2 ||
			    k >= RP_NB || !RP_ACTIF(k))
				k = -1;
			dessine = k != sel;
			sel = k;
		} else if (ev.type == ButtonRelease)
			break;
		if (dessine) {
			ptk_fill(&ptk, m, 0, 0, mw, mh, ptk.white);
			ptk_rect(&ptk, m, 0, 0, mw, mh, ptk.black);
			for (i = 0; i < RP_NB; i++) {
				if (i == sel)
					ptk_fill(&ptk, m, 1, 2 + i * ih, mw - 2, ih, ptk_color(&ptk, "#333399"));
				if (i == par)
					ptk_text(&ptk, m, ptk.font_menu, 6, 15 + i * ih, "\xe2\x88\x9a",
					    i == sel ? ptk.white : ptk.text);
				ptk_text(&ptk, m, ptk.font_menu, 20, 15 + i * ih, rp_noms[i],
				    i == sel ? ptk.white : RP_ACTIF(i) ? ptk.text : ptk.text_inactif);
			}
			XFlush(dpy);
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XDestroyWindow(dpy, m);
	return sel >= 0 ? sel : par;
}

static void
options_presentation(FWin *w)
{
	XSetWindowAttributes a;
	Window win;
	XEvent ev;
	int range, par, ok = -1;

	if (w == NULL || w->view == 2)
		return;
	range = w->range;
	par = w->range_par;
	memset(&a, 0, sizeof(a));
	a.override_redirect = True;
	a.background_pixel = ptk.face.pixel;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask;
	win = XCreateWindow(dpy, root, (sw - OP_W) / 2, ptk.menubar_h + 22, OP_W, OP_H, 0,
	    CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixel | CWEventMask, &a);
	XStoreName(dpy, win, "Options de présentation");
	XMapRaised(dpy, win);
	XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
	    GrabModeAsync, None, None, CurrentTime);
	XGrabKeyboard(dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	while (ok < 0) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.window != win) {
			FWin *f = find_win(ev.xexpose.window);

			if (f != NULL && ev.xexpose.count == 0)
				draw_window(f);
		} else if (ev.type == Expose && ev.xexpose.count == 0)
			op_dessiner(win, w, range, par, 0);
		else if (ev.type == ButtonPress) {
			int x = ev.xbutton.x, y = ev.xbutton.y, k;

			for (k = 0; k < 3; k++)	/* bouton radio ou son libellé */
				if (x >= 50 && x < 270 && y >= 116 + 24 * k && y < 134 + 24 * k &&
				    !(k == 2 && x >= OP_PX))
					range = k;
			if (range == RANGE_TRIEE && x >= OP_PX && x < OP_PX + OP_PW && y >= 162 && y < 180) {
				op_dessiner(win, w, range, par, 1);
				par = op_menu_local(win, OP_PX, 162, par);
				XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask,
				    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
			}
			op_dessiner(win, w, range, par, OP_ANNULER(x, y) ? 2 : OP_OK(x, y) ? 3 : 0);
		} else if (ev.type == ButtonRelease) {
			int x = ev.xbutton.x, y = ev.xbutton.y;

			if (OP_ANNULER(x, y))
				ok = 0;
			else if (OP_OK(x, y))
				ok = 1;
			else
				op_dessiner(win, w, range, par, 0);
		} else if (ev.type == KeyPress) {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);

			if (ks == XK_Return || ks == XK_KP_Enter)
				ok = 1;
			else if (ks == XK_Escape || (ks == XK_period && (ev.xkey.state & Mod1Mask)))
				ok = 0;
		}
	}
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XDestroyWindow(dpy, win);
	XFlush(dpy);
	if (!ok || (range == w->range && par == w->range_par))
		return;
	w->range = range;
	w->range_par = par;
	set_geom(w);	/* le bureau a sa ligne « __bureau__ », pour la disposition */
	/* le réglage s'applique aussitôt : rangement, ou alignement des icônes */
	if (range == RANGE_TRIEE)
		ranger_par(w, par);
	else if (range == RANGE_GRILLE)
		aligner(w, NULL);
	enregistrer_positions(w);
	draw_window(w);
	update_menu(w);
}

/* Réorganiser ▸ par … : un rangement ponctuel dans l'ordre de la clé */
static void
reorganiser(FWin *w, int cle)
{
	if (w == NULL || w->view == 2 || w->range == RANGE_TRIEE || !RP_ACTIF(cle))
		return;
	ranger_par(w, cle);
	enregistrer_positions(w);
	draw_window(w);
}

/*
 * Menus du Finder, contenu de Mac OS 9.2 : « ~ » élément grisé, « √ » coche, \x1f et \x1e
 * sous-menu. Ce qui ne s'applique pas à la sélection, ou n'est pas encore programmé, est
 * grisé. Republiés quand l'état change.
 */
static const char *
path_base(const char *path)
{
	const char *b = strrchr(path, '/');

	return b != NULL && b[1] != '\0' ? b + 1 : path;
}

static int
nb_selection(const FWin *w)
{
	int i, n = 0;

	for (i = 0; w != NULL && i < w->n; i++)
		n += w->icons[i].selected != 0;
	return n;
}

static void
update_menu(FWin *w)
{
	char m[6144], fen[1536];
	/* bureau au premier plan : les menus décrivent le bureau et sa sélection */
	int sel = nb_selection(bureau_devant && desktop != NULL ? desktop : w);
	int dans_corbeille = !bureau_devant && !w->is_desktop && strcmp(w->path, trash_dir) == 0;
	const char *on = sel ? "" : "~";
	FWin *o;

	fen[0] = '\0';
	for (o = wins; o != NULL; o = o->next) {
		char t[300];

		if (o->is_desktop || o->is_info)
			continue;
		snprintf(t, sizeof(t), "%s%s|", o == active ? COCHE : "", path_base(o->path));
		if (strlen(fen) + strlen(t) < sizeof(fen) - 1)
			strcat(fen, t);
	}
	if (fen[0] == '\0')
		snprintf(fen, sizeof(fen), "~Aucune fenêtre|");
	fen[strlen(fen) - 1] = '\0';
	char ro[400];

	menu_rangement(w, ro, sizeof(ro), '|');
	snprintf(m, sizeof(m),
	    "Fichier|Nouveau dossier\t⌘N|%sOuvrir\t⌘O|~Imprimer\t⌘P|"
	    "%sPlacer dans la Corbeille\t⌘⌫|%sFermer la fenêtre\t⌘W|-|"
	    "%sLire les informations" SOUS "Général\t⌘I" ET "~Partage…" ET "~Mémoire…|"
	    "~Étiquette|%sDupliquer\t⌘D|Créer un alias\t⌘M|~Ajouter aux favoris\t⌘T|"
	    "%sRanger\t⌘Y|~Coder|-|~Rechercher…\t⌘F|~Rechercher sur Internet…\t⌘H|"
	    "~Afficher l'original\t⌘R|-|~Format d'impression…|~Imprimer le bureau…\n"
	    "Édition|~Annuler\t⌘Z|-|%sCouper\t⌘X|%sCopier\t⌘C|%sColler\t⌘V|~Effacer|"
	    "Tout sélectionner\t⌘A|-|~Presse-papiers|~Préférences…\n"
	    "Présentation|%spar icônes|%spar boutons|%spar liste|-|" COCHE "en fenêtre|"
	    "~en fenêtre flottante|-|%s|~Réinitialiser les colonnes|-|"
	    "%sOptions de présentation…\t⌘J\n"
	    "Fenêtre|%s\n"
	    "Spécial|Vider la Corbeille…|-|~Éjecter\t⌘E|~Effacer le disque…|-|"
	    "Suspendre|Redémarrer|Éteindre\n"
	    "Aide|Aide Platine",
	    on, on, w->is_desktop ? "~" : "", on, on,
	    (sel && dans_corbeille) ? "" : "~",
	    on, on, pp_n > 0 || XGetSelectionOwner(dpy, a_clip) != None ? "" : "~",
	    w->view == 0 ? COCHE : "", w->view == 1 ? COCHE : "", w->view == 2 ? COCHE : "",
	    ro, OPT_PRES(w), fen);
	if (strcmp(m, w->menu_publie) != 0) {
		snprintf(w->menu_publie, sizeof(w->menu_publie), "%s", m);
		set_utf8(w->win, a_menu, m);
	}
}

static void
dupliquer(FWin *w)
{
	int i;

	for (i = 0; w != NULL && i < w->n; i++) {
		char src[PATH_MAX], dst[PATH_MAX];
		int k;

		/* le disque et la Corbeille du bureau ne se dupliquent pas */
		if (!w->icons[i].selected || (w->is_desktop && i < BUREAU_FIXES))
			continue;
		path_concat(src, sizeof(src), w->path, "/");
		strncat(src, w->icons[i].name, sizeof(src) - strlen(src) - 1);
		snprintf(dst, sizeof(dst), "%.4000s copie", src);
		for (k = 2; access(dst, F_OK) == 0 && k < 100; k++)
			snprintf(dst, sizeof(dst), "%.4000s copie %d", src, k);
		fsops_copy_tree(src, dst);
	}
	refresh_window(w);
}

/* Fichier > Créer un alias : un fichier « <nom> alias » pointant vers
 * l'élément (référencé par son chemin absolu, comme sous Mac OS 9). */
static void
creer_alias(FWin *w)
{
	int i, fait = 0;

	if (w == NULL || w->is_info)
		return;
	for (i = 0; i < w->n; i++) {
		char src[PATH_MAX], dst[PATH_MAX], reel[PATH_MAX];
		int k;

		if (!w->icons[i].selected)
			continue;
		/* l'alias est posé à côté de l'élément ; celui du disque ou de la
		 * Corbeille, dans le dossier du bureau */
		join_path(src, sizeof(src), w->path, w->icons[i].name);
		snprintf(dst, sizeof(dst), "%.4000s alias", src);
		for (k = 2; access(dst, F_OK) == 0 && k < 100; k++)
			snprintf(dst, sizeof(dst), "%.4000s alias %d", src, k);
		if (realpath(w->icons[i].path, reel) == NULL)
			continue;
		if (alias_ecrire(reel, dst) != 0) {
			drop_error("impossible de créer l'alias", dst);
			continue;
		}
		fait = 1;
	}
	if (fait) {
		ptk_son(&ptk, "fnew");
		refresh_window(w);
	}
}

/* le premier plan a changé : barres et bandeaux de toutes les fenêtres de dossier suivent */
static void
redessiner_dossiers(void)
{
	FWin *o;

	for (o = wins; o != NULL; o = o->next)
		if (!o->is_desktop && !o->is_info)
			draw_window(o);
}

/* contour de l'icône et du nom d'une barre de titre, qui reviennent à leur place */
static void
retour_titre(FWin *w, int x, int y, int ix, int iy)
{
	PtkZoom z = { NULL, 0, 0 };
	int tw = ptk_text_width(&ptk, ptk.font_small, path_base(w->path));

	ptk_zoom_icone(&z, arg_dossier16, ix, iy);
	ptk_zoom_rect(&z, ix + 18, iy + 1, tw + 4, 14);
	ptk_zoom_retour(&ptk, &z, x - 9 - ix, y - 9 - iy);
}

/* icône du titre déposée : le dossier de la fenêtre va dans le dossier visé (jamais en lui-même) */
static void
deposer_dossier(FWin *w, const char *pos)
{
	char dest[PATH_MAX], neuf[PATH_MAX];
	const char *h = getenv("HOME"), *base;
	FWin *t = NULL;
	int x, y, ix = 0, iy = 0, idx, lx, ly, sur;
	size_t l;

	if (w == NULL || w->is_desktop || sscanf(pos, "%d %d %d %d", &x, &y, &ix, &iy) != 4)
		return;
	if (!resolve_drop(x, y, &t, &idx, &lx, &ly, dest, sizeof(dest), &sur) || t == w) {
		retour_titre(w, x, y, ix, iy);
		return;
	}
	l = strlen(w->path);
	if ((h != NULL && strcmp(w->path, h) == 0) || (strncmp(dest, w->path, l) == 0 &&
	    (dest[l] == '/' || dest[l] == '\0'))) {
		retour_titre(w, x, y, ix, iy);
		return;
	}
	base = strrchr(w->path, '/');
	base = base != NULL ? base + 1 : w->path;
	if (snprintf(neuf, sizeof(neuf), "%s/%s", dest, base) >= (int)sizeof(neuf) ||
	    access(neuf, F_OK) == 0 || fsops_move_tree(w->path, neuf) != 0) {
		retour_titre(w, x, y, ix, iy);
		return;
	}
	snprintf(w->path, sizeof(w->path), "%s", neuf);
	ptk_son(&ptk, "fdrp");
	refresh_after_drop();
}

/* icône de titre d'une autre application, lâchée sur le Finder (le WM transmet
 * « X Y IX IY\tCHEMIN ») : un alias du fichier de la fenêtre est créé à l'endroit visé ;
 * ailleurs, le contour revient à la barre de titre. */
static void
deposer_alias(const char *arg)
{
	char dest[PATH_MAX], dst[PATH_MAX], reel[PATH_MAX], nom[NAME_MAX + 1];
	const char *chemin = strchr(arg, '\t'), *base;
	FWin *t = NULL;
	int x, y, ix, iy, idx, lx, ly, sur, k;

	if (chemin == NULL || sscanf(arg, "%d %d %d %d", &x, &y, &ix, &iy) != 4)
		return;
	chemin++;
	base = path_base(chemin);
	drag_win = NULL;
	if (!resolve_drop(x, y, &t, &idx, &lx, &ly, dest, sizeof(dest), &sur) ||
	    realpath(chemin, reel) == NULL) {
		PtkZoom z = { NULL, 0, 0 };

		ptk_zoom_icone(&z, arg_dossier16, ix, iy);
		ptk_zoom_rect(&z, ix + 18, iy + 1, ptk_text_width(&ptk, ptk.font_small, base) + 4, 14);
		ptk_zoom_retour(&ptk, &z, x - 9 - ix, y - 9 - iy);
		return;
	}
	snprintf(nom, sizeof(nom), "%.200s alias", base);
	snprintf(dst, sizeof(dst), "%.3800s/%s", dest, nom);
	for (k = 2; access(dst, F_OK) == 0 && k < 100; k++) {
		snprintf(nom, sizeof(nom), "%.200s alias %d", base, k);
		snprintf(dst, sizeof(dst), "%.3800s/%s", dest, nom);
	}
	if (alias_ecrire(reel, dst) != 0) {
		drop_error("impossible de créer l'alias", dst);
		return;
	}
	/* posé sous le pointeur dans une fenêtre par icônes ; sur une icône de dossier : rangé dedans */
	if (!sur && t != NULL && t->is_desktop)
		set_pos(folder_key(t), nom, lx - ICON_SIZE / 2, ly - 16);
	else if (!sur && t != NULL && t->view == 0)
		set_pos(folder_key(t), nom, lx - cell_w(t) / 2 + t->scroll_x, ly - 16 + t->scroll);
	ptk_son(&ptk, "fdrp");
	refresh_after_drop();
}

/* recharger retire les dépliages de la liste, et remet le bon ordre pour la vue choisie */
static void
changer_vue(FWin *w, int vue)
{
	w->view = vue;
	load_dir(w);
	draw_window(w);
}

/* délai du double-clic, réglé dans le tableau de bord Souris ; 32 tics par défaut, comme Mac OS */
static unsigned long
double_clic_ms(void)
{
	Atom t;
	int f;
	unsigned long n, r, ms = 533;
	unsigned char *v = NULL;

	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_PLATINE_DOUBLE_CLIC", False), 0, 1, False,
	    XA_CARDINAL, &t, &f, &n, &r, &v) == Success && v != NULL) {
		if (n == 1)
			ms = (unsigned long)*(long *)v;
		XFree(v);
	}
	return ms;
}

/* motif du bureau choisi dans le tableau de bord Apparence */
static void
charger_motif(const char *nom)
{
	char c[256];
	int tw, th;
	Pixmap t;

	if (strcmp(nom, "Mac OS") == 0)
		snprintf(c, sizeof(c), "fond.ppm");
	else
		snprintf(c, sizeof(c), "motifs/%s.ppm", nom);
	if ((t = ptk_load_ppm(&ptk, c, &tw, &th)) == None)
		return;
	fond_tile = t;
	fond_w = tw;
	fond_h = th;
}

static void
motif_du_reglage(void)
{
	char c[PATH_MAX], l[256];
	const char *h = getenv("HOME");
	FILE *f;

	snprintf(c, sizeof(c), "%s/.config/platine/reglages", h != NULL ? h : ".");
	if ((f = fopen(c, "r")) == NULL)
		return;
	while (fgets(l, sizeof(l), f) != NULL)
		if (strncmp(l, "motif_bureau ", 13) == 0) {
			l[strcspn(l, "\n")] = '\0';
			charger_motif(l + 13);
		}
	fclose(f);
}

/* commande système lancée sans attendre */
static void
lancer_systeme(const char *cmd)
{
	if (fork() == 0) {
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
}

/* le bureau est-il au premier plan (dernier clic sur le bureau) ? voir wm.c */
static int
bureau_au_premier_plan(void)
{
	Atom t;
	int f, oui = 0;
	unsigned long n, r;
	unsigned char *v = NULL;

	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), XInternAtom(dpy, "_PLATINE_BUREAU", False),
	    0, 1, False, AnyPropertyType, &t, &f, &n, &r, &v) == Success && v != NULL) {
		oui = n > 0;
		XFree(v);
	}
	return oui;
}

static int
pp_interne(void)
{
	Window owner = XGetSelectionOwner(dpy, a_clip);
	FWin *w;

	for (w = wins; w != NULL; w = w->next)
		if (w->win == owner)
			return 1;
	return 0;
}

static char *
pp_texte(int uri, char chemins[][PATH_MAX], int count)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t cap = 1, n = 0;
	char *s;
	int i;

	for (i = 0; i < count; i++)
		cap += strlen(chemins[i]) * 3 + 16;
	if ((s = malloc(cap)) == NULL)
		return NULL;
	for (i = 0; i < count; i++) {
		const unsigned char *p = (const unsigned char *)chemins[i];

		if (uri) {
			memcpy(s + n, "file://", 7);
			n += 7;
		}
		for (; *p != '\0'; p++) {
			if (uri && !((*p >= 'a' && *p <= 'z') ||
			    (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
			    strchr("/-._~", *p) != NULL)) {
				s[n++] = '%';
				s[n++] = hex[*p >> 4];
				s[n++] = hex[*p & 15];
			} else {
				s[n++] = (char)*p;
			}
		}
		if (uri)
			s[n++] = '\r';
		s[n++] = '\n';
	}
	s[n] = '\0';
	return s;
}

static void
pp_requete(XSelectionRequestEvent *q)
{
	XEvent r;
	Atom prop = q->property != None ? q->property : q->target;
	char (*chemins)[PATH_MAX] = pp_chemins;
	int count = pp_n;
	char *s = NULL;

	if (q->selection == a_xdnd_selection) {
		chemins = xdnd_chemins;
		count = xdnd_n;
	}
	memset(&r, 0, sizeof(r));
	r.xselection.type = SelectionNotify;
	r.xselection.requestor = q->requestor;
	r.xselection.selection = q->selection;
	r.xselection.target = q->target;
	r.xselection.time = q->time;
	r.xselection.property = None;
	if (count > 0 && q->target == a_targets) {
		Atom cibles[] = { a_targets, a_uri_list, a_utf8, XA_STRING };

		XChangeProperty(dpy, q->requestor, prop, XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)cibles, 4);
		r.xselection.property = prop;
	} else if (count > 0 && (q->target == a_uri_list ||
	    q->target == a_utf8 || q->target == XA_STRING)) {
		s = pp_texte(q->target == a_uri_list, chemins, count);
		if (s != NULL) {
			XChangeProperty(dpy, q->requestor, prop, q->target, 8,
			    PropModeReplace, (unsigned char *)s, (int)strlen(s));
			r.xselection.property = prop;
		}
	}
	free(s);
	XSendEvent(dpy, q->requestor, False, 0, &r);
	XFlush(dpy);
}

static int
pp_hex(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int
pp_decode(const char *in, char *out, size_t outsz)
{
	const char *p = in;
	size_t n = 0;

	if (strncmp(p, "file://", 7) == 0) {
		p += 7;
		if (*p != '/') {
			if (strncmp(p, "localhost/", 10) != 0)
				return -1;
			p += 9;
		}
	}
	if (*p != '/')
		return -1;
	while (*p != '\0') {
		int c = (unsigned char)*p++;

		if (c == '%') {
			int h, l;

			if (p[0] == '\0' || p[1] == '\0')
				return -1;
			h = pp_hex(p[0]);
			l = pp_hex(p[1]);
			if (h < 0 || l < 0)
				return -1;
			p += 2;
			c = h * 16 + l;
			if (c == 0)
				return -1;
		}
		if (n + 1 >= outsz)
			return -1;
		out[n++] = (char)c;
	}
	out[n] = '\0';
	return 0;
}

static int
pp_lire(const unsigned char *data, unsigned long n, int uri,
    char chemins[][PATH_MAX])
{
	char *copie, *s;
	int count = 0;

	if ((copie = malloc(n + 1)) == NULL)
		return 0;
	memcpy(copie, data, n);
	copie[n] = '\0';
	s = copie;
	while (*s != '\0') {
		char *line = s;
		char *fin = strpbrk(s, "\r\n");

		if (fin != NULL) {
			char delim = *fin;

			*fin = '\0';
			s = fin + 1;
			if (delim == '\r' && *s == '\n')
				s++;
		} else {
			s += strlen(s);
		}
		if (*line == '\0' || (uri && *line == '#'))
			continue;
		if (count < PP_MAX &&
		    pp_decode(line, chemins[count], sizeof(chemins[count])) == 0)
			count++;
	}
	free(copie);
	return count;
}

static void
copier_selection(FWin *w, int couper)
{
	FWin *src = bureau_devant && desktop != NULL ? desktop : w;
	int i, n = 0;

	if (src == NULL)
		return;
	for (i = 0; i < src->n && n < PP_MAX; i++) {
		if (!src->icons[i].selected)
			continue;
		if (couper && protected_source(src->icons[i].path))
			continue;
		join_path(pp_chemins[n], sizeof(pp_chemins[n]), src->path,
		    src->icons[i].name);
		n++;
	}
	pp_n = n;
	pp_couper = couper && n > 0;
	if (n > 0) {
		XSetSelectionOwner(dpy, a_clip, src->win, CurrentTime);
		ptk_son(&ptk, couper ? "coup" : "copi");
		/* l'état « Coller » du menu a peut-être changé */
		if (w != NULL)
			update_menu(w);
	}
}

static void
coller_chemins(FWin *w, char chemins[][PATH_MAX], int count, int couper)
{
	char cible[PATH_MAX];
	int i, fait = 0;

	if (w == NULL || w->is_info || count <= 0)
		return;
	snprintf(cible, sizeof(cible), "%s", w->path);
	for (i = 0; i < count; i++) {
		const char *base = strrchr(chemins[i], '/');
		char dst[PATH_MAX];
		struct stat st;

		base = base != NULL ? base + 1 : chemins[i];
		join_path(dst, sizeof(dst), cible, base);
		if (strcmp(dst, chemins[i]) == 0)
			continue;
		/* un dossier ne peut pas être déplacé dans lui-même */
		if (couper && lstat(chemins[i], &st) == 0 && S_ISDIR(st.st_mode)) {
			char real_src[PATH_MAX], real_dst[PATH_MAX];

			if (realpath(chemins[i], real_src) != NULL &&
			    realpath(cible, real_dst) != NULL &&
			    path_within(real_dst, real_src)) {
				drop_error("un dossier ne peut pas être déplacé dans lui-même",
				    chemins[i]);
				break;
			}
		}
		if (lstat(dst, &st) == 0) {
			/* conflit assisté : remplacer = détruire puis copier */
			char msg[600];
			int remplacer;

			snprintf(msg, sizeof(msg),
			    "Un élément nommé « %s » existe déjà.\n"
			    "Voulez-vous le remplacer ?", base);
			ptk_son(&ptk, "lerr");
			remplacer = finder_alert(msg, "Arrêter", "Remplacer",
			    None, None);
			if (!remplacer)
				break;
			if (S_ISDIR(st.st_mode))
				remove_tree(dst);
			else
				unlink(dst);
		}
		if (couper) {
			if (fsops_move_tree(chemins[i], dst) != 0) {
				drop_error("le déplacement a échoué", dst);
				break;
			}
		} else if (fsops_copy_tree(chemins[i], dst) != 0) {
			drop_error("la copie a échoué", dst);
			break;
		}
		/* commentaires, positions et métadonnées suivent l'élément */
		remap_metadata(chemins[i], dst, !couper);
		fait = 1;
	}
	if (couper) {
		pp_n = 0;	/* le couper se consomme, même partiellement */
		pp_couper = 0;
		if (pp_interne())
			XSetSelectionOwner(dpy, a_clip, None, CurrentTime);
	}
	if (fait) {
		ptk_son(&ptk, "fnew");
		refresh_after_drop();
		if (w != NULL)
			update_menu(w);
	}
}

static void
coller(FWin *w)
{
	if (w == NULL || w->is_info)
		return;
	if (pp_n > 0 && pp_interne()) {
		coller_chemins(w, pp_chemins, pp_n, pp_couper);
		return;
	}
	if (XGetSelectionOwner(dpy, a_clip) != None) {
		XConvertSelection(dpy, a_clip, a_uri_list, a_clip, w->win, CurrentTime);
		XFlush(dpy);
	}
}

static void
handle_command(FWin *w, const char *cmd)
{
	if (strncmp(cmd, "__depot ", 8) == 0) {
		deposer_dossier(w, cmd + 8);
		return;
	}
	if (strncmp(cmd, "__alias ", 8) == 0) {
		deposer_alias(cmd + 8);
		return;
	}
	if (strcmp(cmd, "Nouveau dossier") == 0)
		new_folder(w);
	else if (strcmp(cmd, "Ouvrir") == 0)
		open_selected(w);
	else if (strcmp(cmd, "Renommer") == 0)
		start_rename(w);
	else if (strcmp(cmd, "Général") == 0 ||
	    strcmp(cmd, "Lire les informations") == 0)
		open_info_for(w, w ? w->selected : -1);
	else if (strcmp(cmd, "Nettoyer") == 0)
		arrange_icons(w);
	else if (strcmp(cmd, "Options de présentation…") == 0)
		options_presentation(w);
	else if (strncmp(cmd, "par ", 4) == 0 && w != NULL) {
		int k;

		for (k = 0; k < RP_NB; k++)
			if (strcmp(cmd, rp_noms[k]) == 0)
				reorganiser(w, k);
	}
	else if (strcmp(cmd, "Fermer la fenêtre") == 0)
		close_window(w);
	else if (strcmp(cmd, "Placer dans la Corbeille") == 0)
		move_to_trash(w);
	else if (strcmp(cmd, "Ranger") == 0)
		restore_selection(w);
	else if (strcmp(cmd, "Vider la Corbeille…") == 0)
		empty_trash();
	else if (strcmp(cmd, "Suspendre") == 0)
		lancer_systeme(SYS_PRIVILEGE " veille");
	else if (strcmp(cmd, "Redémarrer") == 0)
		demander_arret(2);
	else if (strcmp(cmd, "Éteindre") == 0)
		demander_arret(1);
	else if (strcmp(cmd, "Actualiser") == 0) {
		refresh_window(w);
	} else if (strcmp(cmd, "par icônes") == 0) {
		if (w != NULL) { changer_vue(w, 0); }
	} else if (strcmp(cmd, "par liste") == 0) {
		if (w != NULL) { changer_vue(w, 2); }
	} else if (strcmp(cmd, "par boutons") == 0) {
		if (w != NULL) { changer_vue(w, 1); }
	} else if (cmd[0] == '/' || cmd[0] == '~') {
		char p[PATH_MAX];
		expand_path(cmd, p, sizeof(p));
		navigate_to(w, p);
	}
	else if (strcmp(cmd, "Dupliquer") == 0)
		dupliquer(w);
	else if (strcmp(cmd, "Couper") == 0)
		copier_selection(w, 1);
	else if (strcmp(cmd, "Copier") == 0)
		copier_selection(w, 0);
	else if (strcmp(cmd, "Coller") == 0)
		coller(w);
	else if (strcmp(cmd, "Créer un alias") == 0)
		creer_alias(w);
	else if (strcmp(cmd, "Tout sélectionner") == 0 && w != NULL) {
		int i;
		for (i = 0; i < w->n; i++)
			w->icons[i].selected = 1;
		draw_window(w);
	} else if (strcmp(cmd, "Fenêtre") != 0) {
		/* menu Fenêtre : nom d'une fenêtre ouverte, à ramener au premier plan */
		FWin *o;
		for (o = wins; o != NULL; o = o->next)
			if (!o->is_desktop && !o->is_info &&
			    strcmp(cmd, path_base(o->path)) == 0) {
				XRaiseWindow(dpy, o->win);
				XSetInputFocus(dpy, o->win, RevertToParent, CurrentTime);
				break;
			}
	}
	if (strcmp(cmd, "Aide Platine") == 0 || strcmp(cmd, "Aide") == 0)
		system("command -v platine-aide >/dev/null 2>&1 && "
		    "platine-aide &");
}

static char *
read_command(Window win)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = NULL;
	char *out = NULL;

	if (XGetWindowProperty(dpy, win, a_cmd, 0, 1024, False,
	    AnyPropertyType, &type, &format, &n, &after, &data) == Success &&
	    data != NULL) {
		out = malloc(n + 1);
		if (out) {
			memcpy(out, data, n);
			out[n] = '\0';
		}
		XFree(data);
	}
	return out;
}

static FWin *
find_win(Window win)
{
	FWin *w;
	for (w = wins; w; w = w->next)
		if (w->win == win)
			return w;
	return NULL;
}

static void
restore_drag(void)
{
	int k;

	set_drop_hover(NULL, -1);
	spring_armed = 0;
	spring_path[0] = '\0';
	if (drag_win == NULL)
		return;
	for (k = 0; k < drag_group_n; k++) {
		Icon *ic = &drag_win->icons[drag_group_idx[k]];

		ic->x = drag_group_x[k];
		ic->y = drag_group_y[k];
	}
	draw_window(drag_win);
	XSync(dpy, False);
	retour_anime();
}

static void
set_drop_hover(FWin *w, int idx)
{
	FWin *old = drop_hover_win;
	int old_idx = drop_hover_idx;

	if (old == w && old_idx == idx)
		return;
	drop_hover_win = w;
	drop_hover_idx = idx;
	if (old != NULL)
		draw_window(old);
	if (w != NULL && w != old)
		draw_window(w);
}

/* l'icône est-elle un programme, ou l'alias d'un programme ? « bin » reçoit son
 * chemin réel : un document déposé dessus s'ouvre avec lui, comme sous Mac OS 9 */
static int
programme_de_depot(const Icon *ic, char *bin, size_t n)
{
	Icon t;
	char cible[PATH_MAX];

	if (ic == NULL || ic->is_dir || ic->kind == 2 || ic->kind == 3)
		return 0;
	t = *ic;
	if (ic->is_alias) {
		if (!alias_lire(ic->path, cible, sizeof(cible)) &&
		    realpath(ic->path, cible) == NULL)
			return 0;
		snprintf(t.path, sizeof(t.path), "%s", cible);
		t.is_alias = 0;
	}
	if (!est_programme(&t, NULL, 0))
		return 0;
	if (bin != NULL)
		snprintf(bin, n, "%s", t.path);
	return 1;
}

static int
path_within(const char *path, const char *dir)
{
	size_t n = strlen(dir);

	return strncmp(path, dir, n) == 0 &&
	    (path[n] == '\0' || path[n] == '/');
}

static int
remap_path(const char *path, const char *old, const char *new,
    char *out, size_t outsz)
{
	size_t n;

	if (!path_within(path, old))
		return 0;
	n = strlen(old);
	if (snprintf(out, outsz, "%s%s", new, path + n) >= (int)outsz)
		return 0;
	return 1;
}

static void
remap_comments(const char *old, const char *new, int copy)
{
	char file[PATH_MAX], tmp[PATH_MAX], line[8192];
	FILE *in, *out;

	path_concat(file, sizeof(file), state_dir, "/commentaires");
	path_concat(tmp, sizeof(tmp), state_dir, "/commentaires.tmp");
	in = fopen(file, "r");
	if (in == NULL)
		return;
	out = fopen(tmp, "w");
	if (out == NULL) {
		fclose(in);
		return;
	}
	while (fgets(line, sizeof(line), in) != NULL) {
		char *tab = strchr(line, '\t');
		char mapped[PATH_MAX];

		if (tab == NULL)
			continue;
		*tab = '\0';
		if (copy)
			fprintf(out, "%s\t%s", line, tab + 1);
		if (remap_path(line, old, new, mapped, sizeof(mapped)))
			fprintf(out, "%s\t%s", mapped, tab + 1);
		else if (!copy)
			fprintf(out, "%s\t%s", line, tab + 1);
	}
	fclose(in);
	if (fclose(out) == 0)
		rename(tmp, file);
	else
		unlink(tmp);
}

static void
remap_types(const char *old, const char *new, int copy)
{
	TPos *t;

	for (t = types_db; t != NULL; t = t->next) {
		char mapped[PATH_MAX];

		if (!remap_path(t->path, old, new, mapped, sizeof(mapped)))
			continue;
		if (copy) {
			TPos *nt = calloc(1, sizeof(*nt));

			if (nt == NULL)
				continue;
			*nt = *t;
			snprintf(nt->path, sizeof(nt->path), "%s", mapped);
			nt->next = types_db;
			types_db = nt;
		} else {
			snprintf(t->path, sizeof(t->path), "%s", mapped);
		}
	}
	types_save();
}

static void
remap_metadata(const char *old, const char *new, int copy)
{
	IPos *p;
	WGeom *g;
	FWin *w;

	for (p = positions; p != NULL; p = p->next) {
		char mapped[PATH_MAX];

		if (!remap_path(p->folder, old, new, mapped, sizeof(mapped)))
			continue;
		if (copy) {
			IPos *np = calloc(1, sizeof(*np));

			if (np == NULL)
				continue;
			*np = *p;
			snprintf(np->folder, sizeof(np->folder), "%s", mapped);
			np->next = positions;
			positions = np;
		} else {
			snprintf(p->folder, sizeof(p->folder), "%s", mapped);
		}
	}
	for (g = geoms; g != NULL; g = g->next) {
		char mapped[PATH_MAX];

		if (!remap_path(g->path, old, new, mapped, sizeof(mapped)))
			continue;
		if (copy) {
			WGeom *ng = calloc(1, sizeof(*ng));

			if (ng == NULL)
				continue;
			*ng = *g;
			snprintf(ng->path, sizeof(ng->path), "%s", mapped);
			ng->next = geoms;
			geoms = ng;
		} else {
			snprintf(g->path, sizeof(g->path), "%s", mapped);
		}
	}
	if (!copy) {
		for (w = wins; w != NULL; w = w->next) {
			char mapped[PATH_MAX];
			int i, changed = 0;

			if (w->is_desktop)
				continue;
			if (remap_path(w->path, old, new, mapped,
			    sizeof(mapped))) {
				snprintf(w->path, sizeof(w->path), "%s", mapped);
				changed = 1;
			}
			for (i = 0; i < w->hist_n; i++) {
				if (remap_path(w->hist[i], old, new, mapped,
				    sizeof(mapped))) {
					snprintf(w->hist[i], sizeof(w->hist[i]),
					    "%s", mapped);
					changed = 1;
				}
			}
			if (changed) {
				set_addr(w);
				XStoreName(dpy, w->win, display_name(w->path));
			}
		}
	}
	remap_comments(old, new, copy);
	remap_types(old, new, copy);
}

static int
protected_source(const char *path)
{
	char p[PATH_MAX];

	static const char *const du_volume[] = { "Applications", "System", "Library", "home" };
	static const char *const perso[] = { "Documents", "Pictures", "Desktop", "Downloads",
	    "Music", "Movies", "Library", "Images" };
	size_t i;

	if (strcmp(path, vol_dir) == 0 || strcmp(path, home_dir) == 0 ||
	    strcmp(path, trash_dir) == 0 || strcmp(path, bureau_dir) == 0)
		return 1;
	for (i = 0; i < sizeof(du_volume) / sizeof(du_volume[0]); i++) {
		join_path(p, sizeof(p), vol_dir, du_volume[i]);
		if (strcmp(path, p) == 0)
			return 1;
	}
	for (i = 0; i < sizeof(perso) / sizeof(perso[0]); i++) {
		join_path(p, sizeof(p), home_dir, perso[i]);
		if (strcmp(path, p) == 0)
			return 1;
	}
	return 0;
}

static void
drop_error(const char *message, const char *path)
{
	fprintf(stderr, "platine-finder: %s%s%s\n", message,
	    path != NULL ? " : " : "", path != NULL ? path : "");
	ptk_son(&ptk, "lerr");
	XFlush(dpy);
}

/* Dossier à ressort : après un maintien sur une icône de dossier pendant un
 * glisser, la fenêtre du dossier s'ouvre sous le pointeur. Le dépôt suivant
 * atterrit donc dans ce dossier, comme dans Mac OS 9. */
static void
spring_fire(void)
{
	char path[PATH_MAX];
	FWin *w;
	int k, x, y;

	snprintf(path, sizeof(path), "%s", spring_path);
	spring_armed = 0;
	spring_path[0] = '\0';
	if (drag_win == NULL || path[0] == '\0' || drag_group_n <= 0)
		return;
	/* Un élément ne peut pas être déposé dans lui-même ni dans un
	 * descendant de lui-même. */
	for (k = 0; k < drag_group_n; k++) {
		const char *src = drag_win->icons[drag_group_idx[k]].path;

		if (path_within(path, src))
			return;
	}
	if (drop_hover_win != NULL && drop_hover_idx >= 0 &&
	    drop_hover_idx < drop_hover_win->n)
		zoom_source_de(drop_hover_win, drop_hover_idx);
	open_path(path);
	w = NULL;
	{
		FWin *it;

		for (it = wins; it != NULL; it = it->next) {
			if (!it->is_desktop && strcmp(it->path, path) == 0) {
				w = it;
				break;
			}
		}
	}
	if (w == NULL)
		return;
	/* Centrer la fenêtre sous le curseur, en gardant la barre de titre et
	 * le contenu accessibles. */
	x = drag_last_x - 120;
	y = drag_last_y - 90;
	if (x < 0)
		x = 0;
	if (y < ptk.menubar_h)
		y = ptk.menubar_h;
	if (x + w->w > sw)
		x = sw - w->w;
	if (y + w->h > sh)
		y = sh - w->h;
	w->x = x;
	w->y = y;
	XMoveWindow(dpy, w->win, x, y);
	XRaiseWindow(dpy, w->win);
	set_geom(w);
	draw_window(w);
	XFlush(dpy);
}

static FWin *
drop_window_at(int xr, int yr, int *x, int *y)
{
	Window cur = root;
	int depth;

	for (depth = 0; depth < 32; depth++) {
		Window rr, child = None;
		int rx, ry, wx, wy;
		unsigned int mask;
		FWin *w = find_win(cur);

		if (w != NULL) {
			Window ignored;

			if (XTranslateCoordinates(dpy, root, cur, xr, yr,
			    x, y, &ignored))
				return w;
			return NULL;
		}
		if (!XQueryPointer(dpy, cur, &rr, &child, &rx, &ry,
		    &wx, &wy, &mask) || child == None)
			break;
		cur = child;
	}
	return NULL;
}

static int
icon_cible(FWin *w, int px, int py, int *out)
{
	int r;

	icon_at_sauter_glisse = 1;
	r = icon_at(w, px, py, out);
	icon_at_sauter_glisse = 0;
	return r;
}

static int
resolve_drop(int xr, int yr, FWin **target, int *target_idx,
    int *local_x, int *local_y, char *dest, size_t destsz, int *on_folder)
{
	FWin *w = drop_window_at(xr, yr, local_x, local_y);
	int idx = -1;

	if (w == NULL || w->is_info)
		return 0;
	if (!w->is_desktop) {
		if (*local_x < 4 || *local_x >= w->w - 4 ||
		    *local_y < CONTENT_TOP || *local_y >= w->h - BARRE ||
		    *local_x >= w->w - BARRE)
			return 0;
	}
	if (icon_cible(w, *local_x, *local_y, &idx) && w == drag_win &&
	    w->icons[idx].selected)
		idx = -1;
	if (idx >= 0 && w->icons[idx].is_dir) {
		if (snprintf(dest, destsz, "%s", w->icons[idx].path) >=
		    (int)destsz)
			return 0;
		*on_folder = 1;
	} else {
		if (snprintf(dest, destsz, "%s", w->path) >= (int)destsz)
			return 0;
		*on_folder = 0;
	}
	*target = w;
	*target_idx = idx;
	return 1;
}

/* en fenêtre, x est le coin de la case (image à x + 6 par icônes, centrée par
 * boutons) ; sur le bureau, le bord de l'image : passer de l'un à l'autre garde
 * l'image sous le pointeur */
static int
decalage_image(FWin *w)
{
	return w->is_desktop ? 0 : w->view == 1 ? (cell_w(w) - ICON_SIZE) / 2 : 6;
}

static int
decalage_repere(FWin *src, FWin *dst)
{
	if (src == NULL || dst == NULL)
		return 0;
	return decalage_image(src) - decalage_image(dst);
}

static void
clamp_drop_delta(FWin *w, int *dx, int *dy)
{
	int maxx = w->is_desktop ? DESKTOP_ICON_X(w) : w->w - cell_w(w);
	int maxy = w->h - cell_h(w);
	int miny = w->is_desktop ? 2 : CONTENT_TOP + 3;
	int min_gx = INT_MAX, min_gy = INT_MAX;
	int max_gx = INT_MIN, max_gy = INT_MIN;
	int k;

	for (k = 0; k < drag_group_n; k++) {
		if (drag_group_x[k] < min_gx)
			min_gx = drag_group_x[k];
		if (drag_group_y[k] < min_gy)
			min_gy = drag_group_y[k];
		if (drag_group_x[k] > max_gx)
			max_gx = drag_group_x[k];
		if (drag_group_y[k] > max_gy)
			max_gy = drag_group_y[k];
	}
	if (*dx < -min_gx)
		*dx = -min_gx;
	if (*dx > maxx - max_gx)
		*dx = maxx - max_gx;
	if (*dy < miny - min_gy)
		*dy = miny - min_gy;
	if (*dy > maxy - max_gy)
		*dy = maxy - max_gy;
}

static void
refresh_after_drop(void)
{
	FWin *w;

	for (w = wins; w != NULL; w = w->next)
		if (!w->is_info)
			refresh_window(w);
}

static int
xdnd_version(Window win)
{
	Atom type;
	int format, version = -1;
	unsigned long n, after;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, win, a_xdnd_aware, 0, 1, False, XA_ATOM,
	    &type, &format, &n, &after, &data) == Success && data != NULL) {
		if (format == 32 && n > 0)
			version = (int)*(long *)data;
		XFree(data);
	}
	return version > 5 ? 5 : version;
}

static Window
xdnd_cherche(Window parent, int x, int y, int depth)
{
	Window r, p, *children = NULL;
	unsigned int i, n = 0;
	Window found = None;

	if (depth >= 16 || !XQueryTree(dpy, parent, &r, &p, &children, &n))
		return None;
	for (i = n; i > 0 && found == None; i--) {
		Window child = children[i - 1];
		XWindowAttributes a;

		if ((fantome_pret && child == fantome.win) ||
		    XGetWindowAttributes(dpy, child, &a) == 0 ||
		    a.map_state != IsViewable || x < a.x || y < a.y ||
		    x >= a.x + a.width || y >= a.y + a.height)
			continue;
		found = xdnd_cherche(child, x - a.x, y - a.y, depth + 1);
		if (found == None && xdnd_version(child) >= 0)
			found = child;
	}
	if (children != NULL)
		XFree(children);
	return found;
}

static void
xdnd_message(Atom type, Window target, Window source, long l1, long l2,
    long l3, long l4)
{
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = target;
	ev.xclient.message_type = type;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)source;
	ev.xclient.data.l[1] = l1;
	ev.xclient.data.l[2] = l2;
	ev.xclient.data.l[3] = l3;
	ev.xclient.data.l[4] = l4;
	XSendEvent(dpy, target, False, 0, &ev);
}

static void
xdnd_entree(XClientMessageEvent *m, FWin *w)
{
	Window source = (Window)m->data.l[0];
	int i;

	if (m->message_type == a_xdnd_enter) {
		xdnd_in = w != NULL && !w->is_info ? source : None;
		xdnd_in_accept = 0;
		if (xdnd_in == None)
			return;
		if ((m->data.l[1] & 1) != 0) {
			Atom type;
			int format;
			unsigned long n, after, j;
			unsigned char *data = NULL;

			if (XGetWindowProperty(dpy, source, a_xdnd_type_list, 0, 64,
			    False, XA_ATOM, &type, &format, &n, &after, &data) == Success &&
			    data != NULL && format == 32) {
				for (j = 0; j < n; j++)
					if ((Atom)((long *)data)[j] == a_uri_list)
						xdnd_in_accept = 1;
			}
			if (data != NULL)
				XFree(data);
		} else {
			for (i = 2; i < 5; i++)
				if ((Atom)m->data.l[i] == a_uri_list)
					xdnd_in_accept = 1;
		}
	} else if (source != xdnd_in || xdnd_in == None) {
		return;
	} else if (m->message_type == a_xdnd_leave) {
		xdnd_in = None;
		xdnd_in_accept = 0;
	} else if (m->message_type == a_xdnd_position) {
		xdnd_message(a_xdnd_status, source, w->win, xdnd_in_accept,
		    0, 0, xdnd_in_accept ? (long)a_xdnd_action_copy : 0);
		XFlush(dpy);
	} else if (m->message_type == a_xdnd_drop) {
		if (xdnd_in_accept) {
			XConvertSelection(dpy, a_xdnd_selection, a_uri_list, a_uri_list,
			    w->win, (Time)m->data.l[2]);
		} else {
			xdnd_message(a_xdnd_finished, source, w->win, 0, 0, 0, 0);
			xdnd_in = None;
		}
		XFlush(dpy);
	}
}

static void
xdnd_sortie_leave(void)
{
	if (xdnd_out != None && drag_win != NULL)
		xdnd_message(a_xdnd_leave, xdnd_out, drag_win->win, 0, 0, 0, 0);
	xdnd_out = None;
	xdnd_out_accept = 0;
}

static void
xdnd_suivre(int xr, int yr)
{
	Window cible = xr >= 0 ? xdnd_cherche(root, xr, yr, 0) : None;

	if (cible != None && find_win(cible) != NULL)
		cible = None;
	if (cible != xdnd_out) {
		xdnd_sortie_leave();
		xdnd_out = cible;
		xdnd_out_accept = 0;
		if (cible != None)
			xdnd_message(a_xdnd_enter, cible, drag_win->win,
			    5L << 24, a_uri_list, 0, 0);
	}
	if (cible != None)
		xdnd_message(a_xdnd_position, cible, drag_win->win, 0,
		    (long)(xr << 16) | (yr & 0xffff), 0, a_xdnd_action_copy);
	XFlush(dpy);
}

static int
xdnd_deposer(void)
{
	if (xdnd_out == None)
		return 0;
	if (xdnd_out_accept && drag_win != NULL) {
		XSetSelectionOwner(dpy, a_xdnd_selection, drag_win->win, CurrentTime);
		xdnd_message(a_xdnd_drop, xdnd_out, drag_win->win, 0, CurrentTime, 0, 0);
		XFlush(dpy);
	} else {
		xdnd_sortie_leave();
		xdnd_n = 0;
	}
	return 1;
}

/* Icônes reposées dans leur fenêtre : « Toujours aligner sur la grille » les
 * met dans la case libre la plus proche. Ctrl pendant le glisser inverse le
 * réglage, comme ⌘ sous Mac OS 9 (ici ⌘ et Option sont la même touche, qui
 * copie déjà). */
static void
deposer_aligner(FWin *w, unsigned int state)
{
	char bouge[MAX_ICONS];
	int k;

	if ((w->range == RANGE_GRILLE) == ((state & ControlMask) != 0))
		return;
	memset(bouge, 0, sizeof(bouge));
	for (k = 0; k < drag_group_n; k++)
		bouge[drag_group_idx[k]] = 1;
	aligner(w, bouge);
}

static void
finish_drag(int xr, int yr, unsigned int state)
{
	FWin *target = NULL;
	char dest[PATH_MAX];
	int target_idx = -1, local_x = 0, local_y = 0, on_folder = 0;
	/* Option/Alt-glisser copie, comme sous Mac OS 9. */
	int copy = (state & Mod1Mask) != 0;
	int same_folder, k, moved = 0;
	int drop_dx = 0, drop_dy = 0;

	set_drop_hover(NULL, -1);
	if (drag_win == NULL || drag_group_n <= 0)
		return;
	if (xdnd_deposer()) {
		restore_drag();
		return;
	}
	/* icône du bureau reposée sur le bureau : elle y change de place */
	if (drag_win->is_desktop) {
		int dx0, dy0, dx1;
		FWin *dw = drop_window_at(xr, yr, &dx0, &dy0);

		if (dw == desktop && (!icon_cible(desktop, dx0, dy0, &dx1) || desktop->icons[dx1].selected)) {
			if (desktop->range == RANGE_TRIEE) {
				/* toujours rangé : l'icône reprend sa place */
				restore_drag();
				return;
			}
			deposer_aligner(drag_win, state);
			for (k = 0; k < drag_group_n; k++) {
				Icon *ic = &drag_win->icons[drag_group_idx[k]];

				set_pos(folder_key(drag_win), ic->name, ic->x, ic->y);
			}
			save_state();
			draw_window(drag_win);
			return;
		}
	}
	if (!resolve_drop(xr, yr, &target, &target_idx, &local_x, &local_y,
	    dest, sizeof(dest), &on_folder)) {
		restore_drag();
		return;
	}
	/* dépôt sur un programme (ou son alias) : il ouvre les éléments, qui restent en place */
	if (target_idx >= 0 && !on_folder) {
		char bin[PATH_MAX];

		if (programme_de_depot(&target->icons[target_idx], bin, sizeof(bin))) {
			char *argv[MAX_ICONS + 2];
			int n = 0;
			pid_t pid;

			argv[n++] = bin;
			for (k = 0; k < drag_group_n && n < MAX_ICONS + 1; k++)
				argv[n++] = drag_win->icons[drag_group_idx[k]].path;
			argv[n] = NULL;
			restore_drag();
			annoncer_lancement(path_base(bin));
			pid = fork();
			if (pid == 0) {
				execv(bin, argv);
				_exit(127);
			}
			ptk_son(&ptk, pid > 0 ? "fdrp" : "lerr");
			return;
		}
	}
	same_folder = strcmp(dest, drag_win->path) == 0;
	if (same_folder && !copy && target == drag_win && drag_win->view != 0) {
		/* liste et boutons : ranger à la main n'existe pas, l'élément revient */
		restore_drag();
		return;
	}
	if (same_folder && !copy) {
		if (target != drag_win) {
			drop_dx = local_x - drag_grab_x + target->scroll_x - drag_icon_x +
			    decalage_repere(drag_win, target);
			drop_dy = local_y - drag_grab_y + target->scroll - drag_icon_y;
			clamp_drop_delta(target, &drop_dx, &drop_dy);
			for (k = 0; k < drag_group_n; k++) {
				Icon *ic = &drag_win->icons[drag_group_idx[k]];

				set_pos(folder_key(target), ic->name,
				    drag_group_x[k] + drop_dx,
				    drag_group_y[k] + drop_dy);
			}
			save_state();
			refresh_after_drop();
			return;
		}
		if (drag_win->range == RANGE_TRIEE) {
			restore_drag();
			return;
		}
		deposer_aligner(drag_win, state);
		for (k = 0; k < drag_group_n; k++) {
			Icon *ic = &drag_win->icons[drag_group_idx[k]];

			set_pos(folder_key(drag_win), ic->name, ic->x, ic->y);
		}
		save_state();
		draw_window(drag_win);
		return;
	}
	/* le disque et la Corbeille ne quittent pas le bureau */
	for (k = 0; k < drag_group_n; k++)
		if (drag_win->is_desktop && drag_group_idx[k] < BUREAU_FIXES) {
			restore_drag();
			drop_error("ce dépôt n'est pas permis", dest);
			return;
		}
	/* Dépôt sur la Corbeille : les noms sont rendus uniques, il n'y a
	 * donc pas de collision à prévalider. */
	if (strcmp(dest, trash_dir) == 0) {
		for (k = 0; k < drag_group_n; k++) {
			Icon *ic = &drag_win->icons[drag_group_idx[k]];

			if (protected_source(ic->path)) {
				restore_drag();
				drop_error(
				    "cet élément système ne peut pas être jeté",
				    ic->path);
				return;
			}
		}
		for (k = 0; k < drag_group_n; k++) {
			Icon *ic = &drag_win->icons[drag_group_idx[k]];
			char src[PATH_MAX], dst[PATH_MAX], info[PATH_MAX];
			char name[256];

			snprintf(src, sizeof(src), "%s", ic->path);
			snprintf(name, sizeof(name), "%s", ic->name);
			if (trash_one(src, dst, sizeof(dst), info,
			    sizeof(info)) != 0) {
				int error = errno;

				fprintf(stderr,
				    "platine-finder: impossible de jeter %s: %s\n",
				    src, strerror(error));
				ptk_son(&ptk, "lerr");
				break;
			}
			remap_metadata(src, dst, 0);
			remove_pos(drag_win->path, name);
			moved++;
		}
		if (moved == 0) {
			restore_drag();
			return;
		}
		save_state();
		ptk_son(&ptk, "fdrp");
		refresh_after_drop();
		return;
	}
	if (same_folder && copy) {
		restore_drag();
		drop_error("la copie dans le même dossier nécessite un autre nom",
		    NULL);
		return;
	}
	if (!on_folder) {
		drop_dx = local_x - drag_grab_x + target->scroll_x - drag_icon_x +
		    decalage_repere(drag_win, target);
		drop_dy = local_y - drag_grab_y + target->scroll - drag_icon_y;
		clamp_drop_delta(target, &drop_dx, &drop_dy);
	}

	/* Prévalidation complète : aucune collision ne doit produire un
	 * déplacement partiel du groupe. */
	for (k = 0; k < drag_group_n; k++) {
		Icon *ic = &drag_win->icons[drag_group_idx[k]];
		char dst[PATH_MAX], real_src[PATH_MAX], real_dest[PATH_MAX];
		struct stat st;

		if (protected_source(ic->path)) {
			restore_drag();
			drop_error("cet élément système ne peut pas être déplacé",
			    ic->path);
			return;
		}
		if (strlen(dest) + strlen(ic->name) + 2 > sizeof(dst)) {
			restore_drag();
			drop_error("chemin de destination trop long", ic->name);
			return;
		}
		join_path(dst, sizeof(dst), dest, ic->name);
		if (lstat(ic->path, &st) != 0 || lstat(dst, &(struct stat){0}) == 0 ||
		    errno != ENOENT) {
			restore_drag();
			drop_error("la source est absente ou la destination existe",
			    dst);
			return;
		}
		if (S_ISDIR(st.st_mode) && realpath(ic->path, real_src) != NULL &&
		    realpath(dest, real_dest) != NULL &&
		    path_within(real_dest, real_src)) {
			restore_drag();
			drop_error("un dossier ne peut pas être déplacé dans lui-même",
			    ic->path);
			return;
		}
	}

	for (k = 0; k < drag_group_n; k++) {
		Icon *ic = &drag_win->icons[drag_group_idx[k]];
		char src[PATH_MAX], dst[PATH_MAX], name[256];
		int rc;

		snprintf(src, sizeof(src), "%s", ic->path);
		snprintf(name, sizeof(name), "%s", ic->name);
		join_path(dst, sizeof(dst), dest, name);
		rc = copy ? fsops_copy_tree(src, dst) : fsops_move_tree(src, dst);
		if (rc != 0) {
			int error = errno;

			fprintf(stderr, "platine-finder: échec de %s vers %s: %s\n",
			    src, dst, strerror(error));
			ptk_son(&ptk, "lerr");
			break;
		}
		remap_metadata(src, dst, copy);
		if (!copy) {
			/* Sortie de la corbeille : le .trashinfo disparaît. */
			if (strcmp(drag_win->path, trash_dir) == 0) {
				const char *entry = strrchr(src, '/');
				char info[PATH_MAX];

				entry = entry != NULL ? entry + 1 : src;
				trash_info_name(entry, info, sizeof(info));
				if (info[0] != '\0')
					unlink(info);
			}
			remove_pos(drag_win->path, name);
		}
		if (!on_folder)
			set_pos(dest, name, drag_group_x[k] + drop_dx,
			    drag_group_y[k] + drop_dy);
		moved++;
	}
	if (moved == 0) {
		restore_drag();
		return;
	}
	save_state();
	ptk_son(&ptk, "fdrp");
	refresh_after_drop();
}

static void
select_icon(FWin *w, int idx)
{
	int i;

	if (idx >= 0 && idx != w->selected)
		ptk_son(&ptk, "fsel");
	for (i = 0; i < w->n; i++)
		w->icons[i].selected = 0;
	w->selected = idx;
	if (idx >= 0 && idx < w->n)
		w->icons[idx].selected = 1;
	draw_window(w);
}

static void
key_move(FWin *w, int dx, int dy)
{
	int cols = (w->w - 20) / CELL_W;
	int idx = w->selected;

	if (cols < 1)
		cols = 1;
	if (idx < 0)
		idx = 0;
	else if (w->view == 2) {
		idx += dy;
		if (idx < 0)
			idx = 0;
		if (idx >= w->n)
			idx = w->n - 1;
	} else {
		idx += dx + dy * cols;
		if (idx < 0)
			idx = 0;
		if (idx >= w->n)
			idx = w->n - 1;
	}
	select_icon(w, idx);
}

/*
 * Dossier du bureau : XDG_DESKTOP_DIR (~/.config/user-dirs.dirs) s'il est donné, sinon
 * ~/Bureau. Créé au premier lancement, il reçoit les alias des dossiers qu'affichait
 * l'ancien bureau fixe (Documents, Images, Applications) : on peut les jeter.
 */
static void
bureau_preparer(void)
{
	/* alias de départ : nom de l'alias, original (« ~ » : dossier personnel) */
	static const char *alias[][2] = { { "Documents", "~/Documents" },
	    { "Images", "~/Pictures" }, { "Images", "~/Images" },
	    { "Applications", "Applications" } };
	char cfg[PATH_MAX], ligne[PATH_MAX + 32];
	struct stat st;
	FILE *f;
	size_t i;

	path_concat(bureau_dir, sizeof(bureau_dir), home_dir, "/Desktop");
	{
		/* une ancienne installation a ~/Bureau (avant D11) : il reste le bureau */
		char ancien[PATH_MAX];
		struct stat as;

		path_concat(ancien, sizeof(ancien), home_dir, "/Bureau");
		if (stat(bureau_dir, &as) != 0 && stat(ancien, &as) == 0)
			snprintf(bureau_dir, sizeof(bureau_dir), "%s", ancien);
	}
	snprintf(cfg, sizeof(cfg), "%s/.config/user-dirs.dirs", home_dir);
	if ((f = fopen(cfg, "r")) != NULL) {
		while (fgets(ligne, sizeof(ligne), f) != NULL) {
			char *v, *fin;

			if (strncmp(ligne, "XDG_DESKTOP_DIR=\"", 17) != 0)
				continue;
			v = ligne + 17;
			if ((fin = strchr(v, '"')) == NULL)
				break;
			*fin = '\0';
			if (strncmp(v, "$HOME/", 6) == 0 && v[6] != '\0')
				path_concat(bureau_dir, sizeof(bureau_dir), home_dir, v + 5);
			else if (v[0] == '/' && strcmp(v, home_dir) != 0 &&
			    strlen(v) < sizeof(bureau_dir))
				snprintf(bureau_dir, sizeof(bureau_dir), "%s", v);
			break;
		}
		fclose(f);
	}
	if (stat(bureau_dir, &st) == 0)
		return;
	if (mkdir(bureau_dir, 0755) != 0) {
		fprintf(stderr, "platine-finder: dossier du bureau %s indisponible: %s\n",
		    bureau_dir, strerror(errno));
		return;
	}
	for (i = 0; i < sizeof(alias) / sizeof(alias[0]); i++) {
		char orig[PATH_MAX], lien[PATH_MAX];

		if (alias[i][1][0] == '~')
			join_path(orig, sizeof(orig), home_dir, alias[i][1] + 2);
		else
			join_path(orig, sizeof(orig), vol_dir, alias[i][1]);
		join_path(lien, sizeof(lien), bureau_dir, alias[i][0]);
		if (stat(orig, &st) == 0 && S_ISDIR(st.st_mode))
			(void)symlink(orig, lien);
	}
}

/* le dossier du bureau a changé (programme tiers, Instantané, téléchargement…) :
 * le bureau est relu, la sélection gardée par nom */
static void
bureau_verifier(void)
{
	struct stat st;
	char *sel;
	int i, j, n;

	/* pas pendant un glisser ou un renommage : ils tiennent des indices d'icônes */
	if (desktop == NULL || drag_win != NULL || rename_win == desktop ||
	    stat(bureau_dir, &st) != 0)
		return;
	/* la date est à la seconde : un dossier changé il y a moins de 2 s est relu
	 * même à date égale (deux changements dans la même seconde) */
	if (st.st_mtime == bureau_mtime && time(NULL) - st.st_mtime > 2)
		return;
	n = desktop->n;
	sel = calloc((size_t)n, 256);
	if (sel == NULL)
		return;
	for (i = 0; i < n; i++)
		if (desktop->icons[i].selected)
			snprintf(sel + (size_t)i * 256, 256, "%s", desktop->icons[i].name);
	load_dir(desktop);
	for (i = 0; i < desktop->n; i++)
		for (j = 0; j < n; j++)
			if (sel[(size_t)j * 256] != '\0' &&
			    strcmp(desktop->icons[i].name, sel + (size_t)j * 256) == 0) {
				desktop->icons[i].selected = 1;
				desktop->selected = i;
			}
	free(sel);
	draw_window(desktop);
}

static void
desktop_init(void)
{
	XSetWindowAttributes a;
	FWin *w = calloc(1, sizeof(*w));

	if (w == NULL)
		return;
	w->is_desktop = 1;
	w->w = sw;
	w->h = sh - ptk.menubar_h;
	geom_range("__bureau__", &w->range, &w->range_par);
	snprintf(w->path, sizeof(w->path), "%s", bureau_dir);
	load_dir(w);	/* disque, Corbeille, puis le contenu du dossier du bureau */

	a.override_redirect = True;
	a.background_pixmap = None;
	a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	    PointerMotionMask | KeyPressMask;
	w->win = XCreateWindow(dpy, root, 0, ptk.menubar_h, (unsigned)w->w,
	    (unsigned)w->h, 0, CopyFromParent, InputOutput, CopyFromParent,
	    CWOverrideRedirect | CWBackPixmap | CWEventMask, &a);
	XStoreName(dpy, w->win, "Bureau");
	set_xdnd_aware(w->win);
	motif_du_reglage();
	if (fond_tile != None) {
		Pixmap bg = ptk_tile_pixmap(&ptk, fond_tile, fond_w, fond_h,
		    w->w, w->h);
		XSetWindowBackgroundPixmap(dpy, w->win, bg);
		fond_bureau = bg;	/* gardé : le bureau se compose dessus en mémoire */
	} else {
		XSetWindowBackgroundPixmap(dpy, w->win, ptk.root_pattern);
	}
	XMapWindow(dpy, w->win);
	XLowerWindow(dpy, w->win);
	desktop = w;
	w->next = wins;
	wins = w;
}

int
main(int argc, char **argv)
{
	XEvent ev;
	Time last_click = 0;
	int last_icon = -1;
	FWin *last_win = NULL;

	setlocale(LC_COLLATE, "");
	setlocale(LC_TIME, "");
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "platine-finder: pas de display\n");
		return 1;
	}
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	if (ptk_init(&ptk, dpy, screen) != 0)
		return 1;
	cursor = XCreateFontCursor(dpy, XC_left_ptr);
	copy_cursor = XCreateFontCursor(dpy, XC_plus);
	ptk_set_root_pattern(&ptk);

	a_utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	a_menu = XInternAtom(dpy, "_PLATINE_MENU", False);
	a_app = XInternAtom(dpy, "_PLATINE_APP_NAME", False);
	a_icon = XInternAtom(dpy, "_PLATINE_ICON", False);
	a_cmd = XInternAtom(dpy, "_PLATINE_COMMAND", False);
	a_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	a_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	a_context = XInternAtom(dpy, "_PLATINE_CONTEXT", False);
	a_geometry = XInternAtom(dpy, "_PLATINE_GEOMETRY", False);
	a_clip = XInternAtom(dpy, "CLIPBOARD", False);
	a_uri_list = XInternAtom(dpy, "text/uri-list", False);
	a_targets = XInternAtom(dpy, "TARGETS", False);
	a_xdnd_aware = XInternAtom(dpy, "XdndAware", False);
	a_xdnd_enter = XInternAtom(dpy, "XdndEnter", False);
	a_xdnd_position = XInternAtom(dpy, "XdndPosition", False);
	a_xdnd_status = XInternAtom(dpy, "XdndStatus", False);
	a_xdnd_leave = XInternAtom(dpy, "XdndLeave", False);
	a_xdnd_drop = XInternAtom(dpy, "XdndDrop", False);
	a_xdnd_selection = XInternAtom(dpy, "XdndSelection", False);
	a_xdnd_finished = XInternAtom(dpy, "XdndFinished", False);
	a_xdnd_action_copy = XInternAtom(dpy, "XdndActionCopy", False);
	a_xdnd_type_list = XInternAtom(dpy, "XdndTypeList", False);
	XSelectInput(dpy, DefaultRootWindow(dpy), PropertyChangeMask);

	if (getenv("HOME") != NULL)
		snprintf(home_dir, sizeof(home_dir), "%s", getenv("HOME"));
	else
		snprintf(home_dir, sizeof(home_dir), "/home/jeremie");
	snprintf(vol_dir, sizeof(vol_dir), "%s", ptk_volume());
	while (strlen(vol_dir) > 1 && vol_dir[strlen(vol_dir) - 1] == '/')
		vol_dir[strlen(vol_dir) - 1] = '\0';
	path_concat(trash_dir, sizeof(trash_dir), home_dir,
	    "/.local/share/Trash/files");
	path_concat(trash_info_dir, sizeof(trash_info_dir), home_dir,
	    "/.local/share/Trash/info");
	{
		char cfg[PATH_MAX];
		snprintf(cfg, sizeof(cfg), "%s/.config", home_dir);
		mkdir(cfg, 0700);
		snprintf(cfg, sizeof(cfg), "%s/.config/platine", home_dir);
		mkdir(cfg, 0700);
	}
	path_concat(state_dir, sizeof(state_dir), home_dir,
	    "/.config/platine/finder2");
	mkdir(state_dir, 0700);
	if (make_dirs(trash_dir) != 0 || make_dirs(trash_info_dir) != 0) {
		fprintf(stderr, "platine-finder: corbeille indisponible\n");
	}
	bureau_preparer();

	{
		/* motif 2×2 pour assombrir les icônes sélectionnées */
		static const char dim_bits[8] = {
			(char)0xaa, (char)0x55, (char)0xaa, (char)0x55,
			(char)0xaa, (char)0x55, (char)0xaa, (char)0x55
		};
		dim_stipple = XCreateBitmapFromData(dpy, root, dim_bits, 8,
		    8);
		dim_gc = XCreateGC(dpy, root, 0, NULL);
		mq_stipple = XCreateBitmapFromData(dpy, root, dim_bits, 8,
		    8);
		mq_gc = XCreateGC(dpy, root, 0, NULL);
	}
	arg_dossier16 = ptk_load_argb(&ptk, "dossier-16");
	arg_dossier = ptk_load_argb(&ptk, "dossier-32");
	{
		int di;
		char n[64];

		for (di = 0; di < DOC_NB; di++) {
			snprintf(n, sizeof(n), "%s-32", doc_noms[di]);
			doc32[di] = ptk_load_argb(&ptk, n);
			snprintf(n, sizeof(n), "%s-16", doc_noms[di]);
			doc16[di] = ptk_load_argb(&ptk, n);
		}
	}
	arg_document16 = doc16[DOC_GENERIC];
	arg_document = doc32[DOC_GENERIC];
	arg_disque = ptk_load_argb(&ptk, "disque-32");
	arg_corbeille = ptk_load_argb(&ptk, "corbeille-32");
	arg_corbeille_pleine = ptk_load_argb(&ptk, "corbeille-pleine-32");
	{
		int ti;
		char n[64];

		for (ti = 0; ti < TAB_NB; ti++) {
			snprintf(n, sizeof(n), "%s-32", tab_fichiers[ti]);
			tab32[ti] = ptk_load_argb(&ptk, n);
			snprintf(n, sizeof(n), "%s-16", tab_fichiers[ti]);
			tab16[ti] = ptk_load_argb(&ptk, n);
		}
	}
	ptk_load_xpm(&ptk, "dossier-32.xpm", &pix_folder, &pix_folder_mask);
	pix_folder_dim = dim_pixmap(pix_folder, 32, 32);
	ptk_load_xpm(&ptk, "document-texte-32.xpm", &pix_doc, &pix_doc_mask);
	pix_doc_dim = dim_pixmap(pix_doc, 32, 32);
	ptk_load_xpm(&ptk, "disque-32.xpm", &pix_disk, &pix_disk_mask);
	pix_disk_dim = dim_pixmap(pix_disk, 32, 32);
	ptk_load_xpm(&ptk, "corbeille-32.xpm", &pix_trash, &pix_trash_mask);
	pix_trash_dim = dim_pixmap(pix_trash, 32, 32);
	ptk_load_xpm(&ptk, "corbeille-pleine-32.xpm", &pix_trash_full,
	    &pix_trash_full_mask);
	pix_trash_full_dim = dim_pixmap(pix_trash_full, 32, 32);
	fond_tile = ptk_load_ppm(&ptk, "fond.ppm", &fond_w, &fond_h);
	ptk_load_xpm(&ptk, "dossier-16.xpm", &pix_folder16,
	    &pix_folder16_mask);
	ptk_load_xpm(&ptk, "document-texte-16.xpm", &pix_doc16,
	    &pix_doc16_mask);

	load_state();
	ptk_ramasser_enfants();	/* pas SIG_IGN : hérité par les programmes lancés */
	desktop_init();
	if (argc > 1) {
		int i;

		for (i = 1; i < argc; i++)
			open_path(argv[i]);
	} else if (ptk_reglage_int("finder_fenetre_demarrage", 0)) {
		/* par défaut, le bureau seul au démarrage (demande de l'utilisateur) */
		open_path(home_dir);
	}

	for (;;) {
		FWin *w;
		long long wait = -1;

		/* Ne pas appeler select() si Xlib a déjà des événements en
		 * file : le descripteur peut alors paraître lisible sans
		 * qu'aucun événement ne soit disponible. */
		if (XPending(dpy) == 0) {
			int due = 0;
			long long deadline = -1;

			if (sb_repeat_win != NULL) {
				wait = sb_repeat_next - now_ms();
				due = wait <= 0;
				deadline = sb_repeat_next;
			}
			if (spring_armed) {
				long long t = spring_at - now_ms();

				if (t <= 0)
					due = 1;
				if (deadline < 0 || spring_at < deadline)
					deadline = spring_at;
			}
			/* le dossier du bureau est relu toutes les 2 s (un stat) : ce qu'un
			 * autre programme y pose paraît sur le bureau */
			if (bureau_verif_at - now_ms() <= 0) {
				bureau_verifier();
				bureau_verif_at = now_ms() + 2000;
			}
			if (deadline < 0 || bureau_verif_at < deadline)
				deadline = bureau_verif_at;
			if (due) {
				int again = 0;

				if (sb_repeat_win != NULL) {
					Window rr, child;
					int rx, ry, wx, wy;
					unsigned int mask;

					int ax = 0;

					/* tant que le pointeur reste sur la zone appuyée */
					if (!XQueryPointer(dpy, sb_repeat_win->win,
					    &rr, &child, &rx, &ry, &wx, &wy, &mask) ||
					    sb_zone(sb_repeat_win, wx, wy, &ax) != sb_repeat_zone ||
					    ax != sb_drag_axe) {
						if (sb_repeat_zone == SB_RECUL || sb_repeat_zone == SB_AVANCE) {
							/* hors de la flèche : elle se relève, reprend en revenant */
							FWin *rw = sb_repeat_win;

							scroll_repeat_stop();
							draw_window(rw);
						} else
							scroll_repeat_stop();
					} else {
						sb_agir(sb_repeat_win, sb_drag_axe, sb_repeat_zone);
						sb_repeat_next = now_ms() + 60;
						again = 1;
					}
				}
				if (spring_armed && spring_at - now_ms() <= 0) {
					spring_fire();
					again = 1;
				}
				if (again)
					continue;
			} else if (deadline >= 0) {
				/* select() peut rendre la main immédiatement : le descripteur
				 * reste lisible tant que Xlib n'a pas consommé ses réponses
				 * internes. On boucle donc jusqu'à l'échéance réelle. */
				for (;;) {
					struct timeval tv;
					fd_set fds;
					long long rest = deadline - now_ms();
					int fd = ConnectionNumber(dpy);

					if (rest <= 0 || XPending(dpy))
						break;
					FD_ZERO(&fds);
					FD_SET(fd, &fds);
					tv.tv_sec = (time_t)(rest / 1000);
					tv.tv_usec = (long)((rest % 1000) * 1000);
					if (select(fd + 1, &fds, NULL, NULL, &tv) < 0 &&
					    errno != EINTR)
						break;
				}
				continue;
			}
		}
		XNextEvent(dpy, &ev);
		w = find_win(ev.xany.window);
		switch (ev.type) {
		case FocusOut:
			/* une autre application passe devant : ses menus déroulés (grabs) ne comptent pas */
			if (ev.xfocus.mode == NotifyNormal && !focus_ailleurs) {
				/* le WM qui redonne le focus à la fenêtre qui l'a déjà (clic sur la barre de
				 * titre) produit un FocusOut sans FocusIn : on demande qui l'a vraiment */
				Window fw;
				int rv;
				XGetInputFocus(dpy, &fw, &rv);
				if (fw != None && fw != PointerRoot && find_win(fw) != NULL)
					break;
				focus_ailleurs = 1;
				redessiner_dossiers();
			}
			break;
		case FocusIn: {
			/* le WM change le premier plan : cadre, bandeau et barres suivent */
			FWin *f, *old = active;

			if (ev.xfocus.mode == NotifyNormal && focus_ailleurs) {
				focus_ailleurs = 0;
				redessiner_dossiers();
			}

			for (f = wins; f != NULL; f = f->next)
				if (f->win == ev.xfocus.window)
					break;
			if (f != NULL && f != old && !f->is_desktop) {
				active = f;
				for (FWin *o = wins; o != NULL; o = o->next)
					if (!o->is_desktop && !o->is_info)
						update_menu(o);
				if (old != NULL && !old->is_desktop)
					draw_window(old);
				draw_window(f);
			}
			if (f != NULL && !f->is_desktop && !f->is_info)
				update_menu(f);
			break;
		}
		case Expose:
			if (w != NULL && w->tampon != None && w->tampon_w == w->w &&
			    w->tampon_h == w->h)
				XCopyArea(dpy, w->tampon, w->win, ptk.gc, ev.xexpose.x, ev.xexpose.y,
				    (unsigned)ev.xexpose.width, (unsigned)ev.xexpose.height,
				    ev.xexpose.x, ev.xexpose.y), mq_retracer(w);
			else if (w != NULL && ev.xexpose.count == 0)
				draw_window(w);
			break;
		case ConfigureNotify:
			if (w != NULL) {
				int change = w->w != ev.xconfigure.width ||
				    w->h != ev.xconfigure.height;

				w->w = ev.xconfigure.width;
				w->h = ev.xconfigure.height;
				set_geom(w);
				if (change)	/* un simple déplacement ne redessine rien */
					draw_window(w);
			}
			break;
		case PropertyNotify:
			if (ev.xproperty.window == DefaultRootWindow(dpy)) {
				int b = bureau_au_premier_plan();

				/* Apparence → Bureau : nouveau motif, tout de suite */
				if (ev.xproperty.atom == XInternAtom(dpy, "_PLATINE_MOTIF", False) &&
				    ev.xproperty.state == PropertyNewValue && desktop != NULL) {
					Atom ty;
					int fo;
					unsigned long nb, re;
					unsigned char *v = NULL;

					if (XGetWindowProperty(dpy, root, ev.xproperty.atom, 0, 64, False, AnyPropertyType, &ty, &fo,
					    &nb, &re, &v) == Success && v != NULL) {
						charger_motif((char *)v);
						XFree(v);
						fond_bureau = ptk_tile_pixmap(&ptk, fond_tile, fond_w, fond_h, desktop->w, desktop->h);
						XSetWindowBackgroundPixmap(dpy, desktop->win, fond_bureau);
						draw_window(desktop);
					}
					break;
				}
				/* le menu Pomme demande l'ouverture d'un dossier */
				if (ev.xproperty.atom == XInternAtom(dpy, "_PLATINE_OUVRIR", False) &&
				    ev.xproperty.state == PropertyNewValue) {
					Atom ty;
					int fo;
					unsigned long nb, re;
					unsigned char *v = NULL;

					if (XGetWindowProperty(dpy, root, ev.xproperty.atom, 0, 1024, True, AnyPropertyType, &ty, &fo,
					    &nb, &re, &v) == Success && v != NULL) {
						struct stat st;

						/* un dossier s'ouvre ; un document (menu Pomme) se lance */
						if (stat((char *)v, &st) == 0 && !S_ISDIR(st.st_mode))
							launch_file((char *)v);
						else
							open_path((char *)v);
						XFree(v);
					}
					break;
				}

				if (b != bureau_devant) {
					bureau_devant = b;
					/* retour du bureau à une fenêtre (souvent par sa barre de titre) : le clic sur
					 * le bureau en avait fait la fenêtre active, et aucun FocusIn ne suit */
					if (!b) {
						Window fw;
						int rv;
						FWin *f;

						XGetInputFocus(dpy, &fw, &rv);
						f = find_win(fw);
						if (f != NULL && !f->is_desktop && f != active) {
							active = f;
							for (FWin *o = wins; o != NULL; o = o->next)
								if (!o->is_desktop && !o->is_info)
									update_menu(o);
						}
					}
					redessiner_dossiers();
				}
				break;
			}
			if (w != NULL && ev.xproperty.atom == a_geometry) {
				Atom type;
				int format;
				unsigned long n, after;
				unsigned char *data = NULL;
				if (XGetWindowProperty(dpy, w->win, a_geometry,
				    0, 4, False, XA_CARDINAL, &type, &format,
				    &n, &after, &data) == Success &&
				    data != NULL) {
					long *l = (long *)data;
					if (n >= 4) {
						w->x = (int)l[0];
						w->y = (int)l[1];
						w->w = (int)l[2];
						w->h = (int)l[3];
						set_geom(w);
					}
					XFree(data);
				}
			}
			break;
		case ButtonPress:
			if (w == NULL)
				break;
			if (w->is_desktop) {
				XEvent b;

				memset(&b, 0, sizeof(b));
				b.xclient.type = ClientMessage;
				b.xclient.window = w->win;
				b.xclient.message_type = XInternAtom(dpy, "_PLATINE_BUREAU", False);
				b.xclient.format = 32;
				XSendEvent(dpy, DefaultRootWindow(dpy), False,
				    SubstructureRedirectMask | SubstructureNotifyMask, &b);
			}
			if (w->is_info) {
				if (ev.xbutton.button == Button1 &&
				    ev.xbutton.y >= 10 &&
				    ev.xbutton.y < 28 &&
				    ev.xbutton.x >= 70)
					w->info_focus = 1;
				else if (ev.xbutton.button == Button1 &&
				    ev.xbutton.y >= 184 &&
				    ev.xbutton.y < 274)
					w->info_focus = 2;
				else
					w->info_focus = 0;
				draw_window(w);
				break;
			}
			if (rename_win != NULL)
				commit_rename();
			if (!w->is_desktop && ev.xbutton.y < TOOLBAR_H &&
			    ev.xbutton.button == Button1) {
				int bx, gl;

				if (bouton_barre(w, ev.xbutton.x, ev.xbutton.y, &bx, &gl) >= 0) {
					/* s'enfonce à l'appui, n'agit qu'au relâchement dedans */
					bt_win = w;
					bt_x = bx;
					bt_glyphe = gl;
					bt_dedans = 1;
					ptk_son(&ptk, "bevp");
					tool_button(w, bx, 4, gl, 1, 1);
					XFlush(dpy);
					break;
				}
				toolbar_click(w, ev.xbutton.x, ev.xbutton.y,
				    ev.xbutton.x_root, ev.xbutton.y_root);
				break;
			}
			if (!w->is_desktop)
				w->addr_focus = 0;
			if (ev.xbutton.button == Button3 ||
			    (ev.xbutton.button == Button1 &&
			    (ev.xbutton.state & ControlMask))) {
				int idx;
				if (icon_at(w, ev.xbutton.x, ev.xbutton.y,
				    &idx))
					select_icon(w, idx);
				else {
					idx = -1;
					select_icon(w, -1);
				}
				context_menu(w, idx, ev.xbutton.x_root,
				    ev.xbutton.y_root);
				break;
			}
			/* barres de défilement : tout clic gauche y reste, même grisées */
			if (!w->is_desktop && !w->is_info && ev.xbutton.button == Button1 &&
			    ev.xbutton.y >= CONTENT_TOP &&
			    (ev.xbutton.x >= w->w - BARRE || ev.xbutton.y >= w->h - BARRE)) {
				int ax = 0, zone = sb_zone(w, ev.xbutton.x, ev.xbutton.y, &ax);

				if (zone == SB_POUCE) {
					sb_drag = w;
					sb_drag_axe = ax;
					sb_drag_y = ax ? ev.xbutton.x : ev.xbutton.y;
					sb_drag_scroll = ax ? w->scroll_x : w->scroll;
					ptk_son(&ptk, "sltp");
				} else if (zone != SB_RIEN) {
					ptk_son(&ptk, zone == SB_RECUL || zone == SB_AVANCE ? "sbap" : "sbtp");
					sb_drag_axe = ax;
					sb_repeat_win = w;
					sb_repeat_zone = zone;
					sb_repeat_dir = 1;
					sb_repeat_next = now_ms() + 330;
					sb_repeat_grabbed =
					    XGrabPointer(dpy, w->win, False,
					    ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
					    GrabModeAsync, None, None,
					    CurrentTime) == GrabSuccess;
					if (!sb_repeat_grabbed)
						sb_repeat_win = NULL;
					sb_agir(w, ax, zone);
					draw_window(w);
				}
				break;
			}
			if (ev.xbutton.button == Button4) {
				scroll_by(w, -32);
				break;
			}
			if (ev.xbutton.button == Button5) {
				scroll_by(w, 32);
				break;
			}
			if (ev.xbutton.button != Button1)
				break;
			if (w->view == 2 && !w->is_desktop && ev.xbutton.x < w->w - BARRE) {
				/* en coordonnées de colonnes : la liste a pu défiler ; le
				 * bouton de tri, lui, est fixe au bout visible */
				int px = ev.xbutton.x + w->scroll_x, py = ev.xbutton.y;
				int bord = w->w - BARRE;

				if (py >= CONTENT_TOP - 1 && py < LISTE_Y0) {
					/* en-tête : clic = trier par cette colonne dans son sens
					 * naturel (reclic = inverser), bouton du bout = inverser.
					 * Changement de colonne : le sens repart au naturel. */
					if (ev.xbutton.x >= bord - 15) {
						w->tri_inv = !w->tri_inv;
					} else {
						int c = px < 214 ? 0 : px < 379 ? 1 : px < 445 ? 2 : 3;

						if (w->tri == c)
							w->tri_inv = !w->tri_inv;
						else {
							w->tri = c;
							w->tri_inv = 0;
						}
					}
					liste_trier(w);
					liste_deselectionner(w);
					set_geom(w);
					draw_window(w);
					break;
				}
				if (py >= LISTE_Y0) {
					int r = (py - LISTE_Y0 + w->scroll) / LISTE_RANG;

					if (r >= 0 && r < w->n && w->icons[r].is_dir) {
						int tx = 8 + 16 * w->icons[r].niveau;

						if (px >= tx - 1 && px < tx + 10) {
							liste_basculer(w, r);
							draw_window(w);
							break;
						}
					}
				}
			}
			{
				int idx;
				active = w;
				if (icon_at(w, ev.xbutton.x, ev.xbutton.y,
				    &idx)) {
					int i, nsel = 0;

					for (i = 0; i < w->n; i++)
						if (w->icons[i].selected)
							nsel++;
					if (!w->icons[idx].selected ||
					    nsel <= 1) {
						select_icon(w, idx);
					} else {
						w->selected = idx;
						draw_window(w);
					}
					if (w->view == 1) {
						open_selected(w);
						last_click = 0;
					} else {
						drag_win = w;
						drag_idx = idx;
						drag_brut_dx = drag_brut_dy = 0;
						drag_start_x = ev.xbutton.x_root;
						drag_start_y = ev.xbutton.y_root;
						drag_icon_x = w->icons[idx].x;
						drag_icon_y = w->icons[idx].y;
						drag_moved = 0;
						drag_grab_x = ev.xbutton.x -
						    (w->icons[idx].x - (w->is_desktop ? 0 : w->scroll_x));
						drag_grab_y = ev.xbutton.y -
						    (w->is_desktop ? w->icons[idx].y :
						    w->icons[idx].y - w->scroll);
						drag_group_n = 0;
						spring_armed = 0;
						spring_path[0] = '\0';
						for (i = 0; i < w->n &&
						    drag_group_n < MAX_ICONS; i++) {
							if (!w->icons[i]
							    .selected)
								continue;
							drag_group_idx[
							    drag_group_n] = i;
							drag_group_x[
							    drag_group_n] =
							    w->icons[i].x;
							drag_group_y[
							    drag_group_n] =
							    w->icons[i].y;
							drag_group_n++;
						}
						xdnd_n = 0;
						xdnd_out = None;
						xdnd_out_accept = 0;
						for (i = 0; i < drag_group_n && xdnd_n < PP_MAX; i++)
							snprintf(xdnd_chemins[xdnd_n++], PATH_MAX, "%s",
							    w->icons[drag_group_idx[i]].path);
						drag_copy_shown = 0;
						drag_pointer_grabbed =
						    XGrabPointer(dpy, w->win, False,
						    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
						    GrabModeAsync, GrabModeAsync, None,
						    None, CurrentTime) == GrabSuccess;
					}
				} else {
					select_icon(w, -1);
					if (w->view != 2) {
						mq_active = 1;
						mq_win = w;
						mq_x0 = mq_x1 = ev.xbutton.x;
						mq_y0 = mq_y1 = ev.xbutton.y;
						XGrabPointer(dpy, w->win,
						    False, ButtonPressMask | ButtonReleaseMask |
						    PointerMotionMask,
						    GrabModeAsync,
						    GrabModeAsync, None,
						    None, CurrentTime);
					}
				}
			}
			break;
		case MotionNotify:
			if (mq_active) {
				while (XCheckTypedWindowEvent(dpy, mq_win->win, MotionNotify, &ev))
					;
				mq_x1 = ev.xmotion.x;
				mq_y1 = ev.xmotion.y;
				if (mq_x1 < 0)
					mq_x1 = 0;
				if (mq_y1 < 0)
					mq_y1 = 0;
				if (mq_x1 >= mq_win->w)
					mq_x1 = mq_win->w - 1;
				if (mq_y1 >= mq_win->h)
					mq_y1 = mq_win->h - 1;
				/* les icônes se grisent pendant le tracé ; draw_window repose le lasso */
				if (mq_selection(mq_win))
					draw_window(mq_win);
				else
					mq_draw(mq_win);
				break;
			}
			if (drag_win != NULL) {
				int dx = ev.xmotion.x_root - drag_start_x;
				int dy = ev.xmotion.y_root - drag_start_y;
				if (abs(dx) > 3 || abs(dy) > 3)
					drag_moved = 1;
				if (drag_moved) {
					/* Curseur « copie » pendant Option. */
					if (drag_pointer_grabbed) {
						int want = (ev.xmotion.state &
						    Mod1Mask) != 0;

						if (want != drag_copy_shown) {
							drag_copy_shown = want;
							XChangeActivePointerGrab(
							    dpy,
							    ButtonPressMask | ButtonReleaseMask |
							    PointerMotionMask,
							    want ? copy_cursor :
							    cursor, CurrentTime);
						}
					}
					/* bureau : x est le bord de l'image ; la colonne de droite est à
					 * DESKTOP_ICON_X, son étiquette dépasse de la case comme sous Mac OS 9 */
					int maxx = drag_win->is_desktop ? DESKTOP_ICON_X(drag_win) :
					    drag_win->w - cell_w(drag_win);
					int maxy = drag_win->h - cell_h(drag_win);
					int miny = drag_win->is_desktop ? 2 :
					    CONTENT_TOP;
					int min_gx = INT_MAX, min_gy = INT_MAX;
					int max_gx = INT_MIN, max_gy = INT_MIN;
					int k;

					/* Un seul delta pour tout le groupe : les icônes
					 * ne doivent pas s'empiler contre un bord. */
					for (k = 0; k < drag_group_n; k++) {
						if (drag_group_x[k] < min_gx)
							min_gx = drag_group_x[k];
						if (drag_group_y[k] < min_gy)
							min_gy = drag_group_y[k];
						if (drag_group_x[k] > max_gx)
							max_gx = drag_group_x[k];
						if (drag_group_y[k] > max_gy)
							max_gy = drag_group_y[k];
					}
					if (drag_group_n > 0) {
						if (dx < -min_gx)
							dx = -min_gx;
						if (dx > maxx - max_gx)
							dx = maxx - max_gx;
						if (dy < miny - min_gy)
							dy = miny - min_gy;
						if (dy > maxy - max_gy)
							dy = maxy - max_gy;
					}
					/* comme Mac OS 9 : l'original reste en place, sélectionné ; un fantôme suit */
					drag_dx_final = dx;
					drag_dy_final = dy;
					fantome_suivre(ev.xmotion.x_root - drag_start_x,
					    ev.xmotion.y_root - drag_start_y);
					if (drag_moved) {
						FWin *hover = NULL;
						char hdest[PATH_MAX];
						int hidx = -1, hx, hy, hfolder = 0;
						int ok;

						drag_last_x = ev.xmotion.x_root;
						drag_last_y = ev.xmotion.y_root;
						ok = resolve_drop(ev.xmotion.x_root,
						    ev.xmotion.y_root, &hover, &hidx,
						    &hx, &hy, hdest, sizeof(hdest),
						    &hfolder);
						if (ok && hfolder) {
							set_drop_hover(hover, hidx);
							if (!spring_armed ||
							    strcmp(spring_path,
							    hover->icons[hidx].path) != 0) {
								snprintf(spring_path,
								    sizeof(spring_path),
								    "%s",
								    hover->icons[hidx].path);
								spring_at = now_ms() + 700;
								spring_armed = 1;
							}
						} else if (ok && hidx >= 0 &&
						    programme_de_depot(&hover->icons[hidx], NULL, 0)) {
							set_drop_hover(hover, hidx);
							spring_armed = 0;
							spring_path[0] = '\0';
						} else {
							set_drop_hover(NULL, -1);
							spring_armed = 0;
							spring_path[0] = '\0';
						}
						xdnd_suivre(ok ? -1 : ev.xmotion.x_root,
						    ok ? -1 : ev.xmotion.y_root);
					}
				}
				break;
			}
			if (bt_win != NULL) {
				int mx = ev.xmotion.x, my = ev.xmotion.y;
				int d = mx >= bt_x && mx < bt_x + 22 && my >= 4 && my < 22;

				if (d != bt_dedans) {
					bt_dedans = d;
					tool_button(bt_win, bt_x, 4, bt_glyphe, 1, d);
					XFlush(dpy);
				}
				break;
			}
			if (sb_drag != NULL) {
				int axe = sb_drag_axe, p, l;
				int rail = (axe ? sb_drag->w - BARRE : sb_drag->h - CONTENT_TOP - BARRE) - 30;
				int ms = axe ? max_scroll_x(sb_drag) : max_scroll(sb_drag);
				int delta = (axe ? ev.xmotion.x : ev.xmotion.y) - sb_drag_y;

				while (XCheckTypedWindowEvent(dpy, sb_drag->win, MotionNotify, &ev))
					delta = (axe ? ev.xmotion.x : ev.xmotion.y) - sb_drag_y;
				sb_pouce(sb_drag, axe, &p, &l);
				if (rail - l > 0) {
					int v = sb_drag_scroll + delta * ms / (rail - l);

					v = v < 0 ? 0 : v > ms ? ms : v;
					if (axe)
						sb_drag->scroll_x = v;
					else
						sb_drag->scroll = v;
					draw_window(sb_drag);
				}
			}
			break;
		case ButtonRelease:
			if (bt_win != NULL) {
				FWin *b = bt_win;
				int d = bt_dedans, bx = bt_x;

				bt_win = NULL;
				ptk_son(&ptk, "bevr");
				if (d)
					toolbar_click(b, bx + 2, 8, ev.xbutton.x_root, ev.xbutton.y_root);
				draw_window(b);
				break;
			}
			{
				/* seule une flèche enfoncée a besoin d'être relevée */
				int repetait = sb_repeat_win != NULL;

				scroll_repeat_stop();
				if (repetait && w != NULL)
					draw_window(w);
			}
			if (mq_active) {
				mq_effacer(mq_win);
				mq_active = 0;
				mq_x1 = ev.xbutton.x;
				mq_y1 = ev.xbutton.y;
				XUngrabPointer(dpy, CurrentTime);
				mq_selection(mq_win);
				draw_window(mq_win);
				break;
			}
			if (drag_win != NULL) {
				FWin *dw = drag_win;
				int di = drag_idx;

				ptk_fantome_fin(&ptk, &fantome);
				fantome_pret = 0;
				if (drag_moved) {
					int kk;

					for (kk = 0; kk < drag_group_n; kk++) {
						Icon *gi = &drag_win->icons[drag_group_idx[kk]];

						gi->x = drag_group_x[kk] + drag_dx_final;
						gi->y = drag_group_y[kk] + drag_dy_final;
					}
					finish_drag(ev.xbutton.x_root, ev.xbutton.y_root,
					    ev.xbutton.state);
					last_click = 0;
				} else if (last_win == dw && last_icon == di &&
				    ev.xbutton.time - last_click < double_clic_ms()) {
					open_selected(dw);
					last_click = 0;
				} else {
					last_click = ev.xbutton.time;
					last_icon = di;
					last_win = dw;
				}
				if (drag_pointer_grabbed)
					XUngrabPointer(dpy, CurrentTime);
				drag_pointer_grabbed = 0;
				drag_win = NULL;
				drag_idx = -1;
				drag_group_n = 0;
				spring_armed = 0;
				spring_path[0] = '\0';
				break;
			}
			sb_drag = NULL;
			break;
		case KeyPress: {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);

			if (ks == XK_Escape && drag_win != NULL) {
				ptk_fantome_fin(&ptk, &fantome);
				fantome_pret = 0;
				xdnd_sortie_leave();
				xdnd_n = 0;
				restore_drag();
				if (drag_pointer_grabbed)
					XUngrabPointer(dpy, CurrentTime);
				drag_pointer_grabbed = 0;
				drag_win = NULL;
				drag_idx = -1;
				drag_group_n = 0;
				spring_armed = 0;
				spring_path[0] = '\0';
				break;
			}
			if (w == NULL || w->is_desktop)
				break;
			if (w->is_info) {
				char buf[32];
				int n;
				ks = XLookupKeysym(&ev.xkey, 0);
				n = XLookupString(&ev.xkey, buf,
				    sizeof(buf), &ks, NULL);
				if (ks == XK_Escape) {
					w->info_focus = 0;
					draw_window(w);
					break;
				}
				if (w->info_focus == 1) {
					if (ks == XK_Return ||
					    ks == XK_KP_Enter) {
						char np[PATH_MAX];
						char dir[PATH_MAX];
						char *slash;
						snprintf(np, sizeof(np), "%s",
						    w->info_path);
						slash = strrchr(np, '/');
						if (slash != NULL) {
							*slash = '\0';
							snprintf(dir, sizeof(dir),
							    "%s", np);
							join_path(np,
							    sizeof(np), dir,
							    w->info_name);
							if (fsops_move_tree(
							    w->info_path, np) == 0) {
								remap_metadata(
								    w->info_path, np,
								    0);
								snprintf(w->info_path,
								    sizeof(w->info_path),
								    "%s", np);
								refresh_after_drop();
							}
						}
						w->info_focus = 0;
						draw_window(w);
						break;
					}
					if (ks == XK_BackSpace) {
						size_t l = strlen(w->info_name);
						if (l > 0)
							w->info_name[l - 1] = '\0';
						draw_window(w);
						break;
					}
					if (n > 0 && (unsigned char)buf[0] >= 32 &&
					    strlen(w->info_name) <
					    sizeof(w->info_name) - 1) {
						size_t l = strlen(w->info_name);
						w->info_name[l] = buf[0];
						w->info_name[l + 1] = '\0';
						draw_window(w);
					}
					break;
				}
				if (w->info_focus == 2) {
					size_t l = strlen(w->info_comment);
					if (ks == XK_BackSpace) {
						if (l > 0)
							w->info_comment[l - 1] = '\0';
					} else if (ks == XK_Return ||
					    ks == XK_KP_Enter) {
						if (l < sizeof(w->info_comment) - 1) {
							w->info_comment[l] = '\n';
							w->info_comment[l + 1] = '\0';
						}
					} else if (n > 0 &&
					    (unsigned char)buf[0] >= 32 &&
					    l < sizeof(w->info_comment) - 1) {
						w->info_comment[l] = buf[0];
						w->info_comment[l + 1] = '\0';
					}
					comment_set(w->info_path, w->info_comment);
					draw_window(w);
					break;
				}
				break;
			}
			if (rename_win == w) {
				char buf[32];
				int n;
				ks = XLookupKeysym(&ev.xkey, 0);
				n = XLookupString(&ev.xkey, buf,
				    sizeof(buf), &ks, NULL);
				if (ks == XK_Return || ks == XK_KP_Enter) {
					commit_rename();
					break;
				}
				if (ks == XK_Escape) {
					rename_win = NULL;
					rename_idx = -1;
					rename_select_all = 0;
					draw_window(w);
					break;
				}
				if (ks == XK_BackSpace) {
					if (rename_select_all) {
						rename_len = 0;
						rename_buf[0] = '\0';
						rename_select_all = 0;
					} else if (rename_len > 0) {
						rename_buf[--rename_len] = '\0';
					}
					draw_window(w);
					break;
				}
				if (n > 0 && (unsigned char)buf[0] >= 32 &&
				    rename_len < (int)sizeof(rename_buf) - 1) {
					if (rename_select_all) {
						rename_len = 0;
						rename_buf[0] = '\0';
						rename_select_all = 0;
					}
					rename_buf[rename_len++] = buf[0];
					rename_buf[rename_len] = '\0';
					draw_window(w);
				}
				break;
			}
			if (w->addr_focus) {
				char buf[32];
				int n;
				ks = XLookupKeysym(&ev.xkey, 0);
				n = XLookupString(&ev.xkey, buf,
				    sizeof(buf), &ks, NULL);
				if (ks == XK_Return || ks == XK_KP_Enter) {
					char p[PATH_MAX];
					expand_path(w->addr, p, sizeof(p));
					w->addr_focus = 0;
					navigate_to(w, p);
					break;
				}
				if (ks == XK_Escape) {
					w->addr_focus = 0;
					set_addr(w);
					draw_toolbar(w);
					break;
				}
				if (ks == XK_BackSpace) {
					if (w->addr_len > 0)
						w->addr[--w->addr_len] = '\0';
					draw_toolbar(w);
					break;
				}
				if (n > 0 && (unsigned char)buf[0] >= 32 &&
				    w->addr_len < (int)sizeof(w->addr) - 1) {
					w->addr[w->addr_len++] = buf[0];
					w->addr[w->addr_len] = '\0';
					draw_toolbar(w);
				}
				break;
			}
			ks = XLookupKeysym(&ev.xkey, 0);
			/* Raccourcis ⌘ : le WM les intercepte quand son grab est
			 * actif ; sinon (course au démarrage) ils arrivent ici.
			 * Les deux voies s'excluent, la commande ne tourne qu'une
			 * fois. */
			if (ev.xkey.state & Mod1Mask) {
				if (ks == XK_c) {
					copier_selection(w, 0);
					break;
				}
				if (ks == XK_x) {
					copier_selection(w, 1);
					break;
				}
				if (ks == XK_v) {
					coller(w);
					break;
				}
				if (ks == XK_m) {
					creer_alias(w);
					break;
				}
				if (ks == XK_a) {
					int i;

					for (i = 0; i < w->n; i++)
						w->icons[i].selected = 1;
					draw_window(w);
					break;
				}
				if (ks == XK_i) {
					open_info_for(w, w->selected);
					break;
				}
			}
			if (ks == XK_Left)
				key_move(w, -1, 0);
			else if (ks == XK_Right)
				key_move(w, 1, 0);
			else if (ks == XK_Up)
				key_move(w, 0, -1);
			else if (ks == XK_Down)
				key_move(w, 0, 1);
			else if (ks == XK_Return || ks == XK_KP_Enter)
				start_rename(w);
			else if (ks == XK_Delete || ks == XK_BackSpace)
				move_to_trash(w);
			break;
		}
		case SelectionRequest:
			pp_requete(&ev.xselectionrequest);
			break;
		case SelectionClear:
			if (ev.xselection.selection == a_clip && !pp_interne()) {
				FWin *o;

				pp_n = 0;
				pp_couper = 0;
				for (o = wins; o != NULL; o = o->next)
					if (!o->is_info)
						update_menu(o);
			}
			break;
		case SelectionNotify:
			if (ev.xselection.selection != a_clip &&
			    ev.xselection.selection != a_xdnd_selection)
				break;
			if (ev.xselection.property == None) {
				if (ev.xselection.selection == a_clip &&
				    ev.xselection.target == a_uri_list && w != NULL) {
					XConvertSelection(dpy, a_clip, a_utf8, a_clip, w->win, CurrentTime);
					XFlush(dpy);
				} else if (ev.xselection.selection == a_xdnd_selection &&
				    xdnd_in != None && w != NULL) {
					xdnd_message(a_xdnd_finished, xdnd_in, w->win, 0, 0, 0, 0);
					XFlush(dpy);
					xdnd_in = None;
					xdnd_in_accept = 0;
				}
				break;
			} else {
				Atom type;
				int format;
				unsigned long n, after;
				unsigned char *data = NULL;

				if (XGetWindowProperty(dpy, ev.xselection.requestor,
				    ev.xselection.property, 0, 262144, True, AnyPropertyType,
				    &type, &format, &n, &after, &data) == Success &&
				    data != NULL && format == 8) {
					if (ev.xselection.selection == a_clip) {
						pp_n = pp_lire(data, n,
						    ev.xselection.target == a_uri_list, pp_chemins);
						pp_couper = 0;
						if (w != NULL && pp_n > 0) {
							update_menu(w);
							coller_chemins(w, pp_chemins, pp_n, 0);
						}
					} else if (w != NULL && xdnd_in != None) {
						xdnd_n = pp_lire(data, n, 1, xdnd_chemins);
						if (xdnd_n > 0)
							coller_chemins(w, xdnd_chemins, xdnd_n, 0);
						xdnd_message(a_xdnd_finished, xdnd_in, w->win,
						    xdnd_n > 0, xdnd_n > 0 ? (long)a_xdnd_action_copy : 0,
						    0, 0);
						XFlush(dpy);
						xdnd_n = 0;
						xdnd_in = None;
						xdnd_in_accept = 0;
					}
					XFree(data);
				}
			}
			break;
		case ClientMessage:
			if (ev.xclient.message_type == a_xdnd_enter ||
			    ev.xclient.message_type == a_xdnd_position ||
			    ev.xclient.message_type == a_xdnd_leave ||
			    ev.xclient.message_type == a_xdnd_drop) {
				xdnd_entree(&ev.xclient, w);
				break;
			}
			if (ev.xclient.message_type == a_xdnd_status) {
				xdnd_out_accept = (ev.xclient.data.l[1] & 1) != 0;
				break;
			}
			if (ev.xclient.message_type == a_xdnd_finished) {
				xdnd_out = None;
				xdnd_out_accept = 0;
				xdnd_n = 0;
				XSetSelectionOwner(dpy, a_xdnd_selection, None, CurrentTime);
				break;
			}
			if (ev.xclient.message_type == a_protocols &&
			    (Atom)ev.xclient.data.l[0] == a_delete) {
				if (w != NULL && !w->is_desktop)
					close_window(w);
				break;
			}
			if (ev.xclient.message_type == a_cmd) {
				char *cmd = read_command(ev.xclient.window);
				if (cmd != NULL) {
					handle_command(bureau_au_premier_plan() && desktop != NULL ?
					    desktop : w != NULL ? w : active, cmd);
					free(cmd);
				}
			}
			break;
		case DestroyNotify:
			if (w != NULL && !w->is_desktop)
				free_window(w);
			break;
		default:
			break;
		}
	}
	return 0;
}
