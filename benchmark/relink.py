"""relink.py <work folder>: point every link of the game folder copies at the copies.

setup.sh copies ~/Games/<game> to <work>/<game>. The links in the copies (the Wine
game folders in each prefix, My Documents, the Save links in Settings) still point
into ~/Games/<game>. This script changes each of them to the same place in
<work>/<game>, so a test run cannot write into ~/Games. It stops if a link into
~/Games is left. The app bundles are not changed (their links are relative).
"""
import os
import sys

W = os.path.abspath(sys.argv[1])
G = os.path.expanduser("~/Games")


def links(root):
    for d, dirs, files in os.walk(root):
        dirs[:] = [x for x in dirs if not x.endswith(".app")]
        for name in dirs + files:
            f = os.path.join(d, name)
            if os.path.islink(f):
                yield f, os.readlink(f)


for game in sorted(os.listdir(W)):
    root = os.path.join(W, game)
    if not os.path.isdir(root) or not os.path.isdir(os.path.join(G, game)):
        continue
    n = 0
    for f, t in list(links(root)):
        if t.startswith(G + "/"):
            os.unlink(f)
            os.symlink(W + t[len(G):], f)
            n += 1
    left = [f"{f} -> {t}" for f, t in links(root) if t.startswith(G + "/")]
    if left:
        sys.exit(f"{game}: links into ~/Games are left:\n  " + "\n  ".join(left[:10]))
    print(f"{game}: {n} links point at the copy")
