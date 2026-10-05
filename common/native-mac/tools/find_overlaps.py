#!/usr/bin/env python3
"""List branch targets that are inside another decoded instruction.

Hand-written assembly sometimes jumps into the middle of an instruction
(for example "jne L+1" where L is "66 89 07", mov [edi], ax, and L+1 is
"89 07", mov [edi], eax). SRW cannot translate that; each case needs an
instruction_replacements.sci entry. Also lists 0x402100-style relocation
targets that gen_relocs.py reports as overlaps.

Run in a game folder. Usage: find_overlaps.py [exe]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_relocs import Image, analyze  # noqa: E402

game = dict(l.rstrip("\n").split("=", 1) for l in open("game.conf") if "=" in l and not l.startswith("#"))
exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "srw", game["GAME_EXE"])
img = Image(exe)
st = analyze(img, log=lambda *a: None)
insns, owner = st["insns"], st["owner"]
for va, i in sorted(insns.items()):
    if i.mnemonic.startswith("j") or i.mnemonic in ("call", "loop", "jecxz"):
        for op in i.operands:
            if op.type == 2:
                t = op.imm & 0xFFFFFFFF
                if img.in_text(t) and t not in insns:
                    o = owner.get(t)
                    inner = insns.get(o) if o not in (None, -1) else None
                    print("0x%x %s 0x%x -> inside %s" % (va, i.mnemonic, t,
                          "0x%x %s %s" % (o, inner.mnemonic, inner.op_str) if inner else "data/undecoded"))
# decoded instructions that start inside another decoded instruction
for va, i in sorted(insns.items()):
    for k in range(1, i.size):
        if va + k in insns:
            j = insns[va + k]
            print("0x%x %s %s  contains start 0x%x %s %s" % (va, i.mnemonic, i.op_str, va + k, j.mnemonic, j.op_str))
for va in sorted(set(st["overlaps"])):
    print("overlap 0x%x" % va)
