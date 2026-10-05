#!/usr/bin/env python3
"""patch-miles.py: make the Miles sound library (mss32.dll) work with
Homebrew wine-crossover 23.7.1 (Wine 8.0.1).

Problem: with this Wine, the Miles timer thread hangs in SuspendThread, and
the game hangs at the start with no sound.

Fix: in the import table of mss32.dll, the names "SuspendThread" and
"ResumeThread" (imports from KERNEL32.dll) become "GetThreadId". GetThreadId
takes the same single argument (a thread handle) and uses the same calling
convention (stdcall), so the stack stays correct. No thread is then
suspended. Only the two name strings change (24 bytes), and the file size
stays the same.

Usage: patch-miles.py <mss32.dll> [<mss32.dll> ...]

- The first run keeps the original file as <file>.orig (it never overwrites
  an existing .orig).
- A file that is already patched is not changed.
- The script prints the SHA-256 before and after. KNOWN lists the files that
  were patched on 2026-10-05.
"""
import hashlib
import os
import shutil
import struct
import sys

OLD_NAMES = (b"SuspendThread", b"ResumeThread")
NEW_NAME = b"GetThreadId"

# original SHA-256 -> (game, patched SHA-256)
KNOWN = {
    "8cbfe309cf63139ae652542f2813aa762a5fc6d0d47af2dd3504b3d8374d2ed1":
        ("Revenant (GOG) mss32.dll",
         "eed56bae31a658a11cad2fb0fde86f0bd2ea7f8f74c8124f6b54c9cee7b18d84"),
    "c71c9df1fdfb96995995b4f13f8bafef03374a6b93c3478b4acb0c73f301dda4":
        ("Commandos: Behind Enemy Lines (GOG) MSS32.DLL",
         "a5d70a915a1328cf40a5a0d12c2b009a5991e7bd6fa9ef80c6a19a8522304854"),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def import_names(data):
    """Return (dll name, function name, file offset of the name) for each
    import by name in a PE32 file."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE file")
    nsect, = struct.unpack_from("<H", data, pe + 6)
    optsize, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("not a 32-bit PE file")
    imp_rva, = struct.unpack_from("<I", data, opt + 96 + 8)
    sections = []
    for i in range(nsect):
        s = opt + optsize + 40 * i
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsize, rsize), raw))

    def off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("RVA 0x%x is in no section" % rva)

    def cstr(o):
        return bytes(data[o:data.index(b"\0", o)])

    d = off(imp_rva)
    while True:
        oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", data, d)
        if name_rva == 0:
            break
        dll = cstr(off(name_rva))
        t = off(oft or ft)
        while True:
            thunk, = struct.unpack_from("<I", data, t)
            if thunk == 0:
                break
            if not thunk & 0x80000000:          # import by name: hint, name
                o = off(thunk) + 2
                yield dll, cstr(o), o
            t += 4
        d += 20


def patch(path):
    data = bytearray(open(path, "rb").read())
    before = sha256(data)
    known = KNOWN.get(before)
    print("%s\n  before: %s%s" % (path, before, "  (%s)" % known[0] if known else ""))
    found = [(dll, name, o) for dll, name, o in import_names(data)
             if dll.lower() == b"kernel32.dll" and name in OLD_NAMES]
    if not found:
        names = [name for dll, name, _ in import_names(data) if dll.lower() == b"kernel32.dll"]
        if names.count(NEW_NAME) >= 2:
            print("  already patched: no change")
            return True
        print("  error: no SuspendThread or ResumeThread import from KERNEL32.dll", file=sys.stderr)
        return False
    for _, name, o in found:
        data[o:o + len(name)] = NEW_NAME.ljust(len(name), b"\0")
        print("  0x%x: %s -> %s" % (o, name.decode(), NEW_NAME.decode()))
    orig = path + ".orig"
    if not os.path.exists(orig):
        shutil.copy2(path, orig)
        print("  original kept as", orig)
    with open(path, "r+b") as f:
        f.write(data)
    after = sha256(data)
    print("  after:  %s" % after)
    if known:
        print("  " + ("matches the known patched file" if after == known[1]
                      else "warning: differs from the known patched file"))
    return True


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print((__doc__ or "").strip().split("\n\n")[3])
        sys.exit(2)
    sys.exit(0 if all([patch(p) for p in sys.argv[1:]]) else 1)
