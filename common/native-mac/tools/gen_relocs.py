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

# Imports that never return (the same list as SR_is_noret_import in SRW):
# the bytes after a call to one of them can be padding or data.
NORET_IMPORTS = {
    "_CxxThrowException", "exit", "_exit", "abort", "longjmp", "_amsg_exit",
    "ExitProcess", "ExitThread", "_endthread", "_endthreadex",
    "FatalAppExitA", "FatalExit",
}
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
        self.iat_names = {}
        for imp in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for f in imp.imports:
                self.iat.add(f.address)
                if f.name:
                    self.iat_names[f.address] = f.name.decode()

    def noret_import_call(self, va):
        """True if the 6 bytes at va are "call/jmp dword ptr [IAT]" of an
        import that never returns."""
        b = self.bytes_at(va, 6)
        if b[0] != 0xFF or b[1] not in (0x15, 0x25):
            return False
        return self.iat_names.get(struct.unpack_from("<I", b, 2)[0]) in NORET_IMPORTS

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

    def has_text(self, va):
        """True if any of the 4 bytes at va is a printable character."""
        return any(x in PRINTABLE and x not in (0x09, 0x0A, 0x0D) for x in self.bytes_at(va, 4))

    def pointer_not_string(self, va):
        """A string-like dword that is still a pointer: no text in the dwords
        before and after it (a 3-letter string alone between zeros is rare;
        the MSVC 6 CRT lock table has the pointer 0x00676f68, "hog"), or one of
        a run of dwords that all point into initialized sections (EH and RTTI
        tables in .rdata: 0x005c3070, 0x005c3050). Both cases need a 4-byte
        aligned target."""
        if self.dword(va) % 4:
            return False     # "ESZ\0" in a CRT locale table: 0x005a5345 is a string
        if not (self.has_text(va - 4) or self.has_text(va + 4)):
            return True
        def init_ptr(a):
            v = self.dword(a)
            sec = self.section_of(v)
            return sec is not None and sec[0] not in (".rsrc",) and v < sec[1] + sec[3]
        return init_ptr(va) and (init_ptr(va - 4) or init_ptr(va + 4))

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

    def looks_like_function_start(self, va, insns=None, owner=None, noret=()):
        if not self.in_text(va):
            return False
        if insns is not None and owner is not None:
            prev = owner.get(va - 1)
            if prev == -1 and owner.get(va) is None:
                # after a jump table: switch tables follow the function's code
                b = self.bytes_at(va, 3)
                if b[0] not in PADDING and b != b"\x00\x00\x00":
                    return True
            if prev not in (None, -1) and prev in insns:
                i = insns[prev]
                ends_flow = i.mnemonic in ("ret", "jmp") or i.mnemonic.startswith("ret")
                if i.mnemonic == "call":
                    # after a call that never returns ("throw" at the end of a function)
                    op = i.operands[0] if i.operands else None
                    if op is not None and op.type == X.X86_OP_IMM and (op.imm & 0xFFFFFFFF) in noret:
                        ends_flow = True
                    elif self.noret_import_call(i.address):
                        ends_flow = True
                if i.address + i.size == va and ends_flow:
                    b = self.bytes_at(va, 3)
                    if b[0] not in PADDING and b != b"\x00\x00\x00":
                        return True
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
# The file is the game's srw/jump_tables.txt; run-srw.sh copies it into the
# work folder. JUMP_TABLES=<file> gives another file.
MANUAL_TABLES_FILE = os.environ.get("JUMP_TABLES", "jump_tables.txt")


def load_manual_tables(path=MANUAL_TABLES_FILE):
    tables = {}
    if os.path.exists(path):
        for line in open(path):
            line = line.split("#")[0].split()
            if len(line) == 3:
                tables[int(line[0], 16)] = (int(line[1]), int(line[2]))
    return tables


MANUAL_TABLES = load_manual_tables()

# Data inside .text that the analysis must not decode as code (for example
# DirectInput data formats linked from dinput.lib). One line per block:
#   <start> <length>      (hex start, length in hex with 0x or decimal)
# The file is the game's srw/data_in_text.txt; run-srw.sh copies it into the
# work folder. DATA_IN_TEXT=<file> gives another file. Aligned dwords in these
# blocks that point into the image become relocations.
DATA_IN_TEXT_FILE = os.environ.get("DATA_IN_TEXT", "data_in_text.txt")


def load_data_in_text(path=DATA_IN_TEXT_FILE):
    blocks = {}
    if os.path.exists(path):
        for line in open(path):
            line = line.split("#")[0].split()
            if len(line) == 2:
                blocks[int(line[0], 16)] = int(line[1], 0)
    return blocks


DATA_IN_TEXT = load_data_in_text()

# Constants that look like addresses (for example "push 0x600000", two flag
# bits, which is also an address in .data). One fixup address (hex) per line:
# the address of the 4 bytes of the value, not of the instruction.
# The file is the game's srw/not_relocations.txt; run-srw.sh copies it into
# the work folder. NOT_RELOCATIONS=<file> gives another file.
NOT_RELOCS_FILE = os.environ.get("NOT_RELOCATIONS", "not_relocations.txt")


def load_not_relocs(path=NOT_RELOCS_FILE):
    fixes = set()
    if os.path.exists(path):
        for line in open(path):
            line = line.split("#")[0].split()
            if line:
                fixes.add(int(line[0], 16))
    return fixes


NOT_RELOCS = load_not_relocs()

# Strict rule for code pointers in data (--strict-data-code, for big exes
# such as Generals): a dword in data that points into .text adds new code
# only if its target looks like a function start. In a big .text, many
# plain numbers (two 16-bit values such as 0x00450008) are also in the .text
# range, and runs of them made code at false addresses.
STRICT_DATA_CODE = False



def analyze(img, extra_code=(), log=print):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    data_regions = dict(DATA_IN_TEXT)    # va -> length, bytes inside .text that are data
    rejected = set()                     # guessed code starts that made overlaps
    restarts = 0
    while True:
        st = _analyze_once(img, md, extra_code, data_regions, rejected)
        conflicts = st["conflicts"]
        bad = guessed_roots_in_overlaps(st, img, md) if STRICT_DATA_CODE else set()
        bad -= rejected
        if not conflicts and not bad:
            st["restarts"] = restarts
            st["rejected"] = rejected
            return st
        for va, ca, cb in st.get("overlap_log", ()):
            log("  overlap 0x%x: decode from %s, bytes owned by %s" % (va, ca, cb))
        restarts += 1
        before = (len(data_regions), len(rejected))
        data_regions.update(conflicts)
        rejected |= bad
        log("restart %d: %d jump tables overlapped code, %d guessed code starts rejected"
            % (restarts, len(conflicts), len(bad)))
        if (len(data_regions), len(rejected)) == before or restarts > 20:
            st["restarts"] = restarts
            st["rejected"] = rejected
            return st


def skips_over(img, md, lo, hi):
    """True if a straight decode from lo passes hi inside an instruction."""
    va = lo
    while va < hi:
        try:
            insn = next(md.disasm(img.bytes_at(va, 16), va, 1))
        except StopIteration:
            return False
        va += insn.size
    return va != hi


def guessed_roots_in_overlaps(st, img, md):
    """The guessed code starts that made overlaps, to reject on the next pass.

    A guessed start is a data pointer or a code address used as an
    immediate value (not a call or jump target). At an overlap, two decodes
    disagree: the one that ran into the bytes and the one that owns them.
    Each has a root (the start of its decode). Only guessed roots can be
    wrong here. Sure cases first: one guessed root, or two where one is
    inside an instruction of the other. Only when no overlap has a sure
    case: the guessed root with fewer references, else both."""
    src = st["root_src"]
    insroot = st["insroot"]
    owner = st["owner"]
    refs = collections.Counter(st["relocs"].values())

    def guessed(r):
        return r is not None and src.get(r, ("root", None))[0] in ("data", "imm")

    def chain(r):
        out = []
        seen = set()
        while r is not None and r not in seen:
            seen.add(r)
            kind, parent = src.get(r, ("root", None))
            if kind in ("data", "imm"):
                out.append(r)
            r = parent
        return out

    sure, unsure = set(), []
    for va, root in st["overlap_src"]:
        o = owner.get(va)
        other = insroot.get(o) if o not in (None, -1) else None
        st.setdefault("overlap_log", []).append(
            (va, [hex(x) for x in chain(root)], [hex(x) for x in chain(other)] if other else []))
        cand = [r for r in (root, other) if guessed(r)]
        if va == root and guessed(root):
            # The guessed start itself is inside an instruction of a decode
            # that came first.
            sure.add(root)
        elif len(cand) == 1:
            sure.add(cand[0])
        elif len(cand) == 2 and cand[0] != cand[1]:
            lo, hi = sorted(cand)
            if hi - lo < 256 and skips_over(img, md, lo, hi):
                sure.add(hi)
            else:
                unsure.append(cand)
        elif len(cand) == 2:
            sure.add(cand[0])
        else:
            # Both roots are call or jump targets: the wrong guess is
            # further up one of the chains.
            a = chain(root)
            b = chain(other) if other is not None else []
            unsure.append([x[0] for x in (a, b) if x])
    if sure:
        return sure
    bad = set()
    for cand in unsure:
        if len(cand) == 2 and refs[cand[0]] != refs[cand[1]]:
            bad.add(min(cand, key=lambda r: refs[r]))
        else:
            bad.update(cand)
    return bad


def _analyze_once(img, md, extra_code, data_regions, rejected=frozenset()):
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
    overlap_src = []         # (address, root of the decode that ran into it)
    noret_thunks = set()     # "jmp [IAT]" thunks of imports that never return
    root_src = {}            # root -> (kind, parent root)
    insroot = {}             # instruction -> root of its decode
    cur = [None]             # the root that decode_block decodes now

    def add_reloc(fix, tgt, kind):
        if fix not in relocs:
            relocs[fix] = tgt
            kinds[fix] = kind

    def add_root(va, kind="code"):
        if va in rejected:
            return
        if img.in_text(va) and va not in code_starts and va not in data_bytes:
            root_src[va] = (kind, cur[0])
            code_starts.add(va)
            roots.append(va)

    def handle_operands(insn, prev=None):
        for op in insn.operands:
            if op.type == X.X86_OP_IMM and insn.imm_size == 4 and insn.imm_offset:
                if is_rel_branch(insn):
                    continue
                v = op.imm & 0xFFFFFFFF
                if not img.in_image(v):
                    continue
                fix = insn.address + insn.imm_offset
                if v in rejected:
                    doubtful.append((fix, v, "rejected code start in '%s %s', not a relocation" % (insn.mnemonic, insn.op_str)))
                    continue
                data_addr = False
                if STRICT_DATA_CODE and not img.in_text(v) and img.section_of(v) and (v & 0xFFFF) != 0:
                    # "xor reg, <data address>" (a check that a pointer is in a
                    # static array) and "sbb reg, reg; and reg, <data address>"
                    # (x ? &data : 0) use the address as a value.
                    if insn.mnemonic == "xor":
                        data_addr = True
                    elif (insn.mnemonic == "and" and prev is not None and prev.mnemonic == "sbb"
                          and len(prev.operands) == 2 and prev.operands[0].type == X.X86_OP_REG
                          and insn.operands[0].type == X.X86_OP_REG
                          and prev.operands[0].reg == prev.operands[1].reg == insn.operands[0].reg):
                        data_addr = True
                    elif (insn.mnemonic == "and" and prev is not None and prev.mnemonic == "pop"):
                        # "sbb eax, eax; pop esi; and eax, <data address>"
                        data_addr = True
                if not data_addr and (insn.mnemonic in ("test", "or", "xor")
                        or (insn.mnemonic == "and" and not img.in_text(v))):
                    # Bit masks, not addresses. "and reg, <code address>" stays a
                    # relocation: MSVC writes "x ? &func : 0" as neg/sbb/and.
                    doubtful.append((fix, v, "skipped mask in '%s %s'" % (insn.mnemonic, insn.op_str)))
                    continue
                add_reloc(fix, v, "imm:" + insn.mnemonic)
                code_labels.add(v)
                if img.in_text(v):
                    add_root(v, "imm")
                if v & 0xFFFF == 0:
                    doubtful.append((fix, v, "round immediate in '%s %s'" % (insn.mnemonic, insn.op_str)))
            elif op.type == X.X86_OP_MEM and insn.disp_size == 4 and insn.disp_offset:
                v = op.mem.disp & 0xFFFFFFFF
                if (STRICT_DATA_CODE and img.in_image(v) and insn.mnemonic == "lea" and op.mem.base != 0
                        and op.mem.index != 0 and img.in_text(v)):
                    # "lea ecx, [ecx + edx + 0x800080]": arithmetic with a
                    # constant (the rounding of a pixel average), not an address
                    doubtful.append((insn.address + insn.disp_offset, v, "constant in '%s %s'" % (insn.mnemonic, insn.op_str)))
                    continue
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
        cur[0] = start
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
                        overlap_src.append((va, start))
                    elif STRICT_DATA_CODE:
                        # code that runs into a jump table: wrong if its
                        # start was a guess
                        overlap_src.append((va, start))
                    break
                try:
                    insn = next(md.disasm(img.bytes_at(va, 16), va, 1))
                except StopIteration:
                    overlaps.append(va)
                    overlap_src.append((va, start))
                    break
                if any(b in data_bytes for b in range(va, va + insn.size)):
                    overlaps.append(va)
                    overlap_src.append((va, start))
                    break
                insns[va] = insn
                insroot[va] = start
                history.append(insn)
                for b in range(va, va + insn.size):
                    owner[b] = va
                handle_operands(insn, history[-2] if len(history) > 1 else None)
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
                        t = op.imm & 0xFFFFFFFF
                        add_root(t)
                        if img.in_text(t) and img.noret_import_call(t):
                            noret_thunks.add(t)
                            break
                    elif img.noret_import_call(insn.address):
                        break
                va += insn.size

    def scan_data():
        """Accept data dwords as pointers. Returns True if new code was added."""
        added = False
        for start, n in DATA_IN_TEXT.items():
            for a in range(start, start + n - 3, 4):
                v = img.dword(a)
                if a not in relocs and img.in_image(v) and v != img.base:
                    add_reloc(a, v, "data:text-block")
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
                        if STRICT_DATA_CODE and v % 2 and v not in code_labels:
                            # pairs of 16-bit numbers (0x00940093) and GUID bytes:
                            # data pointers to odd addresses are rare
                            doubtful.append((a, v, "odd data pointer, not used (" + name + ")"))
                        elif not stringy or v in code_labels or img.pointer_not_string(a):
                            add_reloc(a, v, "data:" + name)
                        else:
                            doubtful.append((a, v, "string-like data dword, not used (" + name + ")"))
                a += 4
            added |= flush_run(run)
        return added

    code_ref_cache = [-1, set()]

    def code_referenced(a):
        """True if code has the address a as an operand (relocated). A vtable
        start is stored by the constructor (mov dword [ecx], vtable)."""
        if code_ref_cache[0] != len(relocs):
            code_ref_cache[1] = {t for f, t in relocs.items() if img.in_text(f)}
            code_ref_cache[0] = len(relocs)
        return a in code_ref_cache[1]

    def is_mid(v):
        """an instruction inside a function, not a start"""
        return (v in insns and v not in code_starts
                and not img.looks_like_function_start(v, insns, owner, noret_thunks))

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
            if (known and STRICT_DATA_CODE and is_mid(v) and len(run) <= 2
                    and len({img.dword(x) for x in run if is_mid(img.dword(x))}) == 1):
                # a data word equal to an instruction address in the middle
                # of a function (0x00800080, a pixel mask in a D3DX format
                # table, next to a function pointer): a number, not a code
                # pointer. Only in runs of one or two words with one such
                # value: longer tables of code labels stay (they may be real).
                doubtful.append((a, v, "data word at an instruction inside a function, not used"))
                continue
            if known:
                add_reloc(a, v, "data:codeptr")
                continue
            if (STRICT_DATA_CODE and stringy and v % 16 == 0 and owner.get(v) is None and code_referenced(a)
                    and img.looks_like_function_start(v, insns, owner, noret_thunks)
                    and v not in rejected and v not in data_bytes):
                # the first entry of a vtable (here a vtable with one entry)
                # whose bytes look like text (0x00652c20: " ,e")
                add_reloc(a, v, "data:codeptr")
                if v not in code_starts:
                    cur[0] = None
                    add_root(v, "data")
                    added = True
                continue
            if stringy and img.is_ilt_thunk(v):
                doubtful.append((a, v, "string-like pointer to a link thunk, USED"))
            elif stringy and img.looks_like_function_start(v) and img.byte(a - 1) not in PRINTABLE:
                # Static initializer tables and vtables hold direct pointers
                # whose bytes can look like text ("0zm\0").
                doubtful.append((a, v, "string-like pointer to a function start, USED"))
            elif stringy and img.pointer_not_string(a):
                # a run of code pointers (initializer table __xc_a..__xc_z: 0x00503720, " 7P")
                doubtful.append((a, v, "string-like code pointer in a pointer run, USED"))
            elif stringy:
                doubtful.append((a, v, "string-like code pointer, not used"))
                continue
            solid = sum(1 for x in run if not img.looks_like_string(x))
            if STRICT_DATA_CODE:
                accept = img.looks_like_function_start(v, insns, owner, noret_thunks)
                if not accept and len(run) >= 3 and v % 16 == 0 and owner.get(v) is None:
                    # an entry of a vtable: the other entries are functions
                    # (a switch table before this function hides its start)
                    good = sum(1 for x in run if x != a and (
                        img.dword(x) in insns or img.looks_like_function_start(img.dword(x), insns, owner, noret_thunks)))
                    accept = good >= 2
            else:
                accept = solid >= 2 or img.looks_like_function_start(v)
            if accept:
                if v in data_bytes or (v in owner and owner[v] != v):
                    doubtful.append((a, v, "data pointer into the middle of code, not used"))
                    continue
                if v in rejected:
                    doubtful.append((a, v, "rejected code start, not a relocation"))
                    continue
                add_reloc(a, v, "data:codeptr")
                if v not in code_starts:
                    cur[0] = None
                    add_root(v, "data")
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
        cur[0] = None
        add_root(tgt, "eh")

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

    if STRICT_DATA_CODE:
        # "and reg, A; add reg, B" is "x ? A + B : B": A is a difference,
        # not an address (also for two function pointers, where B is one).
        for fix, tgt in list(relocs.items()):
            if kinds[fix] != "imm:and":
                continue
            i = insns.get(owner.get(fix))
            if i is None:
                continue
            n = insns.get(i.address + i.size)
            if (n is not None and n.mnemonic in ("add", "sub") and len(n.operands) == 2
                    and n.operands[1].type == X.X86_OP_IMM and n.operands[0].type == X.X86_OP_REG
                    and n.operands[0].reg == i.operands[0].reg):
                del relocs[fix]
                doubtful.append((fix, tgt, "and/add pair, a difference of constants, not a relocation"))

    for fix, tgt in list(relocs.items()):
        if tgt == img.base:
            doubtful.append((fix, tgt, "points to ImageBase (" + kinds[fix] + ")"))
        if img.in_text(tgt) and tgt in owner and owner[tgt] not in (tgt, -1):
            if kinds[fix].startswith("imm:mov") or (STRICT_DATA_CODE and not kinds[fix].startswith("disp")):
                # "mov [mem], 0x402848": a constant (flag bits) that happens to
                # be in the .text range. A code address is never inside an
                # instruction, so this is not a relocation.
                del relocs[fix]
                doubtful.append((fix, tgt, "constant inside an instruction, not a relocation (" + kinds[fix] + ")"))
            else:
                doubtful.append((fix, tgt, "target inside an instruction (" + kinds[fix] + ")"))

    return dict(insns=insns, owner=owner, relocs=relocs, kinds=kinds,
                doubtful=doubtful, jump_tables=jump_tables, code_starts=code_starts,
                overlaps=overlaps, conflicts=conflicts, data_bytes=data_bytes,
                overlap_src=overlap_src, root_src=root_src, insroot=insroot,
                noret_thunks=noret_thunks,
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
    ap.add_argument("--strict-data-code", action="store_true",
                    help="data pointers add new code only at function starts (see STRICT_DATA_CODE)")
    args = ap.parse_args()
    global STRICT_DATA_CODE
    STRICT_DATA_CODE = args.strict_data_code
    img = Image(args.exe)
    st = analyze(img, [int(x, 16) for x in args.extra_code])
    relocs, kinds = st["relocs"], st["kinds"]

    # SRW accepts only targets inside a section. Values that point at the
    # PE header (ImageBase) stay plain numbers; they are in the report.
    first = min(sec[1] for sec in img.sections)
    out = sorted((f, t) for f, t in relocs.items()
                 if f not in img.iat and t >= first and f not in NOT_RELOCS)
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

    # Thunks of imports that never return: SRW stops after a call to them.
    # A game's own srw/llasm/noret_procedures.sci (copied here first) is kept.
    noret_path = os.path.join(os.path.dirname(os.path.abspath(args.out)), "noret_procedures.sci")
    lines = set()
    if os.path.exists(noret_path):
        lines = {l.strip() for l in open(noret_path) if l.strip()}
    lines |= {"loc_%X" % va for va in st["noret_thunks"]}
    with open(noret_path, "w") as fh:
        for l in sorted(lines):
            fh.write(l + "\n")

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
        fh.write("rejected code starts: %d\n" % len(st.get("rejected", ())))
        fh.write("\n# doubtful (check by hand)\n")
        for fix, tgt, why in sorted(set(st["doubtful"])):
            fh.write("0x%x -> 0x%x  %s\n" % (fix, tgt, why))
        fh.write("\n# rejected code starts (made overlaps)\n")
        for va in sorted(st.get("rejected", ())):
            fh.write("0x%x\n" % va)
        fh.write("\n# overlaps\n")
        for va in sorted(set(st["overlaps"])):
            fh.write("0x%x\n" % va)
    print(open(args.report).read().split("\n# doubtful")[0])


if __name__ == "__main__":
    main()
