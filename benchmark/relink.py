"""relink.py <work folder>: point every link in the prefix copies at the data copies.

The Wine game folders in a prefix hold links to the items in ~/Games/<game>/Game Data,
and My Documents is a link to the saves folder. In the copies made by setup.sh, these
links are changed to the copies in <work>/data, so a test run cannot write into ~/Games.
Both Wine user folders get the links: Wine 8 uses the Mac user name, the athei
CrossOver Wine uses "crossover". Other user folders that link to the Mac home folder
(Desktop, Downloads, ...) become empty local folders.
"""
import getpass
import os
import sys

W = os.path.abspath(sys.argv[1])
H = os.path.expanduser("~")
G = H + "/Games"
MAPS = [
    (G + "/Commandos Behind Enemy Lines/Game Data", W + "/data/cmd-Game Data"),
    (G + "/Revenant/Game Data", W + "/data/rev-Game Data"),
    (G + "/Command and Conquer Generals Zero Hour/Game Data", W + "/data/gen/Game Data"),
    (G + "/Command and Conquer Generals Zero Hour/Old Windows Saves", W + "/data/gen/Old Windows Saves"),
]
DOCS = {"cmd": W + "/data/cmd-Game Data/User", "gen": W + "/data/gen/Old Windows Saves", "rev": None}


def links(root):
    for d, dirs, files in os.walk(root):
        for name in dirs + files:
            f = os.path.join(d, name)
            if os.path.islink(f):
                yield f, os.readlink(f)


for p in sorted(os.listdir(W)):
    if not p.startswith("pfx-"):
        continue
    game = p.split("-")[1]
    root = f"{W}/{p}/drive_c"
    n = 0
    for f, t in list(links(root)):
        for a, b in MAPS:
            if t == a or t.startswith(a + "/"):
                os.unlink(f)
                os.symlink(b + t[len(a):], f)
                n += 1
                break
    for user in (getpass.getuser(), "crossover"):
        ud = f"{root}/users/{user}"
        os.makedirs(ud, exist_ok=True)
        for folder in ("Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos"):
            f = f"{ud}/{folder}"
            if os.path.islink(f):
                os.unlink(f)
            if folder == "Documents" and DOCS[game]:
                if os.path.isdir(f) and not os.listdir(f):
                    os.rmdir(f)
                if not os.path.exists(f):
                    os.symlink(DOCS[game], f)
            else:
                os.makedirs(f, exist_ok=True)
        mac_desktop = f"{ud}/Desktop/My Mac Desktop"
        if os.path.islink(mac_desktop):
            os.unlink(mac_desktop)
    # Links into ~/Games that are still left would let a run write there: stop.
    left = [f"{f} -> {t}" for f, t in links(root) if t.startswith(G + "/")]
    if left:
        sys.exit(f"{p}: links into ~/Games are left:\n  " + "\n  ".join(left[:10]))
    print(f"{p}: {n} links point at the data copies")
