/* test-fuseau : la clé « fuseau » des réglages s'applique à chaud (ptk_fuseau_suivre). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ptk.h"

static int echecs;

static void
ecrire(const char *home, const char *zone)
{
	char p[512], n[520];
	FILE *f;

	/* comme le tableau : fichier neuf puis rename() */
	snprintf(p, sizeof(p), "%s/.config/platine/reglages", home);
	snprintf(n, sizeof(n), "%s.neuf", p);
	f = fopen(n, "w");
	if (f == NULL) {
		perror(p);
		exit(1);
	}
	fprintf(f, "horloge_24h 1\nfuseau %s\n", zone);
	fclose(f);
	rename(n, p);
}

static int
heure(void)
{
	time_t t = 1700000000;	/* 14 nov. 2023 22:13:20 UTC */

	return localtime(&t)->tm_hour;
}

static void
verifier(const char *quoi, int obtenu, int attendu)
{
	if (obtenu != attendu) {
		fprintf(stderr, "ÉCHEC %s : %d au lieu de %d\n", quoi, obtenu, attendu);
		echecs++;
	} else
		printf("ok   %s\n", quoi);
}

int
main(void)
{
	char home[] = "/tmp/platine-fuseau.XXXXXX", d[600];
	const char *tz;

	if (mkdtemp(home) == NULL)
		return 1;
	snprintf(d, sizeof(d), "mkdir -p %s/.config/platine", home);
	if (system(d) != 0)
		return 1;
	setenv("HOME", home, 1);
	setenv("TZ", "UTC", 1);
	tzset();

	ecrire(home, "Europe/Paris");
	ptk_fuseau_suivre();
	verifier("Europe/Paris appliqué", heure(), 23);
	tz = getenv("TZ");
	verifier("TZ exporté aux enfants", tz != NULL && strcmp(tz, "Europe/Paris") == 0, 1);

	ecrire(home, "Asia/Tokyo");	/* même seconde : la date nanoseconde doit suffire */
	ptk_fuseau_suivre();
	verifier("Asia/Tokyo sans attendre", heure(), 7);

	ecrire(home, "../../etc/passwd");
	ptk_fuseau_suivre();
	verifier("chemin refusé", heure(), 7);
	ecrire(home, "Pas/Un_Fuseau");
	ptk_fuseau_suivre();
	verifier("fuseau inconnu ignoré", heure(), 7);
	verifier("validation UTC", ptk_fuseau_valide("UTC"), 1);
	verifier("validation /etc", ptk_fuseau_valide("/etc/localtime"), 0);

	snprintf(d, sizeof(d), "rm -rf %s", home);
	if (system(d) != 0)
		return 1;
	return echecs != 0;
}
