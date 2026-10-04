#!/usr/bin/env python3
"""Turn "guest trace:" lines of build/run.log into code names.

Only stack words that are exactly at a loc_ symbol are printed (return addresses).
Usage: tools/trace.py [build/run.log]
"""
import subprocess
import sys

syms = {}
for line in subprocess.run(["nm", "build/Commandos"], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        syms[int(p[0], 16)] = p[2].lstrip("_")
for line in open(sys.argv[1] if len(sys.argv) > 1 else "build/run.log", errors="replace"):
    if line.startswith("guest trace:"):
        names = [syms[int(w, 16)] for w in line.split()[2:] if int(w, 16) in syms]
        print(" <- ".join(names))
