#!/usr/bin/env python3
"""Rebuild the relocation table of a PE file whose .reloc section was stripped.

SRW needs to know which 32-bit values in the executable are addresses.
This tool finds them:

1. Recursive-descent disassembly of .text from the entry point. Direct call
   and jump targets, and code addresses used as immediates (push offset f),
   are followed.
2. Switch jump tables (jmp [reg*4+table]) are read with the bound from the
   cmp/ja before them. Their bytes are data, not code. When a table overlaps
   code that was decoded by mistake, the analysis starts again with the
   table marked as data.
3. Each instruction operand (4-byte immediate or displacement) that points
   into the image is a relocation.
4. Aligned dwords in the data sections that point into the image are
   relocations, with checks: a dword that looks like a short ASCII string
   ("RES\\0") is accepted only if its target is a known code or data label.
   A data pointer into .text adds new code only if it is part of a run of
   code pointers (vtables, function tables) or the target looks like a
   function start.

Output: relocations.csv ("fixup,target," per line, the SRW format) and a
report with the values that need a check by hand.
"""

import argparse
import collections
import os
import struct

import capstone
import pefile
from capstone import x86_const as X

PRINTABLE = set(range(0x20, 0x7F)) | {0x09, 0x0A, 0x0D}
PADDING = (0xCC, 0x90)
REL_BRANCH_OPCODES = {0xE8, 0xE9, 0xEB} | set(range(0x70, 0x80))


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.mem = bytes(self.pe.get_memory_mapped_image())
        self.end = self.base + len(self.mem)
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode()
            start = self.base + s.VirtualAddress
            vsize = max(s.Misc_VirtualSize, s.SizeOfRawData)
            self.sections.append((name, start, start + vsize, s.SizeOfRawData))
        self.text = next(s for s in self.sections if s[0] == ".text")
        self.text_end = self.text[1] + self.text[3]
        self.entry = self.base + self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.find_ilt()
        self.iat = set()
        for imp in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for f in imp.imports:
                self.iat.add(f.address)

    def section_of(self, va):
        for s in self.sections:
            if s[1] <= va < s[2]:
                return s
        return None

    def in_image(self, va):
        return self.base <= va < self.end

    def in_text(self, va):
        return self.text[1] <= va < self.text_end

    def byte(self, va):
        return self.mem[va - self.base]

    def dword(self, va):
        return struct.unpack_from("<I", self.mem, va - self.base)[0]

    def bytes_at(self, va, n):
        o = va - self.base
        return self.mem[o:o + n]

    def looks_like_string(self, va):
        """True if the 4 bytes at va look like the end of an ASCII string."""
        b = self.bytes_at(va, 4)
        if b[3] != 0:
            return False
        if all(x in PRINTABLE for x in b[:3]):
            return True
        # UTF-16 text: "w\0b\0"
        return b[1] == 0 and b[0] in PRINTABLE and b[2] in PRINTABLE

    def find_ilt(self):
        """Incremental-link thunk table at the start of .text: jmp rel32 x N."""
        lo = self.text[1]
        while self.byte(lo) == 0xCC:
            lo += 1
        a = lo
        while self.byte(a) == 0xE9:
            a += 5
        self.ilt = (lo, a)

    def is_ilt_thunk(self, va):
        lo, hi = self.ilt
        return lo <= va < hi and (va - lo) % 5 == 0

    def looks_like_function_start(self, va):
        if not self.in_text(va):
            return False
        if self.is_ilt_thunk(va):
            return True
        prev = self.byte(va - 1) if va > self.text[1] else 0xCC
        # Functions follow padding, a ret, or a jmp (incremental link thunks).
        boundary = prev in (0xCC, 0x90, 0xC3) or self.bytes_at(va - 3, 1) == b"\xc2"
        if not boundary:
            return False
        b = self.bytes_at(va, 3)
        return b[0] not in PADDING and b != b"\x00\x00\x00"


def is_rel_branch(insn):
    b = insn.bytes
    if b[0] in REL_BRANCH_OPCODES:
        return True
    return b[0] == 0x0F and 0x80 <= b[1] <= 0x8F


# Jump tables that the analysis cannot size (MSVC memcpy/memmove use
# negative indexes and "and reg, 3" far from the jmp). One line per jmp:
#   <table displacement> <first index> <entries>
MANUAL_TABLES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "srw", "jump_tables.txt")


def load_manual_tables(path=MANUAL_TABLES_FILE):
    tables = {}
    if os.path.exists(path):
        for line in open(path):
            line = line.split("#")[0].split()
            if len(line) == 3:
                tables[int(line[0], 16)] = (int(line[1]), int(line[2]))
    return tables


MANUAL_TABLES = load_manual_tables()


def analyze(img, extra_code=(), log=print):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    data_regions = {}    # va -> length, bytes inside .text that are data
    restarts = 0
    while True:
        st = _analyze_once(img, md, extra_code, data_regions)
        conflicts = st["conflicts"]
        if not conflicts:
            st["restarts"] = restarts
            return st
        restarts += 1
        before = len(data_regions)
        data_regions.update(conflicts)
        log("restart %d: %d jump tables overlapped code" % (restarts, len(conflicts)))
        if len(data_regions) == before or restarts > 20:
            st["restarts"] = restarts
            return st


def _analyze_once(img, md, extra_code, data_regions):
    insns = {}
    owner = {}
    data_bytes = set()
    for va, n in data_regions.items():
        data_bytes.update(range(va, va + n))
    relocs = {}
    kinds = {}
    doubtful = []
    jump_tables = {}
    displaced = {}
    conflicts = {}
    roots = collections.deque([img.entry] + list(extra_code))
    code_starts = set(roots)
    code_labels = set()      # targets referenced from code operands
    overlaps = []

    def add_reloc(fix, tgt, kind):
        if fix not in relocs:
            relocs[fix] = tgt
            kinds[fix] = kind

    def add_root(va):
        if img.in_text(va) and va not in code_starts and va not in data_bytes:
            code_starts.add(va)
            roots.append(va)

    def handle_operands(insn):
        for op in insn.operands:
            if op.type == X.X86_OP_IMM and insn.imm_size == 4 and insn.imm_offset:
                if is_rel_branch(insn):
                    continue
                v = op.imm & 0xFFFFFFFF
                if not img.in_image(v):
                    continue
                fix = insn.address + insn.imm_offset
                if insn.mnemonic in ("test", "and", "or", "xor") and not img.in_text(v):
                    # Bit masks, not addresses.
                    doubtful.append((fix, v, "skipped mask in '%s %s'" % (insn.mnemonic, insn.op_str)))
                    continue
                add_reloc(fix, v, "imm:" + insn.mnemonic)
                code_labels.add(v)
                if img.in_text(v):
                    add_root(v)
                if v & 0xFFFF == 0:
                    doubtful.append((fix, v, "round immediate in '%s %s'" % (insn.mnemonic, insn.op_str)))
            elif op.type == X.X86_OP_MEM and insn.disp_size == 4 and insn.disp_offset:
                v = op.mem.disp & 0xFFFFFFFF
                if img.in_image(v):
                    add_reloc(insn.address + insn.disp_offset, v, "disp:" + insn.mnemonic)
                    code_labels.add(v)

    def table_bound(history, index_reg):
        """Entries of a jump table, from 'cmp reg, N; ja' before the jmp."""
        reg = index_reg
        byte_table = None
        for insn in reversed(history[-12:]):
            ops = insn.operands
            if (insn.mnemonic == "movzx" and len(ops) == 2 and ops[0].type == X.X86_OP_REG
                    and ops[0].reg == reg and ops[1].type == X.X86_OP_MEM and ops[1].size == 1
                    and ops[1].mem.index == 0 and ops[1].mem.base != 0):
                # movzx reg, byte [base + idxtable]: index through a byte table
                byte_table = ops[1].mem.disp & 0xFFFFFFFF
                reg = ops[1].mem.base
                continue
            if (insn.mnemonic == "and" and len(ops) == 2 and ops[0].type == X.X86_OP_REG
                    and ops[1].type == X.X86_OP_IMM and _same_reg(ops[0].reg, reg)
                    and byte_table is None):
                m = ops[1].imm & 0xFFFFFFFF
                if m < 256 and (m & (m + 1)) == 0:
                    return m + 1, None
            if (insn.mnemonic == "cmp" and len(ops) == 2 and ops[0].type == X.X86_OP_REG
                    and ops[1].type == X.X86_OP_IMM):
                if _same_reg(ops[0].reg, reg):
                    n = (ops[1].imm & 0xFFFFFFFF) + 1
                    if n > 4096:
                        return None, None
                    if byte_table is not None:
                        if not img.in_image(byte_table):
                            return None, None
                        idx = img.bytes_at(byte_table, n)
                        return max(idx) + 1, (byte_table, n)
                    return n, None
        return None, None

    def handle_indirect_jmp(insn, history):
        op = insn.operands[0]
        if op.type != X.X86_OP_MEM:
            return
        m = op.mem
        if m.base != 0 or m.index == 0 or m.scale != 4:
            return
        table = m.disp & 0xFFFFFFFF
        if img.section_of(table) is None:
            return
        if table in MANUAL_TABLES:
            first, n = MANUAL_TABLES[table]
            start = table + 4 * first
            targets = [img.dword(start + 4 * i) for i in range(n)]
            jump_tables[start] = targets
            for b in range(start, start + 4 * n):
                if b in owner and owner[b] != -1:
                    conflicts[start] = 4 * n
                    break
            for b in range(start, start + 4 * n):
                owner.setdefault(b, -1)
                data_bytes.add(b)
            for i, t in enumerate(targets):
                add_reloc(start + 4 * i, t, "jumptable")
                code_labels.add(t)
                add_root(t)
            return
        count, byte_table = table_bound(history, m.index)
        targets = []
        lead = 0
        if count is not None:
            for i in range(count):
                v = img.dword(table + 4 * i)
                if not img.in_text(v):
                    if not targets:
                        lead += 1      # unused leading entry (bytes of code)
                        continue
                    doubtful.append((table + 4 * i, v, "jump table entry outside .text"))
                    break
                targets.append(v)
        else:
            a = table
            while len(targets) < 1024:
                if a in owner or (targets and a in jump_tables):
                    break
                v = img.dword(a)
                if not img.in_text(v):
                    break
                targets.append(v)
                a += 4
            doubtful.append((table, len(targets), "jump table without bound, %d entries" % len(targets)))
        # MSVC memcpy: "and eax, 3; jmp [eax*4+table]" where entry 0 is never
        # used and its bytes are the next instruction. Skip leading entries
        # that overlap decoded code; SRW needs a displaced label for them.
        skip = lead
        targets = [img.dword(table + 4 * i) for i in range(lead)] + targets
        while skip < len(targets) and any(
                owner.get(b, -1) not in (-1,) for b in range(table + 4 * skip, table + 4 * skip + 4)):
            skip += 1
        if skip:
            displaced[table] = 4 * skip
            doubtful.append((table, skip, "jump table starts inside code, %d entries skipped" % skip))
            table += 4 * skip
        targets = targets[skip:]
        jump_tables[table] = targets
        regions = [(table, 4 * len(targets))]
        if byte_table is not None:
            regions.append(byte_table)
        for va, n in regions:
            if img.in_text(va):
                for b in range(va, va + n):
                    if b in owner and owner[b] != -1:
                        conflicts[va] = n
                        break
                for b in range(va, va + n):
                    owner.setdefault(b, -1)
                    data_bytes.add(b)
        for i, t in enumerate(targets):
            add_reloc(table + 4 * i, t, "jumptable")
            code_labels.add(t)
            add_root(t)

    def decode_block(start):
        work = [start]
        while work:
            va = work.pop()
            history = []
            while True:
                if va in insns or not img.in_text(va):
                    break
                if va in owner or va in data_bytes:
                    if owner.get(va) != -1:
                        overlaps.append(va)
                    break
                try:
                    insn = next(md.disasm(img.bytes_at(va, 16), va, 1))
                except StopIteration:
                    overlaps.append(va)
                    break
                if any(b in data_bytes for b in range(va, va + insn.size)):
                    overlaps.append(va)
                    break
                insns[va] = insn
                history.append(insn)
                for b in range(va, va + insn.size):
                    owner[b] = va
                handle_operands(insn)
                g = insn.groups
                mnem = insn.mnemonic
                if X.X86_GRP_RET in g or mnem in ("hlt", "int3", "ud2"):
                    break
                if X.X86_GRP_JUMP in g:
                    op = insn.operands[0] if insn.operands else None
                    if op is not None and op.type == X.X86_OP_IMM:
                        work.append(op.imm & 0xFFFFFFFF)
                        if mnem == "jmp":
                            break
                    elif mnem == "jmp":
                        handle_indirect_jmp(insn, history)
                        break
                elif X.X86_GRP_CALL in g:
                    op = insn.operands[0] if insn.operands else None
                    if op is not None and op.type == X.X86_OP_IMM:
                        add_root(op.imm & 0xFFFFFFFF)
                va += insn.size

    def scan_data():
        """Accept data dwords as pointers. Returns True if new code was added."""
        added = False
        for name, start, end, raw in img.sections:
            if name in (".text", ".rsrc"):
                continue
            stop = start + raw
            a = start
            run = []     # consecutive dwords pointing into .text
            while a + 4 <= stop:
                v = img.dword(a)
                ok_text = img.in_text(v)
                if ok_text:
                    run.append(a)
                else:
                    flush_run(run)
                    run = []
                    if (a not in relocs and img.in_image(v) and v != img.base
                            and img.section_of(v)[0] != ".rsrc"
                            if img.section_of(v) else False):
                        stringy = img.looks_like_string(a)
                        if not stringy or v in code_labels:
                            add_reloc(a, v, "data:" + name)
                        else:
                            doubtful.append((a, v, "string-like data dword, not used (" + name + ")"))
                a += 4
            added |= flush_run(run)
        return added

    def flush_run(run):
        added = False
        if not run:
            return False
        for a in run:
            if a in relocs:
                continue
            v = img.dword(a)
            stringy = img.looks_like_string(a)
            known = v in insns and not stringy
            if known:
                add_reloc(a, v, "data:codeptr")
                continue
            if stringy and img.is_ilt_thunk(v):
                doubtful.append((a, v, "string-like pointer to a link thunk, USED"))
            elif stringy and img.looks_like_function_start(v) and img.byte(a - 1) not in PRINTABLE:
                # Static initializer tables and vtables hold direct pointers
                # whose bytes can look like text ("0zm\0").
                doubtful.append((a, v, "string-like pointer to a function start, USED"))
            elif stringy:
                doubtful.append((a, v, "string-like code pointer, not used"))
                continue
            solid = sum(1 for x in run if not img.looks_like_string(x))
            if solid >= 2 or img.looks_like_function_start(v):
                if v in data_bytes or (v in owner and owner[v] != v):
                    doubtful.append((a, v, "data pointer into the middle of code, not used"))
                    continue
                add_reloc(a, v, "data:codeptr")
                if v not in code_starts:
                    add_root(v)
                    added = True
            else:
                doubtful.append((a, v, "lone code pointer to non-function start, not used"))
        return added

    def scan_eh():
        """MSVC C++ EH: FuncInfo (magic 0x1993052x) and the tables it points to."""
        for name, start, end, raw in img.sections:
            if name not in (".rdata", ".data"):
                continue
            a = start
            while a + 28 <= start + raw:
                magic = img.dword(a)
                if 0x19930520 <= magic <= 0x19930522:
                    parse_funcinfo(a)
                a += 4

    def ptr_ok(v):
        return v == 0 or (img.in_image(v) and not img.in_text(v))

    def force_code(fix, tgt, kind):
        add_reloc(fix, tgt, kind)
        code_labels.add(tgt)
        add_root(tgt)

    def parse_funcinfo(fi):
        max_state, unwind, ntry, trymap, nip, ipmap = [img.dword(fi + 4 * i) for i in range(1, 7)]
        if max_state > 100000 or ntry > 10000 or not ptr_ok(unwind) or not ptr_ok(trymap) or not ptr_ok(ipmap):
            return
        if max_state and not unwind:
            return
        eh_funcinfos.add(fi)
        for off, v in ((8, unwind), (16, trymap), (24, ipmap)):
            if v:
                add_reloc(fi + off, v, "eh:funcinfo")
        if img.dword(fi) >= 0x19930521:
            es = img.dword(fi + 28)
            if es and ptr_ok(es):
                add_reloc(fi + 28, es, "eh:funcinfo")
        for i in range(max_state):
            e = unwind + 8 * i
            act = img.dword(e + 4)
            if act:
                if img.in_text(act):
                    force_code(e + 4, act, "eh:unwind")
                else:
                    doubtful.append((e + 4, act, "EH unwind action outside .text"))
        for i in range(ntry):
            t = trymap + 20 * i
            ncatch, harr = img.dword(t + 12), img.dword(t + 16)
            if harr:
                add_reloc(t + 16, harr, "eh:trymap")
            for j in range(min(ncatch, 1000)):
                h = harr + 16 * j
                ptype, handler = img.dword(h + 4), img.dword(h + 12)
                if ptype:
                    add_reloc(h + 4, ptype, "eh:type")
                if handler:
                    force_code(h + 12, handler, "eh:catch")

    eh_funcinfos = set()
    scan_eh()
    while True:
        while roots:
            decode_block(roots.popleft())
        if not scan_data() and not roots:
            break

    for fix, tgt in relocs.items():
        if tgt == img.base:
            doubtful.append((fix, tgt, "points to ImageBase (" + kinds[fix] + ")"))
        if img.in_text(tgt) and tgt in owner and owner[tgt] not in (tgt, -1):
            doubtful.append((fix, tgt, "target inside an instruction (" + kinds[fix] + ")"))

    return dict(insns=insns, owner=owner, relocs=relocs, kinds=kinds,
                doubtful=doubtful, jump_tables=jump_tables, code_starts=code_starts,
                overlaps=overlaps, conflicts=conflicts, data_bytes=data_bytes,
                eh_funcinfos=eh_funcinfos, displaced=displaced)


def _same_reg(a, b):
    groups = [
        {X.X86_REG_EAX, X.X86_REG_AX, X.X86_REG_AL},
        {X.X86_REG_EBX, X.X86_REG_BX, X.X86_REG_BL},
        {X.X86_REG_ECX, X.X86_REG_CX, X.X86_REG_CL},
        {X.X86_REG_EDX, X.X86_REG_DX, X.X86_REG_DL},
        {X.X86_REG_ESI, X.X86_REG_SI},
        {X.X86_REG_EDI, X.X86_REG_DI},
        {X.X86_REG_EBP, X.X86_REG_BP},
    ]
    if a == b:
        return True
    return any(a in g and b in g for g in groups)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("-o", "--out", default="relocations.csv")
    ap.add_argument("-r", "--report", default="relocations-report.txt")
    ap.add_argument("--extra-code", action="append", default=[],
                    help="extra code start address (hex), may repeat")
    args = ap.parse_args()
    img = Image(args.exe)
    st = analyze(img, [int(x, 16) for x in args.extra_code])
    relocs, kinds = st["relocs"], st["kinds"]

    # SRW accepts only targets inside a section. Values that point at the
    # PE header (ImageBase) stay plain numbers; they are in the report.
    first = min(sec[1] for sec in img.sections)
    out = sorted((f, t) for f, t in relocs.items() if f not in img.iat and t >= first)
    with open(args.out, "w") as fh:
        for f, t in out:
            fh.write("0x%x,0x%x,\n" % (f, t))

    # Code addresses used as values (push offset f, mov eax, offset f,
    # function tables in .text). SRW treats a single fixup from code to code
    # as data unless the target is listed in fixup_interpret_as_code.sci.
    with open(os.path.join(os.path.dirname(os.path.abspath(args.out)), "fixup_interpret_as_code.sci"), "w") as fh:
        targets = sorted({t for f, t in out
                          if img.in_text(f) and img.in_text(t) and t in st["insns"]
                          and not kinds[f].startswith("jumptable")})
        for t in targets:
            fh.write("loc_%X\n" % t)

    # Labels inside an instruction (skipped jump table entries).
    with open(os.path.join(os.path.dirname(os.path.abspath(args.out)), "displaced_labels.sci"), "w") as fh:
        for va, n in sorted(st["displaced"].items()):
            fh.write("loc_%X,%d\n" % (va, n))

    covered = sum(i.size for i in st["insns"].values())
    raw = img.text[3]
    with open(args.report, "w") as fh:
        fh.write("restarts: %d\n" % st["restarts"])
        fh.write("instructions: %d, code bytes: %d of %d raw .text (%.1f%%)\n"
                 % (len(st["insns"]), covered, raw, 100.0 * covered / raw))
        fh.write("code starts: %d\n" % len(st["code_starts"]))
        fh.write("jump tables: %d\n" % len(st["jump_tables"]))
        fh.write("EH FuncInfo: %d\n" % len(st["eh_funcinfos"]))
        fh.write("relocations: %d\n" % len(out))
        by = collections.Counter(k.split(":")[0] for k in kinds.values())
        fh.write("by kind: %s\n" % dict(by))
        fh.write("overlaps/undecodable: %d\n" % len(set(st["overlaps"])))
        fh.write("\n# doubtful (check by hand)\n")
        for fix, tgt, why in sorted(set(st["doubtful"])):
            fh.write("0x%x -> 0x%x  %s\n" % (fix, tgt, why))
        fh.write("\n# overlaps\n")
        for va in sorted(set(st["overlaps"])):
            fh.write("0x%x\n" % va)
    print(open(args.report).read().split("\n# doubtful")[0])


if __name__ == "__main__":
    main()
