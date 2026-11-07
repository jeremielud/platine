/*
 * platine-info — fenêtre « Lire les informations » de Platine.
 *
 * Reproduit la fenêtre Mac OS 9.2 mesurée sur la VM :
 *   fenêtre client 342×378, icône 32×32 à (66,9), champ Nom à (65,16,230,20),
 *   popup Show à (65,55,166,20), libellés gras à x=16, valeurs à x=60,
 *   popup Label à (62,203,195,17), zone Commentaires à (23,253,292,80).
 */
#include <locale.h>
#include <dirent.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xutil.h>
#include "../../../toolbox/ptk_app.h"

#include "../../../toolbox/ptk_identite.h"

/* identité embarquée (section .platine), lue par le Finder et l'Installateur */
PTK_IDENTITE("Lire les informations", "pinf", "app-finder", PTK_VERSION, "");


typedef struct {
	PtkApp app;
	PtkWindow *win;
	PtkArgb *arg;
	XftFont *gras;
	Pixmap icon, icon_mask;
	PtkControl *name;
	PtkControl *show;
	PtkControl *label;
	PtkControl *comments;
	char path[PATH_MAX];
	char dir[PATH_MAX];
	char base[256];
	char state_dir[PATH_MAX - 64];
	char kind[64];
	char size[192];
	char where[PATH_MAX];
	char created[64];
	char modified[64];
	int is_dir;
	int label_index;
	int prog;		/* fichier exécutable : un programme */
	PtkIdentite id;		/* son identité embarquée, si elle existe */
	char version[64];
} Info;

/* décalage vertical sous la ligne Version d'un programme */
#define DECALAGE(d) ((d)->prog ? 17 : 0)

static const char *label_names[] = {
	"Aucune", "Essentiel", "Important", "Travail",
	"Personnel", "À faire", "Favori", "Projet"
};

static const char *label_colors[] = {
	NULL, "#ddaa44", "#cc4444", "#44aa44",
	"#4488cc", "#8844cc", "#cc4488", "#888888"
};

static void
join_path(char *dst, size_t dstsz, const char *dir, const char *name)
{
	if (strcmp(dir, "/") == 0)
		snprintf(dst, dstsz, "/%s", name);
	else
		snprintf(dst, dstsz, "%s/%s", dir, name);
}

static void
comment_file(Info *d, char *out, size_t outsz)
{
	snprintf(out, outsz, "%s/commentaires", d->state_dir);
}

static void
comment_get(Info *d, char *out, size_t outsz)
{
	char file[PATH_MAX], line[4096];
	FILE *f;

	out[0] = '\0';
	comment_file(d, file, sizeof(file));
	f = fopen(file, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *tab = strchr(line, '\t');
		char *p;
		size_t o = 0;

		if (tab == NULL)
			continue;
		*tab = '\0';
		if (strcmp(line, d->path) != 0)
			continue;
		p = tab + 1;
		while (*p && *p != '\n' && o + 1 < outsz) {
			if (p[0] == '\\' && p[1] == 'n') {
				out[o++] = '\n';
				p += 2;
			} else {
				out[o++] = *p++;
			}
		}
		out[o] = '\0';
		break;
	}
	fclose(f);
}

static void
comment_set(Info *d, const char *text)
{
	char file[PATH_MAX], tmp[PATH_MAX], line[4096];
	FILE *in, *out;
	int i;

	comment_file(d, file, sizeof(file));
	snprintf(tmp, sizeof(tmp), "%s.tmp", file);
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
			char *p;

			if (tab == NULL)
				continue;
			*tab = '\0';
			if (strcmp(line, d->path) == 0)
				continue;
			fputs(line, out);
			fputc('\t', out);
			for (p = tab + 1; *p && *p != '\n'; p++)
				fputc(*p, out);
			fputc('\n', out);
		}
		fclose(in);
	}
	fprintf(out, "%s\t", d->path);
	for (i = 0; text[i]; i++) {
		if (text[i] == '\n')
			fputs("\\n", out);
		else if (text[i] != '\t')
			fputc(text[i], out);
	}
	fputc('\n', out);
	fclose(out);
	rename(tmp, file);
}

static void
label_file(Info *d, char *out, size_t outsz)
{
	snprintf(out, outsz, "%s/etiquettes", d->state_dir);
}

/* métadonnées type/créateur partagées avec le Finder (finder2/types) */
static int
types_get(Info *d, char *type, size_t tsz, char *crea, size_t csz)
{
	char file[PATH_MAX], line[PATH_MAX * 2];
	FILE *f;

	type[0] = '\0';
	crea[0] = '\0';
	snprintf(file, sizeof(file), "%s/types", d->state_dir);
	f = fopen(file, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *p[4], *save;
		int n = 0;
		char *tok = strtok_r(line, "\t\n", &save);

		while (tok != NULL && n < 4) {
			p[n++] = tok;
			tok = strtok_r(NULL, "\t\n", &save);
		}
		if (n >= 3 && strcmp(p[0], d->path) == 0) {
			snprintf(type, tsz, "%s", p[1]);
			snprintf(crea, csz, "%s", p[2]);
			break;
		}
	}
	fclose(f);
	return type[0] != '\0' || crea[0] != '\0';
}

static const struct { const char *code, *label; } type_labels[] = {
	{ "TEXT", "Document texte" }, { "ttro", "Document texte" },
	{ "PICT", "Image" }, { "PNG ", "Image" }, { "JPEG", "Image" },
	{ "GIFf", "Image" }, { "PDF ", "Document PDF" },
	{ "WAVE", "Son" }, { "AIFF", "Son" }, { "Mp3 ", "Son" },
	{ "MooV", "Film" }, { "ZIP ", "Archive" },
	{ NULL, NULL }
};

static const struct { const char *ext, *label; } kind_ext[] = {
	{ "txt", "Document texte" }, { "md", "Document texte" },
	{ "log", "Document texte" },
	{ "png", "Image" }, { "jpg", "Image" }, { "jpeg", "Image" },
	{ "gif", "Image" }, { "bmp", "Image" }, { "webp", "Image" },
	{ "pdf", "Document PDF" },
	{ "wav", "Son" }, { "mp3", "Son" }, { "ogg", "Son" },
	{ "flac", "Son" }, { "opus", "Son" },
	{ "avi", "Film" }, { "mp4", "Film" }, { "mkv", "Film" },
	{ "mov", "Film" }, { "webm", "Film" },
	{ "zip", "Archive" }, { "tar", "Archive" }, { "gz", "Archive" },
	{ "tgz", "Archive" }, { "bz2", "Archive" }, { "rar", "Archive" },
	{ "7z", "Archive" },
	{ "c", "Source C" }, { "h", "Source C" },
	{ "py", "Script Python" }, { "sh", "Script shell" },
	{ "html", "Page web" }, { "htm", "Page web" },
	{ NULL, NULL }
};

static const char *
kind_label(Info *d, const char *base, int is_dir,
    char *type, size_t tsz, char *crea, size_t csz)
{
	static char buf[96];
	const char *dot;
	int i, a;

	buf[0] = '\0';
	a = types_get(d, type, tsz, crea, csz);
	if (!is_dir && a && type[0] != '\0') {
		for (i = 0; type_labels[i].code != NULL; i++)
			if (strcmp(type, type_labels[i].code) == 0) {
				snprintf(buf, sizeof(buf), "%s (%s/%s)",
				    type_labels[i].label, type,
				    crea[0] != '\0' ? crea : "--");
				return buf;
			}
		snprintf(buf, sizeof(buf), "type %s (%s/%s)", type, type,
		    crea[0] != '\0' ? crea : "--");
		return buf;
	}
	if (is_dir)
		return "dossier";
	dot = strrchr(base, '.');
	if (dot != NULL)
		for (i = 0; kind_ext[i].ext != NULL; i++)
			if (strcasecmp(dot + 1, kind_ext[i].ext) == 0)
				return kind_ext[i].label;
	return "document";
}

static int
label_get(Info *d)
{
	char file[PATH_MAX], line[PATH_MAX + 32];
	FILE *f;
	int v = 0;

	label_file(d, file, sizeof(file));
	f = fopen(file, "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof(line), f) != NULL) {
		char *tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		if (strcmp(line, d->path) == 0) {
			v = atoi(tab + 1);
			break;
		}
	}
	fclose(f);
	return v;
}

static void
label_set(Info *d, int v)
{
	char file[PATH_MAX], tmp[PATH_MAX], line[PATH_MAX + 32];
	FILE *in, *out;

	label_file(d, file, sizeof(file));
	snprintf(tmp, sizeof(tmp), "%s.tmp", file);
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
			if (strcmp(line, d->path) == 0)
				continue;
			fputs(line, out);
			fputc('\t', out);
			fputs(tab + 1, out);
		}
		fclose(in);
	}
	fprintf(out, "%s\t%d\n", d->path, v);
	fclose(out);
	rename(tmp, file);
}

/* taille cumulée d'un dossier et nombre d'éléments, sans suivre les liens */
static void
taille_dossier(const char *chemin, long long *oct, long long *n)
{
	DIR *r = opendir(chemin);
	struct dirent *e;

	if (r == NULL)
		return;
	while ((e = readdir(r)) != NULL) {
		char c[PATH_MAX];
		struct stat st;

		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		snprintf(c, sizeof(c), "%s/%s", chemin, e->d_name);
		if (lstat(c, &st) != 0)
			continue;
		(*n)++;
		if (S_ISDIR(st.st_mode))
			taille_dossier(c, oct, n);
		else
			*oct += st.st_size;
	}
	closedir(r);
}

/* « zéro Ko sur le disque (0 octet), pour 0 élément » : la formule du Finder */
static void
taille_texte(char *b, size_t l, long long oct, int dossier, long long n)
{
	char t[48];

	if (oct == 0)
		snprintf(t, sizeof(t), "zéro Ko");
	else if (oct < 1024 * 1024)
		snprintf(t, sizeof(t), "%lld Ko", (oct + 1023) / 1024);
	else
		snprintf(t, sizeof(t), "%.1f Mo", (double)oct / (1024 * 1024));
	if (dossier)
		snprintf(b, l, "%s sur le disque (%lld octet%s), pour %lld élément%s", t, oct,
		    oct > 1 ? "s" : "", n, n > 1 ? "s" : "");
	else
		snprintf(b, l, "%s sur le disque (%lld octet%s)", t, oct, oct > 1 ? "s" : "");
}

static void
load_info(Info *d)
{
	struct stat st;
	char date[64];

	memset(&st, 0, sizeof(st));
	stat(d->path, &st);
	d->is_dir = S_ISDIR(st.st_mode);
	/* un exécutable est un programme, comme « application program » sous
	 * Mac OS 9 ; sa version vient de son identité (ressource vers) */
	d->prog = S_ISREG(st.st_mode) && access(d->path, X_OK) == 0;
	if (d->prog && ptk_identite_lire(d->path, &d->id))
		snprintf(d->version, sizeof(d->version), "%s", d->id.version);
	if (d->prog)
		snprintf(d->kind, sizeof(d->kind), "programme");
	else {
		char type[8], crea[8];

		snprintf(d->kind, sizeof(d->kind), "%s",
		    kind_label(d, d->base, d->is_dir, type, sizeof(type),
		    crea, sizeof(crea)));
	}
	{
		long long oct = 0, n = 0;

		if (d->is_dir)
			taille_dossier(d->path, &oct, &n);
		else
			oct = st.st_size;
		taille_texte(d->size, sizeof(d->size), oct, d->is_dir, n);
	}
	/* chemin à la façon du Mac : volume et dossiers séparés par des deux-points */
	{
		char *e;

		snprintf(d->where, sizeof(d->where), "Eee PC 701:%s:", d->dir[1] != '\0' ? d->dir + 1 : "");
		for (e = d->where + strlen("Eee PC 701:"); *e != '\0'; e++)
			if (*e == '/')
				*e = ':';
		if (d->dir[1] == '\0')
			d->where[strlen("Eee PC 701:")] = '\0';
	}
	{
		time_t t = st.st_ctime;

		ptk_fuseau_suivre();
		strftime(date, sizeof(date), "%a %e %b %Y, %H:%M", localtime(&t));
		snprintf(d->created, sizeof(d->created), "%s", date);
		t = st.st_mtime;
		strftime(date, sizeof(date), "%a %e %b %Y, %H:%M", localtime(&t));
		snprintf(d->modified, sizeof(d->modified), "%s", date);
	}
	d->label_index = label_get(d);
}

/* libellé en gras aligné à droite sur les deux-points, comme Mac OS 9 (colonne x = 85) */
static void
ligne(Ptk *p, Drawable d, Info *i, int y, const char *lib, const char *val)
{
	ptk_text(p, d, i->gras, 86 - ptk_text_width(p, i->gras, lib), y, lib, p->text);
	if (val != NULL)
		ptk_text(p, d, p->font_small, 89, y, val, p->text);
}

/* trait gravé : #88 puis blanc */
static void
grave_h(Ptk *p, Drawable d, int x0, int x1, int y)
{
	ptk_line(p, d, x0, y, x1, y, ptk_color(p, "#888888"));
	ptk_line(p, d, x0 + 1, y + 1, x1 + 1, y + 1, p->white);
}

static void
grave_v(Ptk *p, Drawable d, int x, int y0, int y1)
{
	ptk_line(p, d, x, y0, x, y1, ptk_color(p, "#888888"));
	ptk_line(p, d, x + 1, y0 + 1, x + 1, y1 + 1, p->white);
}

static void
draw_window(PtkWindow *w, void *user)
{
	Info *d = user;
	Ptk *p = w->p;
	const char *aff = "Afficher :";
	int ax = 65 - ptk_text_width(p, p->font_menu, aff);

	ptk_fill(p, w->surface, 0, 0, w->w, w->h, p->face);

	if (d->arg != NULL)
		ptk_draw_argb(p, w->surface, d->arg, 25, 10, 0);
	else if (d->icon != None)
		ptk_draw_pixmap(p, w->surface, d->icon, d->icon_mask, 25, 10, 32, 32);

	/* groupe gravé (x 13..330, y 68..360), coupé par « Afficher » et son menu local */
	grave_h(p, w->surface, 13, ax - 4, 68);
	grave_h(p, w->surface, 261, 330, 68);
	grave_h(p, w->surface, 13, 330, 360);
	grave_v(p, w->surface, 13, 68, 360);
	grave_v(p, w->surface, 330, 68, 360);

	ptk_text(p, w->surface, p->font_menu, ax, 71, aff, p->text);
	ligne(p, w->surface, d, 94, "Type :", d->kind);
	ligne(p, w->surface, d, 111, "Taille :", d->size);
	ligne(p, w->surface, d, 142, "Où :", d->where);
	ligne(p, w->surface, d, 187, "Créé :", d->created);
	ligne(p, w->surface, d, 204, "Modifié :", d->modified);
	/* programme : la version s'intercale avant l'étiquette (info-application.png) */
	if (d->prog)
		ligne(p, w->surface, d, 221, "Version :",
		    d->version[0] ? d->version : "non disponible");
	ligne(p, w->surface, d, 225 + DECALAGE(d), "Étiquette :", NULL);
	ptk_text(p, w->surface, d->gras, 25, 246 + DECALAGE(d), "Commentaires :", p->text);

	/* étiquette colorée à gauche du nom */
	if (d->label_index > 0 && d->label_index < 8 &&
	    label_colors[d->label_index] != NULL) {
		PtkColor c = ptk_color(p, label_colors[d->label_index]);
		ptk_fill(p, w->surface, 58, 22, 7, 7, c);
		ptk_rect(p, w->surface, 58, 22, 7, 7, p->black);
	}
}

static void
name_action(PtkWindow *w, PtkControl *c, void *user)
{
	Info *d = user;
	char np[PATH_MAX];

	if (strcmp(c->text, d->base) == 0)
		return;
	join_path(np, sizeof(np), d->dir, c->text);
	if (rename(d->path, np) == 0) {
		snprintf(d->path, sizeof(d->path), "%s", np);
		snprintf(d->base, sizeof(d->base), "%s", c->text);
	}
	ptk_window_set_title(w, d->base);
	ptk_window_invalidate(w);
}

static void
comments_action(PtkWindow *w, PtkControl *c, void *user)
{
	Info *d = user;
	(void)w;
	comment_set(d, c->text);
}

static void
popup_action(PtkWindow *w, PtkControl *c, void *user)
{
	Info *d = user;

	if (c == d->label) {
		int i;
		for (i = 0; i < 8; i++)
			if (strcmp(label_names[i], c->text) == 0)
				d->label_index = i;
		label_set(d, d->label_index);
	}
	ptk_window_invalidate(w);
}

int
main(int argc, char **argv)
{
	Info d;
	char *slash;

	setlocale(LC_TIME, "");
	memset(&d, 0, sizeof(d));
	if (argc < 2) {
		fprintf(stderr, "usage: platine-info chemin\n");
		return 1;
	}
	if (argv[1][0] == '/') {
		snprintf(d.path, sizeof(d.path), "%s", argv[1]);
	} else {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd)) != NULL)
			snprintf(d.path, sizeof(d.path), "%s/%s", cwd,
			    argv[1]);
		else
			snprintf(d.path, sizeof(d.path), "%s", argv[1]);
	}
	slash = strrchr(d.path, '/');
	if (slash != NULL && slash != d.path) {
		*slash = '\0';
		snprintf(d.dir, sizeof(d.dir), "%s", d.path);
		snprintf(d.base, sizeof(d.base), "%s", slash + 1);
		*slash = '/';
	} else {
		snprintf(d.dir, sizeof(d.dir), "/");
		snprintf(d.base, sizeof(d.base), "%s", d.path);
	}
	{
		const char *h = getenv("HOME");
		if (h == NULL)
			h = ".";
		snprintf(d.state_dir, sizeof(d.state_dir),
		    "%s/.config/platine/finder2", h);
	}
	load_info(&d);

	if (ptk_app_init(&d.app, "Finder", "dossier") != 0)
		return 1;
	ptk_load_xpm(&d.app.ptk,
	    d.is_dir ? "dossier-32.xpm" : "document-texte-32.xpm",
	    &d.icon, &d.icon_mask);
	d.arg = NULL;
	if (d.prog) {
		/* l'icône que porte le programme, sinon l'icône d'application générique */
		char n[80];

		if (d.id.icone[0] != '\0' && d.id.icone[0] != '@') {
			snprintf(n, sizeof(n), "%s-32", d.id.icone);
			d.arg = ptk_load_argb(&d.app.ptk, n);
			if (d.arg == NULL)
				d.arg = ptk_load_argb(&d.app.ptk, d.id.icone);
		}
		if (d.arg == NULL)
			d.arg = ptk_load_argb(&d.app.ptk, "application-32");
	}
	if (d.arg == NULL)
		d.arg = ptk_load_argb(&d.app.ptk, d.is_dir ? "dossier-32" : "document-32");
	d.gras = ptk_police_bitmap(&d.app.ptk, "geneva-10-gras.bdf", 10);
	if (d.gras == NULL)
		d.gras = d.app.ptk.font_menu;
	d.win = ptk_window_new(&d.app, d.base, 120, 40, 342, 374);
	if (d.win == NULL)
		return 1;
	ptk_window_set_icon(d.win, d.is_dir ? "folder" : "doc");
	{
		/* taille fixe : ni zoom ni poignée, comme sous Mac OS 9 */
		XSizeHints h;

		memset(&h, 0, sizeof(h));
		h.flags = PPosition | PMinSize | PMaxSize;
		h.x = 120;
		h.y = 40;
		h.min_width = h.max_width = 342;
		h.min_height = h.max_height = 374;
		XSetWMNormalHints(d.app.ptk.dpy, d.win->win, &h);
	}
	{
		char title[300];
		snprintf(title, sizeof(title), "%s Info", d.base);
		ptk_window_set_title(d.win, title);
	}
	ptk_window_set_menu(d.win,
	    "Fichier|Fermer\t⌘W\n"
	    "Édition|Copier\t⌘C|Coller\t⌘V\n"
	    "Aide|À propos de Platine");
	ptk_window_set_draw(d.win, draw_window, &d);

	d.name = ptk_control_add(d.win, PTK_TEXTFIELD, 67, 16, 232, 22, "");
	ptk_control_set_text(d.name, d.base);
	d.name->tout_selectionne = 1;
	ptk_control_set_action(d.name, name_action, &d);

	d.show = ptk_control_add(d.win, PTK_POPUP, 67, 57, 190, 20, "");
	ptk_control_set_items(d.show,
	    "Informations générales|Mémoire|Partage|Application");
	ptk_control_set_text(d.show, "Informations générales");
	ptk_control_set_action(d.show, popup_action, &d);

	d.label = ptk_control_add(d.win, PTK_POPUP, 88, 213 + DECALAGE(&d), 176, 17, "");
	d.label->police = d.gras;
	ptk_control_set_items(d.label,
	    "Aucune|Essentiel|Important|Travail|Personnel|À faire|Favori|"
	    "Projet");
	ptk_control_set_text(d.label, label_names[d.label_index]);
	ptk_control_set_action(d.label, popup_action, &d);

	d.comments = ptk_control_add(d.win, PTK_TEXTAREA, 25, 254 + DECALAGE(&d), 295,
	    82 - DECALAGE(&d), "");
	{
		char text[2048];
		comment_get(&d, text, sizeof(text));
		ptk_control_set_text(d.comments, text);
	}
	ptk_control_set_action(d.comments, comments_action, &d);

	ptk_window_show(d.win);
	ptk_app_run(&d.app);
	return 0;
}
