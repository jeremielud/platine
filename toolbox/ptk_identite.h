/*
 * ptk_identite.h — identité d'un programme Platine, embarquée dans le binaire.
 *
 * Sous Mac OS 9, l'application est le fichier : son code, son icône (ICN#),
 * sa version (vers) et les documents qu'elle ouvre (FREF/BNDL) vivent dans sa
 * ressource, et le Finder les lit pour la dessiner et lui confier ses
 * documents. Platine fait de même avec une section ELF « .platine » que le
 * Finder, le WM et l'Installateur lisent (ptk_identite_lire, ptk.h) :
 *
 *	PTK_IDENTITE("TextEdit", "ttxt", "app-texte", "1.0", "txt md c h");
 *
 * nom        nom affiché, celui du fichier installé dans Applications ;
 * signature  code créateur de 4 caractères : le Finder retrouve le programme
 *            par elle (base de bureau), où qu'il soit rangé ;
 * icône      nom d'icône des assets (app-texte → app-texte-32.argb, -16) ;
 *            « @nom » : l'icône dépend du nom du fichier (tableaux de bord) ;
 * version    affichée par Lire les informations ;
 * types      extensions des documents que le programme ouvre.
 *
 * Header seul : un programme OpenGL (ptk_gl) peut aussi porter son identité.
 */
#ifndef PTK_IDENTITE_H
#define PTK_IDENTITE_H

#define PTK_IDENTITE_MAGIE "PLATINE-IDENTITE-1"

/* version des programmes livrés avec Platine */
#define PTK_VERSION "1.0, Platine 2026"

#define PTK_IDENTITE(nom, signature, icone, version, types)		\
	__attribute__((used, section(".platine")))			\
	static const char ptk_identite_[] = PTK_IDENTITE_MAGIE "\n"	\
	    "nom=" nom "\n"						\
	    "signature=" signature "\n"				\
	    "icone=" icone "\n"					\
	    "version=" version "\n"					\
	    "types=" types "\n"

#endif
