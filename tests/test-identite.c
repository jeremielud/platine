/* test-identite : lit l'identité embarquée d'un binaire, cherche un programme
 * par signature ou extension dans le volume, résout une référence « @… ».
 *   test-identite lire FICHIER | signature SIG | extension EXT | resoudre REF
 *   test-identite nom DOSSIER NOM | masque DOSSIER NOM   (noms montrés, masquage) */
#include <stdio.h>
#include <string.h>

#include "../toolbox/ptk.h"

int
main(int argc, char **argv)
{
	PtkIdentite id;
	char out[4096];

	if (argc == 4 && strcmp(argv[1], "nom") == 0) {
		printf("%s\n", ptk_nom_affiche(argv[2], argv[3]));
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "masque") == 0) {
		printf("%d\n", ptk_masque(argv[2], argv[3]));
		return 0;
	}
	if (argc != 3)
		return 2;
	if (strcmp(argv[1], "lire") == 0) {
		if (!ptk_identite_lire(argv[2], &id))
			return 1;
		printf("nom=%s\nsignature=%s\nicone=%s\nversion=%s\ntypes=%s\n",
		    id.nom, id.signature, id.icone, id.version, id.types);
		return 0;
	}
	if (strcmp(argv[1], "signature") == 0) {
		if (!ptk_programme(argv[2], out, sizeof(out)))
			return 1;
		printf("%s\n", out);
		return 0;
	}
	if (strcmp(argv[1], "extension") == 0) {
		if (!ptk_programme_pour(argv[2], out, sizeof(out)))
			return 1;
		printf("%s\n", out);
		return 0;
	}
	if (strcmp(argv[1], "resoudre") == 0) {
		printf("%s\n", ptk_resoudre(argv[2], out, sizeof(out)));
		return 0;
	}
	return 2;
}
