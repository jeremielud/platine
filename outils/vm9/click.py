#!/usr/bin/env python3
"""Clic/recalage dans la VM Mac OS 9 via le script gui.py existant."""
import os
import subprocess
import sys

VM = os.environ.get("VM", os.path.expanduser(
    "~/Projets/eeepc-openbsd/macos9"))
subprocess.run(["python3", os.path.join(VM, "gui.py"), *sys.argv[1:]],
    cwd=VM, check=False)
