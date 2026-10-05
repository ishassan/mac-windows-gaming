#!/usr/bin/env python3
"""Name C runtime math functions in the exe by running them in an emulator.

Each candidate function runs in Unicorn (x86 emulator) with test inputs,
in two calling conventions:
  - x87: arguments on the FPU stack, result in st0 (MSVC _CIxxx helpers)
  - cdecl: double arguments on the stack, result in st0
  - ftol: value in st0, integer result in edx:eax (_ftol, _ftol2)
The results are compared with Python's math functions. A function gets a
name only when every test input matches.

Usage: crt_probe.py <game>.exe 0x651d90 0x651ec0 ...
"""

import math
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UcError, UC_HOOK_CODE  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EDX,  # noqa: E402
                               UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_EIP,
                               UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES)
from gen_relocs import Image  # noqa: E402

STACK = 0x00100000
STACK_SIZE = 0x00100000
SCRATCH = 0x00300000   # code stub + result area
TEB = 0x00380000
FAKE = 0x00390000      # import stubs and fake per-thread data
GDT = 0x003A0000
SENTINEL = SCRATCH + 0xF00

ONE_ARG = {
    "sin": math.sin, "cos": math.cos, "tan": math.tan, "atan": math.atan,
    "asin": math.asin, "acos": math.acos, "exp": math.exp, "log": math.log,
    "log10": math.log10, "sqrt": math.sqrt, "floor": math.floor, "ceil": math.ceil,
    "fabs": math.fabs, "sinh": math.sinh, "cosh": math.cosh, "tanh": math.tanh,
}
TWO_ARG = {"atan2": math.atan2, "pow": math.pow, "fmod": math.fmod}
ONE_INPUTS = [0.3, 0.7, 0.25, 0.9, 0.11]
TWO_INPUTS = [(0.3, 0.7), (0.9, 0.2), (2.5, 1.5), (0.6, 3.0)]


def setup(img):
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    size = (len(img.mem) + 0xFFF) & ~0xFFF
    uc.mem_map(img.base, size)
    uc.mem_write(img.base, bytes(img.mem))
    uc.mem_map(STACK, STACK_SIZE)
    uc.mem_map(SCRATCH, 0x100000)
    # fs segment -> TEB (fs:[0] = -1, fs:[0x18] = self)
    teb = bytearray(0x1000)
    struct.pack_into("<I", teb, 0, 0xFFFFFFFF)
    struct.pack_into("<I", teb, 0x18, TEB)
    uc.mem_write(TEB, bytes(teb))
    # Flat data segment (SS/DS/ES) and an fs segment based at the TEB.
    uc.mem_write(GDT + 8 * 2, _gdt_entry(0, 0xFFFFF, 0x93, gran=True))
    uc.mem_write(GDT + 8 * 3, _gdt_entry(TEB, 0xFFF, 0x93))
    uc.reg_write(UC_X86_REG_GDTR, (0, GDT, 0x100, 0))
    uc.reg_write(UC_X86_REG_SS, 2 << 3)
    uc.reg_write(UC_X86_REG_DS, 2 << 3)
    uc.reg_write(UC_X86_REG_ES, 2 << 3)
    uc.reg_write(UC_X86_REG_FS, 3 << 3)
    # Imports: each IAT slot points to "mov eax, FAKE+0x800; ret" style stubs.
    stubs = {}
    off = 0
    for d in img.pe.DIRECTORY_ENTRY_IMPORT:
        for f in d.imports:
            name = (f.name or b"ord").decode()
            stub = FAKE + off
            # mov eax, FAKE+0x8000 (zeroed fake data); ret N (N patched below)
            n = STDCALL_ARGS.get(name, 0) * 4
            code = b"\xb8" + struct.pack("<I", FAKE + 0x8000) + b"\xc2" + struct.pack("<H", n)
            uc.mem_write(stub, code)
            uc.mem_write(f.address, struct.pack("<I", stub))
            stubs[stub] = name
            off += 16
    return uc, stubs


STDCALL_ARGS = {"TlsGetValue": 1, "TlsSetValue": 2, "GetLastError": 0, "SetLastError": 1,
                "GetCurrentThreadId": 0, "EncodePointer": 1, "DecodePointer": 1,
                "RaiseException": 4, "GetProcAddress": 2, "GetModuleHandleA": 1}


def _gdt_entry(base, limit, access, gran=False):
    e = limit & 0xFFFF
    e |= (base & 0xFFFFFF) << 16
    e |= access << 40
    e |= ((limit >> 16) & 0xF) << 48
    e |= (0xC if gran else 0x4) << 52   # 32-bit, page granularity if gran
    e |= ((base >> 24) & 0xFF) << 56
    return struct.pack("<Q", e)


def run(img, func, kind, args):
    uc, stubs = setup(img)
    data = SCRATCH + 0x800
    code = bytearray()
    esp = STACK + STACK_SIZE - 0x100
    if kind == "x87":
        for i, a in enumerate(reversed(args)):
            uc.mem_write(data + 8 * i, struct.pack("<d", a))
            code += b"\xdd\x05" + struct.pack("<I", data + 8 * i)   # fld qword [x]
    elif kind == "ftol":
        uc.mem_write(data, struct.pack("<d", args[0]))
        code += b"\xdd\x05" + struct.pack("<I", data)
    else:   # cdecl doubles
        for i, a in enumerate(reversed(args)):
            uc.mem_write(data + 8 * i, struct.pack("<d", a))
            code += b"\xff\x35" + struct.pack("<I", data + 8 * i + 4)   # push dword [hi]
            code += b"\xff\x35" + struct.pack("<I", data + 8 * i)       # push dword [lo]
    call_at = SCRATCH + len(code)
    code += b"\xe8" + struct.pack("<i", func - (call_at + 5))
    if kind == "cdecl":
        code += b"\x83\xc4" + bytes([8 * len(args)])                   # add esp, n
    if kind != "ftol":
        code += b"\xdd\x1d" + struct.pack("<I", data + 0x100)          # fstp qword [res]
    code += b"\xe9" + struct.pack("<i", SENTINEL - (SCRATCH + len(code) + 5))
    uc.mem_write(SCRATCH, bytes(code))
    uc.reg_write(UC_X86_REG_ESP, esp)
    try:
        uc.emu_start(SCRATCH, SENTINEL, count=200000)
    except UcError as e:
        return None, "fault %s at 0x%x" % (e, uc.reg_read(UC_X86_REG_EIP))
    if uc.reg_read(UC_X86_REG_EIP) != SENTINEL:
        return None, "did not return"
    if kind == "ftol":
        lo, hi = uc.reg_read(UC_X86_REG_EAX), uc.reg_read(UC_X86_REG_EDX)
        v = lo | (hi << 32)
        return (v - (1 << 64) if v >> 63 else v), None
    return struct.unpack("<d", bytes(uc.mem_read(data + 0x100, 8)))[0], None


def close(a, b):
    return a is not None and (abs(a - b) <= 1e-6 * max(1.0, abs(b)))


def identify(img, func):
    """Return matching names. A name matches when every input inside its
    domain gives the same result as Python's math function."""
    found = []
    one = [0.3, -0.6, 1.7, 2.9, -2.2]
    two = [(0.3, 0.7), (-0.9, 0.2), (2.5, 1.5), (0.6, 3.0)]
    for kind in ("x87", "cdecl"):
        res1 = [run(img, func, kind, [x])[0] for x in one]
        for name, f in ONE_ARG.items():
            checked = 0
            ok = True
            for x, r in zip(one, res1):
                try:
                    want = f(x)
                except ValueError:
                    continue
                checked += 1
                if not close(r, want):
                    ok = False
                    break
            if ok and checked >= 2:
                found.append("%s(%s)" % (name, kind))
        res2 = [run(img, func, kind, list(a))[0] for a in two]
        for name, f in TWO_ARG.items():
            if all(close(r, f(*a)) for a, r in zip(two, res2)):
                found.append("%s(%s)" % (name, kind))
    tests = [(2.7, 2), (-2.7, -2), (123456.9, 123456), (-0.5, 0)]
    if all(run(img, func, "ftol", [x])[0] == want for x, want in tests):
        found.append("ftol(truncate)")
    return found


def main():
    img = Image(sys.argv[1])
    for a in sys.argv[2:]:
        func = int(a, 16)
        names = identify(img, func)
        r, err = run(img, func, "x87", [0.3])
        print("0x%x: %s%s" % (func, ", ".join(names) or "no match",
                              "" if names else "  (x87 0.3 -> %s %s)" % (r, err or "")))


if __name__ == "__main__":
    main()
