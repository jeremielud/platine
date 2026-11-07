/*
 * Lecteur QuickTime : le pont de Platine vers la vidéo et le son, à l'image de QuickTime
 * Player 5 de Mac OS 9 (Applications/QuickTime/QuickTime Player).
 *
 * La fenêtre est en métal brossé : l'image du film, la barre de temps (glisser ou
 * cliquer pour aller ailleurs dans le film), puis la rangée de commandes avec les
 * boutons extraits de QuickTime Player 5 — volume, début, recul, lecture/pause,
 * avance, fin. Sans film, la fenêtre montre le panneau de QuickTime Player.
 *
 * Le décodage est confié à mplayer, lancé en mode esclave dans une sous-fenêtre
 * (-wid) : les commandes passent par son entrée, les réponses (position, durée,
 * taille de l'image) reviennent par sa sortie, lue par la boucle de la Toolbox.
 * Barre d'espace : lecture/pause ; flèches : ±10 s ; ↑ ↓ : volume.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/keysym.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"

#define TYPES "mov mp4 m4v avi mpg mpeg mkv webm ogv flv wmv 3gp mp3 m4a aac ogg oga flac wav aif aiff"
PTK_IDENTITE("Lecteur QuickTime", "TVOD", "app-quicktime", "5.0.2, Platine 2026", TYPES);

#define BORD 8
#define TEMPS_H 14
#define COMMANDES_H 46
#define MAX_VW 640
#define MAX_VH 360

enum { B_DEBUT, B_RECUL, B_LECTURE, B_AVANCE, B_FIN, NB };
static const char *noms_boutons[NB] = { "debut", "recul", "lecture", "avance", "fin" };

static struct {
	PtkApp app;
	PtkWindow *w;
	Window video;
	Pixmap metal, apropos;
	int metal_w, metal_h, apropos_w, apropos_h;
	PtkArgb *img[NB][3], *pause[3], *vol[5];
	int bx[NB], by[NB], bw[NB], bh[NB];
	int presse;		/* bouton enfoncé, -1 */
	int dedans;
	pid_t pid;
	int vers, depuis;	/* tubes vers / depuis mplayer */
	char tampon[4096];
	size_t lu;
	char film[1024];
	int vw, vh;		/* taille de l'image affichée (0 : son seul) */
	double pos, duree;
	int lecture;		/* en lecture (pas en pause) */
	int volume;
	int glisse_temps, glisse_vol;
} Q;

/* ---------- mplayer ---------- */

static void
envoyer(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

static void
envoyer(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	int n;

	if (Q.pid <= 0)
		return;
	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n > 0 && write(Q.vers, b, (size_t)n) < 0 && errno == EPIPE)
		Q.pid = 0;
}

static void
arreter(void)
{
	if (Q.pid > 0) {
		envoyer("quit\n");
		close(Q.vers);
		close(Q.depuis);
		kill(Q.pid, SIGTERM);
		waitpid(Q.pid, NULL, 0);
	}
	Q.pid = 0;
	Q.app.fd_ext = -1;
}

static void geometrie(void);

static void
ligne_recue(char *l)
{
	double v;
	int n;

	if (sscanf(l, "ANS_TIME_POSITION=%lf", &v) == 1)
		Q.pos = v;
	else if (sscanf(l, "ANS_LENGTH=%lf", &v) == 1 || sscanf(l, "ID_LENGTH=%lf", &v) == 1)
		Q.duree = v;
	else if (sscanf(l, "ID_VIDEO_WIDTH=%d", &n) == 1)
		Q.vw = n;
	else if (sscanf(l, "ID_VIDEO_HEIGHT=%d", &n) == 1) {
		Q.vh = n;
		geometrie();
	} else if (strcmp(l, "ANS_pause=yes") == 0)
		Q.lecture = 0;
	else if (strcmp(l, "ANS_pause=no") == 0)
		Q.lecture = 1;
	else if (strncmp(l, "Exiting...", 10) == 0 || strncmp(l, "ID_EXIT=", 8) == 0)
		Q.lecture = 0;
}

static void
lire_mplayer(PtkApp *app, void *user)
{
	ssize_t n;
	char *nl;

	(void)user;
	n = read(Q.depuis, Q.tampon + Q.lu, sizeof(Q.tampon) - 1 - Q.lu);
	if (n <= 0) {
		if (n == 0 || errno != EAGAIN) {
			app->fd_ext = -1;
			Q.lecture = 0;
			ptk_window_invalidate(Q.w);
		}
		return;
	}
	Q.lu += (size_t)n;
	Q.tampon[Q.lu] = '\0';
	while ((nl = strpbrk(Q.tampon, "\r\n")) != NULL) {
		*nl = '\0';
		ligne_recue(Q.tampon);
		memmove(Q.tampon, nl + 1, Q.lu - (size_t)(nl + 1 - Q.tampon) + 1);
		Q.lu = strlen(Q.tampon);
	}
	if (Q.lu >= sizeof(Q.tampon) - 1)
		Q.lu = 0;
}

static void
ouvrir(const char *film)
{
	int a[2], b[2];
	char wid[32];

	arreter();
	snprintf(Q.film, sizeof(Q.film), "%s", film);
	Q.pos = Q.duree = 0;
	Q.vw = Q.vh = 0;
	if (pipe(a) != 0 || pipe(b) != 0)
		return;
	snprintf(wid, sizeof(wid), "%lu", (unsigned long)Q.video);
	Q.pid = fork();
	if (Q.pid == 0) {
		int nul = open("/dev/null", O_WRONLY);

		dup2(a[0], 0);
		dup2(b[1], 1);
		if (nul >= 0)
			dup2(nul, 2);
		close(a[1]);
		close(b[0]);
		{	/* mplayer de pkgsrc, même si la session ne l'a pas dans son PATH */
			char chemin[2048];
			const char *p = getenv("PATH");

			snprintf(chemin, sizeof(chemin), "%s:/usr/pkg/bin:/usr/local/bin", p ? p : "/usr/bin:/bin");
			setenv("PATH", chemin, 1);
		}
		execlp("mplayer", "mplayer", "-slave", "-quiet", "-identify", "-idle",
		    "-noconsolecontrols", "-nomouseinput", "-input", "nodefault-bindings",
		    "-nofs", "-zoom", "-vo", "xv,x11", "-wid", wid, "-volume", "60",
		    "--", film, (char *)NULL);
		_exit(127);
	}
	close(a[0]);
	close(b[1]);
	if (Q.pid < 0)
		return;
	Q.vers = a[1];
	Q.depuis = b[0];
	fcntl(Q.depuis, F_SETFL, O_NONBLOCK);
	Q.app.fd_ext = Q.depuis;
	Q.app.fd_pret = lire_mplayer;
	Q.lecture = 1;
	Q.volume = 60;
	geometrie();	/* commandes seules en attendant la taille de l'image (un son n'en a pas) */
	{
		const char *s = strrchr(film, '/');

		ptk_window_set_title(Q.w, s ? s + 1 : film);
		ptk_window_set_document(Q.w, film);
	}
	/* mplayer absent : l'enfant sort avec 127 tout de suite */
	usleep(300000);
	if (waitpid(Q.pid, NULL, WNOHANG) == Q.pid) {
		Q.pid = 0;
		Q.app.fd_ext = -1;
		ptk_alert(Q.w, "Impossible de lire ce film",
		    "Le Lecteur QuickTime confie le décodage à mplayer : vérifiez qu’il est "
		    "installé (pkg_add mplayer) et que le fichier est un film ou un son.", "OK");
	}
}

/* toutes les demi-secondes : position et état */
static void
tic(PtkApp *app, void *user)
{
	(void)app;
	(void)user;
	if (Q.pid <= 0)
		return;
	envoyer("pausing_keep_force get_property pause\n");
	envoyer("pausing_keep_force get_time_pos\n");
	if (Q.duree <= 0)
		envoyer("pausing_keep_force get_time_length\n");
	if (!Q.glisse_temps && !Q.glisse_vol)
		ptk_window_invalidate(Q.w);
}

/* ---------- géométrie et dessin ---------- */

static int
zone_video_h(void)
{
	if (Q.pid > 0 || Q.film[0])
		return Q.vh > 0 ? Q.w->h - 2 * BORD - TEMPS_H - COMMANDES_H - 6 : 0;
	return Q.apropos_h;
}

static void
geometrie(void)
{
	int w = 480, h = 0;

	if (Q.vw > 0 && Q.vh > 0) {
		w = Q.vw;
		h = Q.vh;
		if (w > MAX_VW) {
			h = h * MAX_VW / w;
			w = MAX_VW;
		}
		if (h > MAX_VH) {
			w = w * MAX_VH / h;
			h = MAX_VH;
		}
		if (w < 300) {
			h = h * 300 / w;
			w = 300;
		}
	} else if (!Q.film[0]) {
		w = Q.apropos_w;
		h = Q.apropos_h;
	}
	XResizeWindow(Q.app.dpy, Q.w->win, (unsigned)(w + 2 * BORD),
	    (unsigned)(h + 2 * BORD + TEMPS_H + COMMANDES_H + 6));
}

static void
placer_boutons(void)
{
	int y0 = Q.w->h - COMMANDES_H, i, x, cx = Q.w->w / 2;
	static const int lw[NB] = { 29, 37, 41, 37, 29 }, lh[NB] = { 30, 38, 42, 38, 30 };

	x = cx - 41 / 2 - 37 - 29 - 4;
	for (i = 0; i < NB; i++) {
		Q.bx[i] = x;
		Q.bw[i] = lw[i];
		Q.bh[i] = lh[i];
		Q.by[i] = y0 + (COMMANDES_H - lh[i]) / 2;
		x += lw[i] + 2;
	}
}

static void
temps_txt(double t, char *o, size_t n)
{
	int s = t > 0 ? (int)t : 0;

	snprintf(o, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
}

static void
dessiner(PtkWindow *w, void *user)
{
	Ptk *p = w->p;
	int vy = BORD, vh = zone_video_h(), ty, i, x, y;
	PtkColor lcd = ptk_color(p, "#c8d0c0"), fonce = ptk_color(p, "#555555");

	(void)user;
	if (Q.metal != None) {
		Pixmap t = ptk_tile_pixmap(p, Q.metal, Q.metal_w, Q.metal_h, w->w, w->h);

		XCopyArea(p->dpy, t, w->surface, p->gc, 0, 0, (unsigned)w->w, (unsigned)w->h, 0, 0);
		XFreePixmap(p->dpy, t);
	} else
		ptk_fill(p, w->surface, 0, 0, w->w, w->h, ptk_color(p, "#bbbbbb"));
	/* l'image : sous-fenêtre de mplayer, ou le panneau de QuickTime Player */
	if (Q.film[0] == '\0' && Q.apropos != None)
		XCopyArea(p->dpy, Q.apropos, w->surface, p->gc, 0, 0, (unsigned)Q.apropos_w,
		    (unsigned)Q.apropos_h, BORD, vy);
	if (vh > 0)
		ptk_rect(p, w->surface, BORD - 1, vy - 1, w->w - 2 * BORD + 2, vh + 2, fonce);
	/* barre de temps : afficheur à cristaux liquides, durée écoulée, curseur */
	ty = w->h - COMMANDES_H - TEMPS_H - 2;
	ptk_fill(p, w->surface, BORD, ty, w->w - 2 * BORD, TEMPS_H, lcd);
	ptk_rect(p, w->surface, BORD, ty, w->w - 2 * BORD, TEMPS_H, fonce);
	{
		char t[32];
		int piste = w->w - 2 * BORD - 70;

		temps_txt(Q.pos, t, sizeof(t));
		ptk_text(p, w->surface, p->font_small, BORD + 4, ty + 11, t, p->text);
		ptk_fill(p, w->surface, BORD + 62, ty + 5, piste, 4, ptk_color(p, "#8a9484"));
		if (Q.duree > 0) {
			x = BORD + 62 + (int)(piste * (Q.pos < Q.duree ? Q.pos : Q.duree) / Q.duree);
			ptk_fill(p, w->surface, BORD + 62, ty + 5, x - BORD - 62, 4, fonce);
			/* curseur triangulaire */
			for (i = 0; i < 5; i++)
				ptk_line(p, w->surface, x - 4 + i, ty + 2 + i, x + 4 - i, ty + 2 + i, p->black);
		}
	}
	/* volume : haut-parleur et réglette */
	y = w->h - COMMANDES_H / 2;
	{
		int niv = Q.volume == 0 ? 0 : 1 + Q.volume * 3 / 101;

		if (Q.vol[niv] != NULL)
			ptk_draw_argb(p, w->surface, Q.vol[niv], BORD + 2, y - 8, 0);
		ptk_fill(p, w->surface, BORD + 26, y - 1, 50, 3, fonce);
		ptk_fill(p, w->surface, BORD + 26 + Q.volume * 46 / 100, y - 6, 5, 12, p->white);
		ptk_rect(p, w->surface, BORD + 26 + Q.volume * 46 / 100, y - 6, 5, 12, p->black);
	}
	placer_boutons();
	for (i = 0; i < NB; i++) {
		int etat = Q.pid <= 0 ? 0 : (Q.presse == i && Q.dedans) ? 2 : 1;
		PtkArgb *a = i == B_LECTURE && Q.lecture ? Q.pause[etat] : Q.img[i][etat];

		if (a != NULL)
			ptk_draw_argb(p, w->surface, a, Q.bx[i], Q.by[i], 0);
	}
}

/* ---------- actions ---------- */

static void
action(int i)
{
	switch (i) {
	case B_LECTURE:
		envoyer("pause\n");
		Q.lecture = !Q.lecture;
		break;
	case B_RECUL:
		envoyer("seek -10 0\n");
		break;
	case B_AVANCE:
		envoyer("seek 10 0\n");
		break;
	case B_DEBUT:
		envoyer("%sseek 0 2\n", Q.lecture ? "" : "pausing ");
		Q.pos = 0;
		break;
	case B_FIN:
		envoyer("pausing seek 99.9 1\n");
		Q.lecture = 0;
		break;
	}
}

static int
bouton_en(int x, int y)
{
	int i;

	for (i = 0; i < NB; i++)
		if (x >= Q.bx[i] && x < Q.bx[i] + Q.bw[i] && y >= Q.by[i] && y < Q.by[i] + Q.bh[i])
			return i;
	return -1;
}

static void
aller_temps(int x)
{
	int piste = Q.w->w - 2 * BORD - 70;
	double f = (double)(x - BORD - 62) / piste;

	if (Q.duree <= 0)
		return;
	if (f < 0)
		f = 0;
	if (f > 1)
		f = 1;
	Q.pos = f * Q.duree;
	envoyer("%sseek %.1f 2\n", Q.lecture ? "" : "pausing ", Q.pos);
}

static void
regler_volume(int x)
{
	int v = (x - BORD - 26) * 100 / 46;

	Q.volume = v < 0 ? 0 : v > 100 ? 100 : v;
	envoyer("volume %d 1\n", Q.volume);
}

static void
evenement(PtkWindow *w, PtkEvent *e, void *user)
{
	int ty = w->h - COMMANDES_H - TEMPS_H - 2, y = w->h - COMMANDES_H / 2;

	(void)user;
	switch (e->type) {
	case PTK_EV_MOUSE_DOWN:
		if (Q.pid > 0 && e->y >= ty && e->y < ty + TEMPS_H && e->x >= BORD + 58) {
			Q.glisse_temps = 1;
			aller_temps(e->x);
		} else if (Q.pid > 0 && e->x >= BORD + 20 && e->x < BORD + 82 && e->y > y - 9 && e->y < y + 9) {
			Q.glisse_vol = 1;
			regler_volume(e->x);
		} else if (Q.pid > 0 && (Q.presse = bouton_en(e->x, e->y)) >= 0)
			Q.dedans = 1;
		break;
	case PTK_EV_MOUSE_MOVE:
		if (Q.glisse_temps)
			aller_temps(e->x);
		else if (Q.glisse_vol)
			regler_volume(e->x);
		else if (Q.presse >= 0)
			Q.dedans = bouton_en(e->x, e->y) == Q.presse;
		else
			return;
		break;
	case PTK_EV_MOUSE_UP:
		if (Q.presse >= 0 && Q.dedans)
			action(Q.presse);
		Q.presse = -1;
		Q.glisse_temps = Q.glisse_vol = 0;
		break;
	case PTK_EV_KEY:
		if (e->state & Mod1Mask)
			return;
		if (e->keysym == XK_space || e->keysym == XK_Return)
			action(B_LECTURE);
		else if (e->keysym == XK_Left)
			action(B_RECUL);
		else if (e->keysym == XK_Right)
			action(B_AVANCE);
		else if (e->keysym == XK_Up || e->keysym == XK_Down) {
			Q.volume += e->keysym == XK_Up ? 10 : -10;
			Q.volume = Q.volume < 0 ? 0 : Q.volume > 100 ? 100 : Q.volume;
			envoyer("volume %d 1\n", Q.volume);
		} else
			return;
		break;
	case PTK_EV_RESIZE:
		XMoveResizeWindow(Q.app.dpy, Q.video, BORD, BORD,
		    (unsigned)(w->w - 2 * BORD > 1 ? w->w - 2 * BORD : 1),
		    (unsigned)(zone_video_h() > 1 ? zone_video_h() : 1));
		if (zone_video_h() > 0 && Q.film[0])
			XMapWindow(Q.app.dpy, Q.video);
		else
			XUnmapWindow(Q.app.dpy, Q.video);
		break;
	default:
		return;
	}
	ptk_window_invalidate(w);
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	(void)user;
	if (strncmp(cmd, "Ouvrir le document\t", 19) == 0) {
		ouvrir(cmd + 19);
	} else if (strcmp(cmd, "Ouvrir…") == 0) {
		char ch[1024], dossier[1024];

		/* Movies (Vidéos à l'écran), sinon l'ancien Vidéos d'avant D11 */
		if (!ptk_perso("Movies", dossier, sizeof(dossier)) || access(dossier, F_OK) != 0)
			ptk_perso("Vidéos", dossier, sizeof(dossier));
		if (ptk_ouvrir(&Q.app, TYPES, dossier, ch, sizeof(ch)))
			ouvrir(ch);
	} else if (strcmp(cmd, "Lecture / Pause") == 0)
		action(B_LECTURE);
	else if (strcmp(cmd, "Aller au début") == 0)
		action(B_DEBUT);
	else if (strcmp(cmd, "Aller à la fin") == 0)
		action(B_FIN);
	else if (strcmp(cmd, "Taille réelle") == 0)
		geometrie();
	else if (strcmp(cmd, "À propos du Lecteur QuickTime") == 0)
		ptk_alert(w, "Lecteur QuickTime",
		    "Lit les films et les sons à la manière de QuickTime Player 5. "
		    "Le décodage est assuré par mplayer.", "OK");
	ptk_window_invalidate(w);
}

static int
fermer(PtkWindow *w, void *user)
{
	(void)w;
	(void)user;
	arreter();
	return 1;
}

int
main(int argc, char **argv)
{
	int i, e;
	char nom[64];

	signal(SIGPIPE, SIG_IGN);
	memset(&Q, 0, sizeof(Q));
	Q.presse = -1;
	Q.volume = 60;
	if (ptk_app_init(&Q.app, "Lecteur QuickTime", "app-quicktime") != 0)
		return 1;
	Q.metal = ptk_load_ppm(&Q.app.ptk, "quicktime-metal.ppm", &Q.metal_w, &Q.metal_h);
	Q.apropos = ptk_load_ppm(&Q.app.ptk, "quicktime-apropos.ppm", &Q.apropos_w, &Q.apropos_h);
	if (Q.apropos == None) {
		Q.apropos_w = 480;
		Q.apropos_h = 160;
	}
	for (i = 0; i < NB; i++)
		for (e = 0; e < 3; e++) {
			snprintf(nom, sizeof(nom), "quicktime/%s-%d", noms_boutons[i], e);
			Q.img[i][e] = ptk_load_argb(&Q.app.ptk, nom);
		}
	for (e = 0; e < 5; e++) {
		snprintf(nom, sizeof(nom), "quicktime/volume-%d", e);
		Q.vol[e] = ptk_load_argb(&Q.app.ptk, nom);
	}
	for (e = 0; e < 3; e++) {
		snprintf(nom, sizeof(nom), "quicktime/pause-%d", e);
		Q.pause[e] = ptk_load_argb(&Q.app.ptk, nom);
	}
	Q.w = ptk_window_new(&Q.app, "Lecteur QuickTime", 100, 60, Q.apropos_w + 2 * BORD,
	    Q.apropos_h + 2 * BORD + TEMPS_H + COMMANDES_H + 6);
	if (Q.w == NULL)
		return 1;
	ptk_window_set_document(Q.w, NULL);
	ptk_window_set_menu(Q.w,
	    "Fichier|Ouvrir…\t⌘O|-|Fermer\t⌘W|-|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|~Couper\t⌘X|~Copier\t⌘C|~Coller\t⌘V\n"
	    "Séquence|Lecture / Pause\t⌘P|Aller au début|Aller à la fin|-|Taille réelle\t⌘0\n"
	    "Aide|À propos du Lecteur QuickTime");
	Q.video = XCreateSimpleWindow(Q.app.dpy, Q.w->win, BORD, BORD, 1, 1, 0, 0,
	    BlackPixel(Q.app.dpy, Q.app.screen));
	/* déjà ouvert : l'autre copie reçoit le film et repasse devant */
	if (ptk_instance_unique(&Q.app, Q.w, "TVOD", argc > 1 ? argv[1] :
	    Q.app.document[0] ? Q.app.document : NULL))
		return 0;
	ptk_window_set_draw(Q.w, dessiner, NULL);
	ptk_window_set_event(Q.w, evenement, NULL);
	ptk_window_set_command(Q.w, commande, NULL);
	ptk_window_set_close(Q.w, fermer, NULL);
	Q.app.tic = tic;
	Q.app.tic_periode = 0.5;
	ptk_window_show(Q.w);
	XSync(Q.app.dpy, False);
	if (argc > 1)
		ouvrir(argv[1]);
	else if (Q.app.document[0] != '\0')
		ouvrir(Q.app.document);
	ptk_app_run(&Q.app);
	arreter();
	return 0;
}
