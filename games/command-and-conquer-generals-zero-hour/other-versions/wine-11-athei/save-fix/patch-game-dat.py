#!/usr/bin/env python3
"""patch-game-dat.py: let game.dat 1.04 (Zero Hour, EA app copy) load save
files with Unicode strings of either width, for the Wine versions.

Problem: the original game writes 2 bytes for each character of a Unicode
string in a save file (wchar_t on Windows). GeneralsX, the port of the
released source code, wrote 4 bytes for each character before 2026-10-08
(wchar_t on macOS). The original game then stops with "Error loading game".

Fix: xfer-unicode.s (next to this script) goes into the free bytes at the end
of .text (0x938DF0), and the xferUser call of XferLoad::xferUnicodeString at
0x602139 becomes a call to it. Its variables are in the free bytes of .data1
(0xA70F00). The virtual sizes of the two sections grow to cover these bytes.
Nothing else changes: saves of the original game load as before, and the
game writes saves as before (2 bytes for each character).

Usage: patch-game-dat.py <game.dat> [<game.dat> ...]

- Needs i686-w64-mingw32-as and -objcopy (brew install mingw-w64).
- The first run keeps the original file as game.dat.orig (it never
  overwrites an existing .orig). The native build reads game.dat.orig when
  it is there (common/native-mac/mk/game.mk).
- A file that is already patched is not changed. A file with an older
  version of this patch gets the new code (from 2026-10-09: the width is
  guessed only at the save description, see xfer-unicode.s). Another
  game.dat version stops the script.
"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ORIGINAL_SHA256 = "9f615bfd3910ca174d0f044cdaee8c6fa26e5fdc28b89cc0e61c52943d857df7"
IMAGE_BASE = 0x400000
CALL_SITE = 0x602139
CALL_BYTES = bytes.fromhex("ff92b8000000")          # call dword [edx+0xb8]
CAVE = 0x938DF0
CAVE_END = 0x939000                                  # start of .rdata
DATA1_VSIZE = 0x1000


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def assemble():
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xfer-unicode.s")
    with tempfile.TemporaryDirectory() as tmp:
        obj, binary = os.path.join(tmp, "cave.o"), os.path.join(tmp, "cave.bin")
        subprocess.run(["i686-w64-mingw32-as", "-o", obj, src], check=True)
        subprocess.run(["i686-w64-mingw32-objcopy", "-O", "binary", "-j", ".text", obj, binary], check=True)
        return open(binary, "rb").read()


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    out = {}
    for i in range(count):
        at = table + 40 * i
        name = data[at:at + 8].rstrip(b"\0").decode()
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, at + 8)
        out[name] = dict(header=at, vsize=vsize, va=IMAGE_BASE + va, rawsize=rawsize, rawptr=rawptr)
    return out


def patch(path, cave):
    data = bytearray(open(path, "rb").read())
    before = sha256(data)
    sec = sections(data)
    text, data1 = sec[".text"], sec[".data1"]
    call_at = CALL_SITE - text["va"] + text["rawptr"]
    cave_at = CAVE - text["va"] + text["rawptr"]
    cave_end_at = CAVE_END - text["va"] + text["rawptr"]
    call = b"\xe8" + struct.pack("<i", CAVE - (CALL_SITE + 5))
    if data[call_at:call_at + 5] == call:
        if data[cave_at:cave_at + len(cave)] == cave and not any(data[cave_at + len(cave):cave_end_at]):
            print(f"{path}: already patched (sha256 {before})")
            return
        # An older version of this patch: put the new code in place of the old code.
        data[cave_at:cave_end_at] = bytes(cave_end_at - cave_at)
        data[cave_at:cave_at + len(cave)] = cave
        struct.pack_into("<I", data, text["header"] + 8, max(text["vsize"], CAVE + len(cave) - text["va"]))
        open(path, "wb").write(data)
        print(f"{path}: patch updated (sha256 {before} -> {sha256(data)})")
        return
    if before != ORIGINAL_SHA256:
        sys.exit(f"{path}: not game.dat 1.04 (sha256 {before}); nothing changed")
    if CAVE + len(cave) > CAVE_END or any(data[cave_at:cave_end_at]):
        sys.exit(f"{path}: the free bytes at 0x{CAVE:X} are not free; nothing changed")
    if data[call_at:call_at + 6] != CALL_BYTES:
        sys.exit(f"{path}: unexpected bytes at 0x{CALL_SITE:X}; nothing changed")

    orig = path + ".orig"
    if not os.path.exists(orig):
        shutil.copy2(path, orig)
    data[cave_at:cave_at + len(cave)] = cave
    data[call_at:call_at + 6] = call + b"\x90"
    struct.pack_into("<I", data, text["header"] + 8, max(text["vsize"], CAVE + len(cave) - text["va"]))
    struct.pack_into("<I", data, data1["header"] + 8, max(data1["vsize"], DATA1_VSIZE))
    open(path, "wb").write(data)
    print(f"{path}: patched (sha256 {before} -> {sha256(data)}; original kept as {os.path.basename(orig)})")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cave = assemble()
    for path in sys.argv[1:]:
        patch(path, cave)


if __name__ == "__main__":
    main()
