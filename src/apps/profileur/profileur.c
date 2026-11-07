/*
 * Profileur système : à l'image d'Informations Système Apple (Apple System Profiler) de
 * Mac OS 9 — un panneau à onglets qui décrit la machine et ce qui y est installé :
 *
 *   Profil système            matériel, logiciel, mémoire, réseau, énergie
 *   Périphériques et volumes  arbre des pilotes (drvctl) et volumes montés
 *   Tableaux de bord, Extensions, Applications : les programmes du volume et leur
 *                             version (identité embarquée, décision D9)
 *   Dossiers Système          le dossier System, ses sous-dossiers, Finder et Système
 *
 * Fichier ▸ Enregistrer le rapport… écrit le tout en texte (zone de dialogue
 * d'enregistrement) ; Édition ▸ Copier copie l'onglet affiché.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "../../../toolbox/ptk_app.h"
#include "../../../toolbox/ptk_identite.h"
#include "../../../toolbox/ptk_sys.h"

PTK_IDENTITE("Profileur système", "prof", "app-profil", PTK_VERSION, "");

#ifdef __NetBSD__
#define C_PROCESSEUR	"sysctl -n machdep.cpu_brand 2>/dev/null | sed 's/  */ /g'"
#define C_NOYAU		"sysctl -n kern.version 2>/dev/null | head -1"
#define C_DEMARRAGE	"sysctl -n kern.boottime 2>/dev/null"
#define C_BIOS		"echo $(sysctl -n machdep.dmi.bios-version 2>/dev/null) $(sysctl -n machdep.dmi.bios-date 2>/dev/null)"
#define C_INTERFACES	"ifconfig -a 2>/dev/null | awk '/^[a-z]/ { sub(\":\", \"\", $1); i = $1 } " \
			"$1 == \"address:\" { m[i] = $2 } $1 == \"inet\" { a[i] = $2 } " \
			"END { for (i in m) printf \"%s\\t%s %s\\n\", i, m[i], a[i] }'"
#define C_PERIPH	"drvctl -lt 2>/dev/null"
#define C_ENERGIE	"envstat -d acpiacad0 2>/dev/null | awk '/connected:/ { print ($2 == \"TRUE\") ? \"secteur\" : \"batterie\" }'"
#else
#define C_PROCESSEUR	"sed -n 's/^model name[^:]*: *//p' /proc/cpuinfo | head -1"
#define C_NOYAU		"uname -sr"
#define C_DEMARRAGE	"uptime -s 2>/dev/null"
#define C_BIOS		"echo $(cat /sys/class/dmi/id/bios_version 2>/dev/null) $(cat /sys/class/dmi/id/bios_date 2>/dev/null)"
#define C_INTERFACES	"for i in /sys/class/net/*; do n=${i##*/}; printf '%s\\t%s %s\\n' $n $(cat $i/address) " \
			"$(ip -4 -o addr show $n 2>/dev/null | awk '{ print $4 }'); done"
#define C_PERIPH	"lspci 2>/dev/null; lsusb 2>/dev/null"
#define C_ENERGIE	"cat /sys/class/power_supply/AC*/online 2>/dev/null | sed 's/1/secteur/;s/0/batterie/'"
#endif
#define C_ECRAN		"xdpyinfo 2>/dev/null | awk '/dimensions:/ { print $2; exit }'"

enum { O_PROFIL, O_PERIPH, O_TABLEAUX, O_EXTENSIONS, O_APPLIS, O_DOSSIERS, NO };
static const char *noms_onglets = "Profil système|Périphériques|Tableaux de bord|"
    "Extensions|Applications|Dossiers";
static const char *colonnes[NO] = {
	"Élément\tValeur", "Périphérique\tDétail", "Nom\tVersion", "Nom\tVersion",
	"Nom\tVersion", "Dossier\tContenu",
};

static PtkApp app;
static PtkWindow *fen;
static PtkControl *onglets, *listes[NO];
static char contenu[NO][8192];

/* première ligne de la sortie d'une commande, sans fin de ligne ni blancs aux bords */
static void
sortie(const char *cmd, char *out, size_t n)
{
	FILE *f = popen(cmd, "r");
	size_t l;
	char *d;

	out[0] = '\0';
	if (f == NULL)
		return;
	if (fgets(out, (int)n, f) == NULL)
		out[0] = '\0';
	pclose(f);
	l = strlen(out);
	while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == ' ' || out[l - 1] == '\t'))
		out[--l] = '\0';
	for (d = out; *d == ' ' || *d == '\t'; d++)
		;
	memmove(out, d, strlen(d) + 1);
}

/* ajoute une rangée « nom\tvaleur » à la liste (séparateur | des items) ; les | du
 * texte deviennent des barres obliques */
static void
rangee(int o, const char *nom, const char *valeur)
{
	char r[512], *p;
	size_t l = strlen(contenu[o]);

	snprintf(r, sizeof(r), "%s%s\t%s", l ? "|" : "", nom, valeur ? valeur : "");
	for (p = r + (l ? 1 : 0); *p; p++)
		if (*p == '|')
			*p = '/';
	if (l + strlen(r) < sizeof(contenu[o]))
		memcpy(contenu[o] + l, r, strlen(r) + 1);
}

/* titre de section : grisé, comme les triangles de détail d'Informations Système */
static void
section(int o, const char *titre)
{
	char t[128];

	snprintf(t, sizeof(t), "~%s", titre);
	rangee(o, t, "");
}

static void
taille_lisible(unsigned long long octets, char *out, size_t n)
{
	if (octets >= 1024ULL * 1024 * 1024)
		snprintf(out, n, "%.1f Go", (double)octets / (1024.0 * 1024 * 1024));
	else if (octets >= 1024ULL * 1024)
		snprintf(out, n, "%llu Mo", octets / (1024ULL * 1024));
	else
		snprintf(out, n, "%llu Ko", octets / 1024ULL);
}

static void
profil_systeme(void)
{
	char b[256], v[256];
	long util = 0, total = 0;
	struct utsname u;
	FILE *f;

	section(O_PROFIL, "Aperçu du matériel");
	sortie(SYS_FABRICANT, b, sizeof(b));
	sortie(SYS_MODELE, v, sizeof(v));
	if (b[0] || v[0]) {
		char m[512];

		snprintf(m, sizeof(m), "%s %s", b, v);
		rangee(O_PROFIL, "  Ordinateur", m);
	}
	sortie(C_PROCESSEUR, b, sizeof(b));
	rangee(O_PROFIL, "  Processeur", b[0] ? b : "inconnu");
	if (uname(&u) == 0)
		rangee(O_PROFIL, "  Architecture", u.machine);
	ptk_memoire(&util, &total);
	snprintf(b, sizeof(b), "%ld Mo", ptk_memoire_installee_mo(total));
	rangee(O_PROFIL, "  Mémoire installée", b);
	sortie(C_BIOS, b, sizeof(b));
	if (b[0])
		rangee(O_PROFIL, "  Version du BIOS", b);
	sortie(C_ECRAN, b, sizeof(b));
	if (b[0])
		rangee(O_PROFIL, "  Écran", b);
	sortie(SYS_SON, b, sizeof(b));
	if (b[0])
		rangee(O_PROFIL, "  Son", b);

	section(O_PROFIL, "Aperçu du logiciel");
	sortie(C_NOYAU, b, sizeof(b));
	rangee(O_PROFIL, "  Système", b);
	rangee(O_PROFIL, "  Bureau", "Platine " PTK_VERSION);
	sortie(C_DEMARRAGE, b, sizeof(b));
	if (b[0])
		rangee(O_PROFIL, "  Démarré le", b);
	rangee(O_PROFIL, "  Utilisateur", getenv("USER") ? getenv("USER") : getenv("LOGNAME"));

	section(O_PROFIL, "Mémoire");
	snprintf(b, sizeof(b), "%ld Mo", total / 1024);
	rangee(O_PROFIL, "  Disponible pour le système", b);
	snprintf(b, sizeof(b), "%ld Mo", util / 1024);
	rangee(O_PROFIL, "  Utilisée", b);
	snprintf(b, sizeof(b), "%ld Mo", (total - util) / 1024);
	rangee(O_PROFIL, "  Libre", b);

	section(O_PROFIL, "Aperçu du réseau");
	if ((f = popen(C_INTERFACES, "r")) != NULL) {
		char l[256], *tab;

		while (fgets(l, sizeof(l), f) != NULL) {
			l[strcspn(l, "\n")] = '\0';
			if ((tab = strchr(l, '\t')) == NULL || strncmp(l, "lo", 2) == 0)
				continue;
			*tab++ = '\0';
			snprintf(b, sizeof(b), "  %s", l);
			rangee(O_PROFIL, b, tab);
		}
		pclose(f);
	}
	sortie(SYS_WIFI_SSID, b, sizeof(b));
	rangee(O_PROFIL, "  Réseau Wi-Fi", b[0] ? b : "aucun");

	section(O_PROFIL, "Énergie");
	sortie(C_ENERGIE, b, sizeof(b));
	if (b[0])
		rangee(O_PROFIL, "  Alimentation", b);
	sortie(SYS_BATTERIE, b, sizeof(b));
	if (b[0]) {
		strncat(b, " %", sizeof(b) - strlen(b) - 1);
		rangee(O_PROFIL, "  Charge de la batterie", b);
	}
}

static void
peripheriques(void)
{
	FILE *f;
	char l[256], b[128], u[64], lib[64];
	struct statvfs s;
	static const char *points[] = { "/", "/home", "/tmp", "/mnt", "/media" };
	size_t i;

	section(O_PERIPH, "Volumes");
	for (i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
		struct stat st, sr;

		if (statvfs(points[i], &s) != 0 || stat(points[i], &st) != 0)
			continue;
		/* un dossier du volume racine n'est pas un volume à part */
		if (i > 0 && stat("/", &sr) == 0 && sr.st_dev == st.st_dev)
			continue;
		taille_lisible((unsigned long long)s.f_blocks * s.f_frsize, u, sizeof(u));
		taille_lisible((unsigned long long)s.f_bavail * s.f_frsize, lib, sizeof(lib));
		snprintf(b, sizeof(b), "%s, %s libres", u, lib);
		snprintf(l, sizeof(l), "  %s", i == 0 ? "Eee PC 701 (/)" : points[i]);
		rangee(O_PERIPH, l, b);
	}
	section(O_PERIPH, "Périphériques");
	if ((f = popen(C_PERIPH, "r")) != NULL) {
		while (fgets(l, sizeof(l), f) != NULL) {
			char *d;

			l[strcspn(l, "\n")] = '\0';
			if (l[0] == '\0')
				continue;
			/* lspci : « 00:02.0 VGA compatible controller: Intel… » → nom, détail */
			d = strstr(l, ": ");
			if (d != NULL && strchr(l, ' ') < d) {
				*d = '\0';
				snprintf(b, sizeof(b), "  %s", strchr(l, ' ') + 1);
				rangee(O_PERIPH, b, d + 2);
			} else {
				snprintf(b, sizeof(b), "  %s", l);
				rangee(O_PERIPH, b, "");
			}
		}
		pclose(f);
	}
}

static int
cmp_noms(const void *a, const void *b)
{
	return strcoll(*(char *const *)a, *(char *const *)b);
}

/* programmes d'un dossier du volume (un niveau de sous-dossiers) et leur version */
static void
programmes(int o, const char *rel, int profondeur, const char *prefixe)
{
	char dossier[1024], chemin[1400], *noms[256], aff[300];
	DIR *d;
	struct dirent *e;
	int n = 0, i;

	if (!ptk_dans_volume(rel, dossier, sizeof(dossier)) && snprintf(dossier, sizeof(dossier),
	    "%s/%s", ptk_volume(), rel) >= (int)sizeof(dossier))
		return;
	if ((d = opendir(dossier)) == NULL)
		return;
	while ((e = readdir(d)) != NULL && n < 256)
		if (e->d_name[0] != '.')
			noms[n++] = strdup(e->d_name);
	closedir(d);
	qsort(noms, (size_t)n, sizeof(noms[0]), cmp_noms);
	for (i = 0; i < n; i++) {
		struct stat st;
		PtkIdentite id;

		snprintf(chemin, sizeof(chemin), "%s/%s", dossier, noms[i]);
		snprintf(aff, sizeof(aff), "%s%s", prefixe, noms[i]);
		if (lstat(chemin, &st) == 0 && S_ISLNK(st.st_mode)) {
			rangee(o, aff, "alias");
		} else if (stat(chemin, &st) == 0 && S_ISDIR(st.st_mode)) {
			if (profondeur > 0) {
				char sous[1100], p2[64];

				section(o, noms[i]);
				snprintf(sous, sizeof(sous), "%s/%s", rel, noms[i]);
				snprintf(p2, sizeof(p2), "%s  ", prefixe);
				programmes(o, sous, profondeur - 1, p2);
			} else
				rangee(o, aff, "dossier");
		} else if (ptk_identite_lire(chemin, &id)) {
			rangee(o, aff, id.version[0] ? id.version : "—");
		} else if (st.st_mode & S_IXUSR) {
			rangee(o, aff, "programme");
		} else {
			char t[32];

			taille_lisible((unsigned long long)st.st_size, t, sizeof(t));
			rangee(o, aff, t);
		}
		free(noms[i]);
	}
	if (n == 0)
		rangee(o, prefixe[0] ? "  (vide)" : "(dossier vide ou absent)", "");
}

static void
dossiers(void)
{
	static const char *sd[] = { "Extensions", "Control Panels", "Apple Menu Items",
	    "Startup Items", "Control Strip Modules", "Fonts" };
	char chemin[1100], b[64];
	size_t i;

	snprintf(chemin, sizeof(chemin), "%s/System", ptk_volume());
	rangee(O_DOSSIERS, "Système", access(chemin, F_OK) == 0 ? chemin : "absent");
	for (i = 0; i < sizeof(sd) / sizeof(sd[0]); i++) {
		DIR *d;
		struct dirent *e;
		int n = 0;
		char nom[128];

		snprintf(chemin, sizeof(chemin), "%s/System/%s", ptk_volume(), sd[i]);
		if ((d = opendir(chemin)) != NULL) {
			while ((e = readdir(d)) != NULL)
				n += e->d_name[0] != '.';
			closedir(d);
			snprintf(b, sizeof(b), "%d élément%s", n, n > 1 ? "s" : "");
		} else
			snprintf(b, sizeof(b), "absent");
		snprintf(chemin, sizeof(chemin), "%s/System", ptk_volume());
		snprintf(nom, sizeof(nom), "  %s", ptk_nom_affiche(chemin, sd[i]));
		rangee(O_DOSSIERS, nom, b);
	}
	section(O_DOSSIERS, "Logiciel système");
	{
		static const char *f[][2] = { { "Finder", "System/Finder" },
		    { "Système", "System/Système" } };
		PtkIdentite id;

		for (i = 0; i < 2; i++) {
			snprintf(chemin, sizeof(chemin), "%s/%s", ptk_volume(), f[i][1]);
			snprintf(b, sizeof(b), "  %s", f[i][0]);
			rangee(O_DOSSIERS, b, ptk_identite_lire(chemin, &id) ? id.version : "absent");
		}
	}
}

static void
relever(void)
{
	int o;

	for (o = 0; o < NO; o++)
		contenu[o][0] = '\0';
	profil_systeme();
	peripheriques();
	programmes(O_TABLEAUX, "System/Control Panels", 0, "");
	programmes(O_EXTENSIONS, "System/Extensions", 0, "");
	programmes(O_APPLIS, "Applications", 1, "");
	dossiers();
	for (o = 0; o < NO; o++)
		ptk_control_set_items(listes[o], contenu[o]);
}

/* l'onglet o en texte : « nom : valeur », une ligne par rangée */
static size_t
texte_onglet(int o, char *out, size_t n)
{
	const char *p = contenu[o];
	size_t l = 0;

	while (*p && l + 2 < n) {
		const char *fin = strchr(p, '|');
		size_t lg = fin ? (size_t)(fin - p) : strlen(p);
		char r[512], *tab;

		snprintf(r, sizeof(r), "%.*s", (int)lg, p);
		if (r[0] == '~')
			memmove(r, r + 1, strlen(r));
		if ((tab = strchr(r, '\t')) != NULL) {
			*tab = '\0';
			l += (size_t)snprintf(out + l, n - l, tab[1] ? "%s : %s\n" : "%s\n", r, tab + 1);
		} else
			l += (size_t)snprintf(out + l, n - l, "%s\n", r);
		if (l >= n)
			l = n - 1;
		p += lg + (fin ? 1 : 0);
	}
	out[l] = '\0';
	return l;
}

static void
enregistrer(void)
{
	char chemin[1024], t[PTK_TEXT_MAX];
	const char *titres[NO] = { "Profil système", "Périphériques et volumes", "Tableaux de bord",
	    "Extensions", "Applications", "Dossiers Système" };
	FILE *f;
	int o;

	if (!ptk_enregistrer(&app, "Enregistrer le rapport sous :", "Rapport système", NULL,
	    chemin, sizeof(chemin)))
		return;
	if ((f = fopen(chemin, "w")) == NULL) {
		ptk_alert(fen, "Impossible d’enregistrer le rapport",
		    "Vérifiez les permissions du dossier.", "OK");
		return;
	}
	for (o = 0; o < NO; o++) {
		texte_onglet(o, t, sizeof(t));
		fprintf(f, "== %s ==\n%s\n", titres[o], t);
	}
	fclose(f);
}

static void
apres_onglet(PtkWindow *w, PtkControl *c, void *user)
{
	(void)c;
	(void)user;
	ptk_onglets_afficher(w, onglets);
	ptk_window_invalidate(w);
}

static void
commande(PtkWindow *w, const char *cmd, void *user)
{
	(void)user;
	if (strcmp(cmd, "Actualiser") == 0) {
		relever();
		ptk_window_invalidate(w);
	} else if (strcmp(cmd, "Enregistrer le rapport…") == 0) {
		enregistrer();
	} else if (strcmp(cmd, "Copier") == 0) {
		char t[PTK_TEXT_MAX];

		texte_onglet(onglets->value, t, sizeof(t));
		ptk_copier(w, t);
	} else if (strcmp(cmd, "À propos du Profileur système") == 0) {
		ptk_alert(w, "Profileur système",
		    "Décrit le matériel, le système et les programmes installés, "
		    "comme Informations Système Apple.", "OK");
	}
}

static void
evenement(PtkWindow *w, PtkEvent *e, void *user)
{
	int o;

	(void)user;
	if (e->type != PTK_EV_RESIZE)
		return;
	onglets->w = w->w - 20;
	onglets->h = w->h - 20;
	for (o = 0; o < NO; o++) {
		listes[o]->w = w->w - 46;
		listes[o]->h = w->h - 72;
		listes[o]->col2 = listes[o]->w / 2;
	}
	ptk_window_invalidate(w);
}

int
main(void)
{
	int o, W = 660, H = 400;

	if (ptk_app_init(&app, "Profileur système", "app-profil") != 0)
		return 1;
	fen = ptk_window_new(&app, "Profileur système", 80, 40, W, H);
	if (fen == NULL)
		return 1;
	ptk_window_set_menu(fen,
	    "Fichier|Actualiser\t⌘R|Enregistrer le rapport…\t⌘S|-|Fermer\t⌘W|Quitter\t⌘Q\n"
	    "Édition|~Annuler\t⌘Z|-|~Couper\t⌘X|Copier\t⌘C|~Coller\t⌘V\n"
	    "Aide|À propos du Profileur système");
	onglets = ptk_control_add(fen, PTK_ONGLETS, 10, 10, W - 20, H - 20, "");
	ptk_control_set_items(onglets, noms_onglets);
	onglets->page = -1;
	ptk_control_set_action(onglets, apres_onglet, NULL);
	for (o = 0; o < NO; o++) {
		listes[o] = ptk_control_add(fen, PTK_LISTE, 23, 50, W - 46, H - 72, colonnes[o]);
		ptk_control_set_page(listes[o], o);
		listes[o]->value = -1;
		listes[o]->col2 = (W - 46) / 2;
	}
	relever();
	ptk_onglets_afficher(fen, onglets);
	ptk_window_set_command(fen, commande, NULL);
	ptk_window_set_event(fen, evenement, NULL);
	ptk_window_show(fen);
	ptk_app_run(&app);
	return 0;
}
