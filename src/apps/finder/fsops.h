/*
 * Opérations récursives sur les fichiers du Finder.
 */
#ifndef FSOPS_H
#define FSOPS_H

/* La destination est créée sans écrasement et ne doit pas déjà exister. */
int fsops_copy_tree(const char *src, const char *dst);
int fsops_move_tree(const char *src, const char *dst);

#endif
