#!/usr/bin/env python3
"""patch-wow64cpu.py: stop the start-up crash of 32-bit games in Wine 11
under Rosetta (Gcenx wine-stable 11.0_1, the app "Wine Stable").

Problem: Wine 11 runs 32-bit programs in WoW64 mode. The 64-bit part of Wine
(wow64cpu.dll) changes the CPU between 32-bit and 64-bit code with a far
jump. Under Rosetta, the CPU mode sometimes does not change on that jump.
The 32-bit Direct3D games then crash at the start, in both directions.

Fix: both jumps now land on a small stub that runs correctly in both modes.
The stub finds which mode the CPU is in. If the mode did not change, it does
the change again, then continues as before.
  - Entry (32 -> 64): if the CPU is still in 32-bit mode, a far return to
    the 64-bit code selector (0x2b) runs again.
  - Return (64 -> 32): ecx holds the real return address. If the CPU is still
    in 64-bit mode, an lretq to the 32-bit code selector runs again, then a
    jump to ecx. ecx is a scratch register at these returns.
The stubs go into empty space at the end of the code section, and the
section size in the header grows to include them. The idea of the entry stub
comes from hardparking/link-ecu-linux-tuning pull request 4 (MIT).

Usage: patch-wow64cpu.py [<wow64cpu.dll>]
Default: /Applications/Wine Stable.app/.../x86_64-windows/wow64cpu.dll

- The script checks the expected bytes first and stops on any other build.
- The first run keeps the original file as <file>.orig (it never overwrites
  an existing .orig).
- A file that is already patched is not changed.
- A Wine update replaces the file, so run the script again after an update.

Tested 2026-10-06 with Revenant: without the fix the game started in only 3
to 4 of 10 tries; with it, 20 of 20 starts worked.
"""
import hashlib
import os
import shutil
import struct
import sys

DEFAULT = ("/Applications/Wine Stable.app/Contents/Resources/wine/lib/wine/"
           "x86_64-windows/wow64cpu.dll")
# Gcenx wine-stable 11.0_1: original SHA-256 -> patched SHA-256
KNOWN_ORIG = "cbbf4d054d5488e19d82ea24e7c280c3408ee43b00fce708d1b559acada9d21e"
KNOWN_PATCHED = "99138d53b385a3271a2e5631a527330e69d9025076c55e411bca2129a07930f8"

BASE = 0x7a400000                      # preferred image base; the code below is position-independent
SYS_ENTRY, UNIX_ENTRY = 0x7a401110, 0x7a401214
SYS_RET, UNIX_RET = 0x7a401191, 0x7a40127a     # start of the "far jump back to 32-bit" blocks
CS32_VAR = 0x7a40600c                          # the 32-bit code selector variable of wow64cpu
CAVE = 0x7a401a00
EXPECT = {
    SYS_ENTRY: "4c87f4", UNIX_ENTRY: "4c87f4",
    SYS_RET: "418b95b8000000891424", UNIX_RET: "418b95b8000000891424",
    0x7a4013ef: "488d1d1afdffff", 0x7a401525: "488d0de8fcffff",
}


def off(va):
    return va - BASE                   # .text raw offset == RVA in this build


def rel32(frm_end, to):
    return struct.pack("<i", to - frm_end)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def make_stubs():
    """Return (code, sys_stub, unix_stub, ret32, ret64) for the cave."""
    code = bytearray()

    def here():
        return CAVE + len(code)

    def entry_stub(target):
        s = here()
        code.extend(bytes.fromhex("31c9"))        # xor ecx,ecx
        code.extend(bytes.fromhex("4189d2"))      # 32: inc ecx; mov edx,edx | 64: mov r10d,edx
        code.extend(bytes.fromhex("85c9"))        # test ecx,ecx
        code.extend(bytes.fromhex("740d"))        # jz L64
        code.extend(bytes.fromhex("e800000000"))  # 32: call next
        code.extend(bytes.fromhex("59"))          #     pop ecx            (ecx = s+14)
        code.extend(bytes.fromhex("8d49f2"))      #     lea ecx,[ecx-14]   (ecx = s: check again)
        code.extend(bytes.fromhex("6a2b"))        #     push 0x2b          (64-bit code selector)
        code.extend(bytes.fromhex("51"))          #     push ecx
        code.extend(bytes.fromhex("cb"))          #     retf
        assert here() - s == 22
        code.extend(b"\xe9")
        code.extend(rel32(here() + 4, target))    # L64: jmp target
        return s

    sys_stub = entry_stub(SYS_ENTRY)
    unix_stub = entry_stub(UNIX_ENTRY)
    while len(code) % 16:
        code.append(0xcc)

    ret32 = here()                                # lands here after the far jump to 32-bit
    code.extend(bytes.fromhex("31d2"))            # xor edx,edx
    code.extend(bytes.fromhex("4290"))            # 32: inc edx; nop | 64: nop
    code.extend(bytes.fromhex("85d2"))            # test edx,edx
    jnz_at = len(code)
    code.extend(bytes.fromhex("7500"))            # jnz L32 (set below)
    code.extend(b"\x8b\x15")
    code.extend(rel32(here() + 4, CS32_VAR))      # 64: mov edx,[rip+cs32]
    code.extend(bytes.fromhex("52"))              # push rdx           (cs)
    code.extend(b"\x48\x8d\x15")
    code.extend(rel32(here() + 4, ret32))         # lea rdx,[rip+ret32]
    code.extend(bytes.fromhex("52"))              # push rdx           (rip)
    code.extend(bytes.fromhex("48cb"))            # lretq -> check again
    code[jnz_at + 1] = len(code) - (jnz_at + 2)
    code.extend(bytes.fromhex("ffe1"))            # L32: jmp ecx
    while len(code) % 16:
        code.append(0xcc)

    ret64 = here()                                # replaces both "far jump back" blocks
    code.extend(bytes.fromhex("418b8db8000000"))  # mov ecx,[r13+0xb8]  (real 32-bit eip)
    code.extend(b"\x48\x8d\x15")
    code.extend(rel32(here() + 4, ret32))         # lea rdx,[rip+ret32]
    code.extend(bytes.fromhex("891424"))          # mov [rsp],edx
    code.extend(bytes.fromhex("418b95bc000000"))  # mov edx,[r13+0xbc]  (32-bit cs)
    code.extend(bytes.fromhex("89542404"))        # mov [rsp+4],edx
    code.extend(bytes.fromhex("458bb5c4000000"))  # mov r14d,[r13+0xc4] (32-bit esp)
    code.extend(bytes.fromhex("4c87f4"))          # xchg rsp,r14
    code.extend(bytes.fromhex("41ff2e"))          # ljmp *(r14)
    assert len(code) < 0x100
    return code, sys_stub, unix_stub, ret32, ret64


def patch(path):
    data = bytearray(open(path, "rb").read())
    before = sha256(data)
    print("%s\n  before: %s" % (path, before))
    if before == KNOWN_PATCHED:
        print("  already patched: no change")
        return True
    for va, hx in EXPECT.items():
        got = data[off(va):off(va) + len(hx) // 2].hex()
        if got != hx:
            print("  error: unexpected bytes at 0x%x (%s, expected %s). This is not the "
                  "Wine 11.0 build the script knows; no change." % (va, got, hx), file=sys.stderr)
            return False
    if set(data[off(CAVE):off(CAVE) + 0x100]) != {0}:
        print("  error: the space for the stubs is not empty; no change", file=sys.stderr)
        return False
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sh = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    if data[sh:sh + 8] != b".text\0\0\0" or struct.unpack_from("<II", data, sh + 8) != (0x9d0, 0x1000):
        print("  error: unexpected .text section header; no change", file=sys.stderr)
        return False

    code, sys_stub, unix_stub, _, ret64 = make_stubs()
    data[off(CAVE):off(CAVE) + len(code)] = code
    for at in (SYS_RET, UNIX_RET):                # jmp ret64
        data[off(at):off(at) + 5] = b"\xe9" + rel32(at + 5, ret64)
    for at, old, new in ((0x7a4013ef, SYS_ENTRY, sys_stub),     # syscall thunk target
                         (0x7a401525, UNIX_ENTRY, unix_stub)):  # Unix call thunk target
        disp = struct.unpack_from("<i", data, off(at) + 3)[0]   # lea reg,[rip+disp32], 7 bytes
        assert at + 7 + disp == old, hex(at + 7 + disp)
        struct.pack_into("<i", data, off(at) + 3, new - (at + 7))
    struct.pack_into("<I", data, sh + 8, 0xb00)   # .text VirtualSize covers the stubs

    orig = path + ".orig"
    if not os.path.exists(orig):
        shutil.copy2(path, orig)
        print("  original kept as", orig)
    with open(path, "r+b") as f:
        f.write(data)
    after = sha256(data)
    print("  after:  %s" % after)
    if before == KNOWN_ORIG:
        print("  " + ("matches the known patched file" if after == KNOWN_PATCHED
                      else "warning: differs from the known patched file"))
    return True


if __name__ == "__main__":
    if len(sys.argv) > 2 or (len(sys.argv) == 2 and sys.argv[1].startswith("-")):
        print((__doc__ or "").strip().split("\n\n")[3])
        sys.exit(2)
    sys.exit(0 if patch(sys.argv[1] if len(sys.argv) == 2 else DEFAULT) else 1)
