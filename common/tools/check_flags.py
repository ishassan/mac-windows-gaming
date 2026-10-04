#!/usr/bin/env python3
"""Find code that reads CPU flags that SRW did not compute (stale flags).

SRW joins "cmp a, b" (or "test") with the jump after it into one compare
when it thinks that no later code needs the flags; eflags is then not
written. If the jump target starts with an instruction that reads a flag
(jae, jge, jz, sbb, ...), that instruction reads old eflags, and the game
takes a wrong branch at random. SRW does not report this.

Example (Commandos, 0x60F2E4): "cmp edi, esi; jl 60F29A" at the end of a
loop, and "jae" at 60F29A. The game then compared a NULL string and
crashed after mission 1.

Fix: entries in the game's srw/llasm/instruction_flags.sci, as for
fix_flags.py: "loc_<jump>,<flags>,0x00" for each jump (or fall-through
instruction) into the target, and "loc_<target>,0x00,<flags>". Flag bits
(SRW udis86_dep.h): CF 0x01, PF 0x02, AF 0x04, ZF 0x08, SF 0x10, OF 0x20.

The check looks only at the first instruction of each target, and only at
predecessors in the same SRW procedure. It is not a full proof.

Usage: check_flags.py build/srw/seg01_code.llinc
Exit status 1 when it finds a site.
"""
import re, sys
lines = open(sys.argv[1]).read().split("\n")
READERS = re.compile(r"^;(j(?!mp)[a-z]+|sbb|adc|set[a-z]+|cmov[a-z]+|pushf|rcl|rcr|lahf)\b")
SETTERS = re.compile(r"^;(cmp|test|add|sub|and|or|xor|inc|dec|neg|shl|shr|sar|sal|rol|ror|imul|mul|bt|bsf|bsr|sbb|adc|cmpxchg|scas|cmps)\b")
procs, order, cur = {}, [], None
for i, l in enumerate(lines):
    m = re.match(r"^proc (loc_[0-9A-F]+)", l)
    if m: cur = m.group(1); procs[cur] = [i, None]; order.append(cur); continue
    if l.startswith("endp") and cur: procs[cur][1] = i; cur = None
def first_x86(name):
    s, e = procs[name]
    for l in lines[s+1:e]:
        if l.startswith(";") and not l.startswith("; "): return l
    return None
readers = {n for n in order if first_x86(n) and READERS.match(first_x86(n))}
nexts = {order[k]: order[k+1] for k in range(len(order)-1)}
bad = []
for n in order:
    s, e = procs[n]
    for i in range(s+1, e):
        m = re.match(r"^(ctcall\w*|tcall) .*?(loc_[0-9A-F]+)\s*$", lines[i])
        if not m or m.group(2) not in readers: continue
        # walk back: last eflags write vs last x86 flag setter
        jump = None
        for j in range(i-1, s, -1):
            l = lines[j]
            if l.startswith(";") and not l.startswith("; ") and jump is None:
                jump = l[1:]
            if re.match(r"^\s*\w+ eflags,", l) or "eflags" in l.split(",")[0]:
                break
            if SETTERS.match(l):
                bad.append((n, jump, m.group(2), first_x86(m.group(2))[1:], l[1:])); break
for b in bad: print("%s (%s) -> %s (%s); setter: %s" % b)
print("stale flags: %d sites" % len(bad), file=sys.stderr)
sys.exit(1 if bad else 0)
