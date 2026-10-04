#!/usr/bin/env python3
"""Function-level map of the game exe, for finding C runtime functions.

For each function (direct call target or ILT thunk target) it lists the size,
the imports it calls, the strings it uses, the instructions SRW cannot
translate, and its callers. Output: build/srw/functions.txt (gitignored).
"""

import collections
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from gen_relocs import Image, analyze  # noqa: E402

UNSUPPORTED = set("""mulsd movlpd addsd subsd mulpd movapd addpd andpd movd pshufd pextrw movq
psrlq xorpd movdqa psllq orpd unpcklpd pinsrw stmxcsr subpd divsd unpckhpd pmovmskb pcmpeqd
comisd psubd sqrtsd pxor fprem fscale cvtsd2si fldpi fpatan frndint fprem1 ldmxcsr ucomisd
shufpd fnclex cpuid cmpltpd cmpnlepd fxam andnpd cmpeqsd cvttsd2si fnstenv fldenv cmpltsd psubq
cvtdq2pd pmaxsw cvtsi2sd cmove f2xm1 movdqu movups movaps""".split())


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "comandos.exe"
    img = Image(exe)
    st = analyze(img, log=lambda *a: None)
    insns = st["insns"]
    iat = {}
    for d in img.pe.DIRECTORY_ENTRY_IMPORT:
        for f in d.imports:
            name = f.name.decode() if f.name else "ord%d" % f.ordinal
            iat[f.address] = d.dll.decode().split(".")[0] + "!" + name

    def resolve_thunk(t):
        if img.is_ilt_thunk(t) and t in insns and insns[t].mnemonic == "jmp":
            return insns[t].operands[0].imm & 0xFFFFFFFF
        return t

    # Function starts: call targets (through thunks) and data code pointers.
    starts = set()
    callers = collections.defaultdict(set)
    for va, i in insns.items():
        if i.mnemonic == "call" and i.operands and i.operands[0].type == 2:
            t = resolve_thunk(i.operands[0].imm & 0xFFFFFFFF)
            starts.add(t)
            callers[t].add(va)
    for fix, tgt in st["relocs"].items():
        if img.in_text(tgt) and st["kinds"][fix].startswith("data"):
            starts.add(resolve_thunk(tgt))
    starts.add(img.entry)
    starts = sorted(s for s in starts if s in insns and not img.is_ilt_thunk(s))

    def func_of(va):
        import bisect
        k = bisect.bisect_right(starts, va) - 1
        return starts[k] if k >= 0 else None

    info = collections.defaultdict(lambda: dict(n=0, imports=set(), strings=set(), bad=collections.Counter()))
    for va, i in sorted(insns.items()):
        if img.is_ilt_thunk(va):
            continue
        f = func_of(va)
        if f is None:
            continue
        d = info[f]
        d["n"] += 1
        if i.mnemonic in UNSUPPORTED:
            d["bad"][i.mnemonic] += 1
        for op in i.operands:
            if op.type == 3 and op.mem.base == 0 and op.mem.index == 0:  # [abs]
                a = op.mem.disp & 0xFFFFFFFF
                if a in iat:
                    d["imports"].add(iat[a])
            if op.type == 2:
                a = op.imm & 0xFFFFFFFF
                sec = img.section_of(a)
                if sec and sec[0] in (".rdata", ".data") and a < sec[1] + sec[3]:
                    raw = img.bytes_at(a, 48).split(b"\0")[0]
                    if len(raw) >= 3 and all(32 <= c < 127 for c in raw):
                        d["strings"].add(raw.decode())

    out = os.path.join(os.path.dirname(os.path.abspath(exe)), "functions.txt")
    with open(out, "w") as fh:
        for f in starts:
            d = info.get(f)
            if not d:
                continue
            cs = sorted(func_of(c) or 0 for c in callers[f])
            fh.write("%08x n=%d callers=%d [%s]\n" % (f, d["n"], len(cs), " ".join("%x" % c for c in cs[:6])))
            if d["imports"]:
                fh.write("    imports: %s\n" % " ".join(sorted(d["imports"])))
            if d["strings"]:
                fh.write("    strings: %s\n" % " | ".join(sorted(d["strings"]))[:300])
            if d["bad"]:
                fh.write("    UNSUPPORTED: %s\n" % dict(d["bad"]))
    print("functions:", len(starts), "->", out)


if __name__ == "__main__":
    main()
