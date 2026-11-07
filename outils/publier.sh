#!/bin/sh
# publier.sh : synchronise l'état courant du master dans la branche « publication »
# (un commit à plat, refait à chaque publication) et la pousse sur GitHub.
#
# La branche « publication » est orpheline : elle est recréée à chaque publication
# à partir de l'arbre du master, sans son historique. Depuis le 2026-11-07 les
# assets et les captures publiques SONT dans le dépôt (assets/, systeme/,
# docs/img/public/) — ils restent la propriété d'Apple, usage strictement
# personnel (NOTICE.md) ; les fichiers hors publication — audit/, ressources/,
# archive/ et les wheels — sont exclus par .gitignore et retirés de l'index au
# passage, pour rester privés.
#
# Le master doit être propre (tout est commité) ; on revient dessus à la fin.
set -eu
cd "$(dirname "$0")/.."

git checkout -q master
if [ -n "$(git status --porcelain)" ]; then
	echo "le master a des modifications non commitées : commiter d'abord" >&2
	exit 1
fi
M=$(git rev-parse --short master)

# Une orpheline neuve, à partir de l'arbre du master (et non un checkout de
# l'ancienne « publication », qui remettrait l'ancien instantané en place).
git checkout -q --orphan publication-neuve
# le master suit peut-être des fichiers qui ne partiront pas : retirés de
# l'index, ils restent sur le disque (usage privé, NOTICE.md).
git rm -r --cached -q --ignore-unmatch audit ressources archive || true
git rm -q --cached --ignore-unmatch '*.whl' || true
git add -A
git commit -q -m "Publication depuis le master ($M)"
git branch -D -q publication 2>/dev/null || true
git branch -m publication
git push -f origin publication:main
git log --oneline -1
git checkout -q master
