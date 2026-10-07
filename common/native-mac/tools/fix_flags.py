#!/usr/bin/env python3
"""Generate instruction_flags.sci entries for SRW, with SRW as the judge.

SRW checks CPU flag use one region (block) at a time. When a block reads a
flag that its predecessor set (for example "jne L" ... "L: sbb ecx, ecx"
reads CF from the cmp before the jne), SRW stops with
"unknown flags on start of region". The fix is two entries:

    loc_<pred>,<flags>,0     the predecessor must keep these flags
    loc_<start>,0,<flags>    the block start gets them from the predecessor

This tool runs SRW in its keep-going mode, reads all such errors, adds the
entries for every predecessor (direct jumps to the block, and the
instruction that falls through into it), and runs SRW again until no
error is left.
"""

import argparse
import re
import subprocess
import sys

sys.path.insert(0, __import__("os").path.dirname(__file__))
from gen_relocs import Image, analyze  # noqa: E402

ERR = re.compile(r"unknown flags on start of region - \d+ - \d+ \(0x([0-9a-f]+)\) - 0x([0-9a-f]+)")


def predecessors(insns, ends, target):
    preds = []
    for va in jumps_to.get(target, []):
        preds.append(va)
    prev = ends.get(target)
    if prev is not None:
        i = insns[prev]
        if i.mnemonic not in ("jmp", "ret", "retf", "int3", "hlt"):
            preds.append(prev)
    return preds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--srw", required=True)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", required=True, help="name of the SRW output file (GAME_LLASM.llasm)")
    ap.add_argument("--sci", default="instruction_flags.sci")
    ap.add_argument("--max", type=int, default=5000)
    ap.add_argument("--strict-data-code", action="store_true",
                    help="the same option as for gen_relocs.py (GEN_RELOCS_OPTIONS in game.conf)")
    args = ap.parse_args()
    import gen_relocs
    gen_relocs.STRICT_DATA_CODE = args.strict_data_code

    img = Image(args.exe)
    st = analyze(img, log=lambda *a: None)
    insns = st["insns"]
    ends = {va + i.size: va for va, i in insns.items()}
    global jumps_to
    jumps_to = {}
    for va, i in insns.items():
        if i.mnemonic.startswith("j") and i.operands and i.operands[0].type == 2:  # imm
            jumps_to.setdefault(i.operands[0].imm & 0xFFFFFFFF, []).append(va)

    entries = {}
    try:
        for line in open(args.sci):
            line = line.split(";")[0].strip()
            if line:
                a, s, c = line.split(",")
                entries[int(a[4:], 16)] = [int(s, 0), int(c, 0)]
    except FileNotFoundError:
        pass

    def write():
        with open(args.sci, "w") as fh:
            for a in sorted(entries):
                s, c = entries[a]
                fh.write("loc_%X,0x%02x,0x%02x\n" % (a, s, c))

    env = dict(__import__("os").environ, SRW_KEEP_GOING="1")
    for n in range(args.max):
        r = subprocess.run([args.srw, args.exe, args.out], capture_output=True, text=True, env=env)
        found = [(int(a, 16), int(f, 16)) for a, f in ERR.findall(r.stderr)]
        if not found:
            print("rounds: %d, entries: %d" % (n, len(entries)))
            return
        changed = False
        for target, flags in found:
            preds = predecessors(insns, ends, target)
            if not preds:
                print("no direct predecessor for 0x%x (flags 0x%x); fix by hand" % (target, flags))
                continue
            if target in entries and entries[target][1] & flags == flags:
                print("still failing at 0x%x (flags 0x%x); fix by hand" % (target, flags))
                continue
            entries.setdefault(target, [0, 0])[1] |= flags
            for p in preds:
                entries.setdefault(p, [0, 0])[0] |= flags
            changed = True
        write()
        print("round %d: %d region errors" % (n, len(found)))
        if not changed:
            sys.exit(1)
    print("stopped after %d rounds" % args.max)


if __name__ == "__main__":
    main()
