#!/usr/bin/env python3
"""Write a disassembly listing of a PE file, from the gen_relocs.py analysis.

Only bytes that the recursive-descent analysis decoded as code are shown as
instructions; other bytes in .text are shown as "db" runs. Code starts
(call targets, jump targets, function pointers) get a "loc_XXXXXX:" line,
the same names that SRW gives. Strings in operands that point into the
image are shown as comments.

Run in a game folder. Usage: listing.py [exe] [output]
Defaults: build/srw/<GAME_EXE> and build/listing.txt.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_relocs import Image, analyze  # noqa: E402


def game_conf():
    return dict(l.rstrip("\n").split("=", 1) for l in open("game.conf") if "=" in l and not l.startswith("#"))


def string_at(img, addr):
    off = addr - img.base
    if off < 0 or off >= len(img.mem):
        return None
    end = img.mem.find(b"\0", off, off + 80)
    if end < 0 or end - off < 3:
        return None
    s = img.mem[off:end]
    if all(0x20 <= c < 0x7F or c in (9, 10, 13) for c in s):
        return s.decode("latin-1").replace("\n", "\\n").replace("\r", "\\r")
    return None


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join("build", "srw", game_conf()["GAME_EXE"])
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build", "listing.txt")
    img = Image(exe)
    st = analyze(img, log=lambda *a: None)
    insns, starts, relocs = st["insns"], set(st["code_starts"]), st["relocs"]
    labels = set(starts)
    for va, i in insns.items():
        if i.mnemonic.startswith("j") or i.mnemonic == "call":
            for op in i.operands:
                if op.type == 2:
                    labels.add(op.imm & 0xFFFFFFFF)
    labels |= {t for t in relocs.values() if t in insns}
    text_start, text_end = img.text[1], img.text_end
    with open(out, "w") as fh:
        va = text_start
        while va < text_end:
            if va in insns:
                i = insns[va]
                if va in labels:
                    fh.write("\nloc_%X:\n" % va)
                comment = ""
                for k in range(i.size - 3):
                    t = relocs.get(va + k)
                    if t is not None:
                        s = string_at(img, t)
                        if s:
                            comment = '   ; "%s"' % s
                fh.write("  %08x  %-7s %s%s\n" % (va, i.mnemonic, i.op_str, comment))
                va += i.size
            else:
                run = va
                while run < text_end and run not in insns and run - va < 16:
                    run += 1
                b = img.mem[va - img.base:run - img.base]
                fh.write("  %08x  db      %s\n" % (va, " ".join("%02x" % c for c in b)))
                va = run
    print("wrote %s: %d instructions, %d labels" % (out, len(insns), len(labels)))


if __name__ == "__main__":
    main()
