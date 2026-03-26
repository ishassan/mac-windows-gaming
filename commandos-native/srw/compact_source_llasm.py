#!/usr/bin/env python3
"""Make the SRW data files smaller: repeated "db" lines become "db ... dup N".

Based on compact_source_llasm.py from M-HT/SR (MIT license).
Run in the folder with the SRW output (seg*_data.llinc).
"""

import glob
import os


def compact(path):
    tmp = path + "tmp"
    with open(path, "rt") as fin, open(tmp, "wt") as fout:
        num = 0
        rep = ""

        def flush():
            if num == 1:
                fout.write(rep)
            elif rep.startswith("dskip"):
                fout.write("dskip " + str(num) + "\n")
            elif num == 2:
                fout.write(rep)
                fout.write(rep)
            else:
                fout.write(rep.rstrip() + " dup " + str(num) + rep[len(rep.rstrip()):])

        for line in fin:
            if num != 0:
                if line == rep:
                    num += 1
                    continue
                flush()
                num = 0
            if line.startswith("db ") or line.strip() == "dskip 1":
                num = 1
                rep = line
            else:
                fout.write(line)
        if num != 0:
            flush()
    os.replace(tmp, path)


for f in sorted(glob.glob("seg*_data.llinc")):
    compact(f)
