#!/usr/bin/env python3
"""Test of the save reader of Unicode strings, with synthetic save files (no game data).

The same read sequences go through the C reader of the native port
(native-mac/runtime/llasm/xfer-unicode.c, built with harness.cpp and the stub
headers in inc/) and through the x86 code of ../xfer-unicode.s (run in the
Unicorn emulator). Both must give the expected text and file position.
Needs clang++, python3 -m pip install unicorn, and the assembler tools of
../patch-game-dat.py. Usage: python3 test.py (in this folder).
"""
import importlib.util
import os, struct, subprocess, sys
from unicorn import *
from unicorn.x86_const import *

def ustr(t, w):
    return bytes([len(t)]) + b''.join(ord(c).to_bytes(w, 'little') for c in t)

def save(path, w, desc, mission=False, mids=()):
    """desc at 0x2b (0x45 with a mission map name); mids: (position, text, zero bytes after)"""
    b = bytearray(b'\x0fCHUNK_GameState' + b'\x10\0\0\0' + b'\x02' + b'\0' * 4)
    mm = b'Maps\\GLA04\\GLA04.map_xxxxx' if mission else b''
    b += bytes([len(mm)]) + mm + b'\x01' * 16
    pos = {'desc': len(b)}
    b += ustr(desc, w) + b'\x05LABEL'
    for p, t, z in mids:
        b += b'\x07' * (p - len(b)); pos[t] = p
        b += ustr(t, w) + b'\0' * z + b'\x09'
    b += b'\x07' * 64
    open(path, 'wb').write(b)
    return pos

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)
D = 'build'; os.makedirs(D, exist_ok=True)
spec = importlib.util.spec_from_file_location('patch', os.path.join(HERE, '..', 'patch-game-dat.py'))
patch = importlib.util.module_from_spec(spec); spec.loader.exec_module(patch)
open(f'{D}/cave.bin', 'wb').write(patch.assemble())
subprocess.run(['clang++', '-x', 'c++', '-std=c++17', '-O1', '-Wno-unused', '-Iinc',
                '../../../native-mac/runtime/llasm/xfer-unicode.c', 'harness.cpp', '-o', f'{D}/reader'], check=True)
mids = [(0x1000, 'Hello', 0), (0x2000, 'A', 8), (0x3000, 'Objective', 0), (0x3100, 'B', 4)]
pA2 = save(f'{D}/a2.sav', 2, 'GLA 4-3', mids=mids)
pA4 = save(f'{D}/a4.sav', 4, 'GLA 2-2', mids=mids)
pM2 = save(f'{D}/m2.sav', 2, 'Mission Start - GLA 4', True, mids)
pM4 = save(f'{D}/m4.sav', 4, 'Mission Start - GLA 9', True, mids)
pE2 = save(f'{D}/e2.sav', 2, '', mids=mids)

def seq(slot, f, p, keys):
    return sum(([f'seek {slot} {p[k]}', f'read {slot}'] for k in keys), [])

ALL = ['desc', 'Hello', 'A', 'Objective', 'B']
cases = {}
# 1: a 2-byte save loads; in the middle, the game reads the header of a save (another FILE).
for other, od in (('a2.sav', 'GLA 4-3'), ('a4.sav', 'GLA 2-2')):
    ops = [f'open 0 {D}/a2.sav'] + seq(0, 0, pA2, ['desc', 'Hello']) + [f'open 1 {D}/{other}'] + seq(1, 0, pA2, ['desc']) + \
          ['close 1'] + seq(0, 0, pA2, ['A', 'Objective', 'B']) + ['close 0']
    cases[f'nested read of {other} in a 2-byte save'] = (ops, ['GLA 4-3', 'Hello', od, 'A', 'Objective', 'B'])
# 2: the same in a 4-byte save
ops = [f'open 0 {D}/a4.sav'] + seq(0, 0, pA4, ['desc', 'Hello']) + [f'open 1 {D}/a2.sav'] + seq(1, 0, pA2, ['desc']) + \
      ['close 1'] + seq(0, 0, pA4, ['A', 'Objective', 'B']) + ['close 0']
cases['nested read of a2.sav in a 4-byte save'] = (ops, ['GLA 2-2', 'Hello', 'GLA 4-3', 'A', 'Objective', 'B'])
# 3: a backward seek in the middle of a 2-byte save
ops = [f'open 0 {D}/a2.sav'] + seq(0, 0, pA2, ['desc', 'Hello', 'Objective', 'A', 'B']) + ['close 0']
cases['backward seek in a 2-byte save'] = (ops, ['GLA 4-3', 'Hello', 'Objective', 'A', 'B'])
# 4: the load list (the same FILE address again and again), then a load
lst = [('a4.sav', pA4, 'GLA 2-2'), ('m2.sav', pM2, 'Mission Start - GLA 4'), ('a2.sav', pA2, 'GLA 4-3'),
       ('m4.sav', pM4, 'Mission Start - GLA 9'), ('a2.sav', pA2, 'GLA 4-3'), ('e2.sav', pE2, '')]
ops, ex = [], []
for f, p, d in lst:
    ops += [f'open 0 {D}/{f}'] + seq(0, 0, p, ['desc']) + ['close 0']; ex += [d]
ops += [f'open 0 {D}/a2.sav'] + seq(0, 0, pA2, ALL) + ['close 0']; ex += ['GLA 4-3', 'Hello', 'A', 'Objective', 'B']
ops += [f'open 0 {D}/m4.sav'] + seq(0, 0, pM4, ALL) + ['close 0']; ex += ['Mission Start - GLA 9', 'Hello', 'A', 'Objective', 'B']
cases['load list, then loads'] = (ops, ex)
# 5: whole saves, one after the other
for f, p, d in (('a2.sav', pA2, 'GLA 4-3'), ('a4.sav', pA4, 'GLA 2-2'), ('m2.sav', pM2, 'Mission Start - GLA 4')):
    cases[f'plain load of {f}'] = ([f'open 0 {D}/{f}'] + seq(0, 0, p, ALL) + ['close 0'], [d, 'Hello', 'A', 'Objective', 'B'])

# --- the x86 code in Unicorn
CAVE, SENT, FTELL, FREAD, FSEEK, XUSER, VT, BUF = 0x938DF0, 0x939900, 0x939800, 0x939810, 0x939820, 0x939830, 0x9000, 0x20000
def run_asm(binfile, ops):
    code = open(binfile, 'rb').read()
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(0, 0x40000); mu.mem_map(0x938000, 0x2000); mu.mem_map(0xA70000, 0x1000); mu.mem_map(0x100000, 0x10000)
    mu.mem_write(CAVE, code)
    for iat, stub in ((0x939458, FSEEK), (0x93945C, FTELL), (0x939460, FREAD)):
        mu.mem_write(iat, struct.pack('<I', stub)); mu.mem_write(stub, b'\xc3')
    mu.mem_write(XUSER, b'\xc2\x08\x00'); mu.mem_write(VT + 0xB8, struct.pack('<I', XUSER))
    files, out, err = {}, [], []
    rd = lambda a: struct.unpack('<I', mu.mem_read(a, 4))[0]
    def hook(mu, addr, size, _):
        esp = mu.reg_read(UC_X86_REG_ESP)
        if addr == FTELL: mu.reg_write(UC_X86_REG_EAX, files[rd(esp + 4)].tell())
        elif addr == FSEEK: files[rd(esp + 4)].seek(rd(esp + 8)); mu.reg_write(UC_X86_REG_EAX, 0)
        elif addr == FREAD:
            p, s, n, f = rd(esp + 4), rd(esp + 8), rd(esp + 12), rd(esp + 16)
            d = files[f].read(s * n); mu.mem_write(p, d); mu.reg_write(UC_X86_REG_EAX, len(d) // s)
        elif addr == XUSER:
            this = mu.reg_read(UC_X86_REG_ECX); p, s = rd(esp + 4), rd(esp + 8)
            d = files[rd(this + 0x10)].read(s)
            if len(d) < s: err.append(1)
            mu.mem_write(p, d)
        # the stubs change ecx and edx, as a real C function may
        if addr in (FTELL, FSEEK, FREAD, XUSER):
            mu.reg_write(UC_X86_REG_ECX, 0xdeadbeef); mu.reg_write(UC_X86_REG_EDX, 0xdeadbeef)
    mu.hook_add(UC_HOOK_CODE, hook, begin=0x939800, end=0x939840)
    for o in ops:
        w = o.split(); slot = int(w[1]); self, fa = 0x8000 + slot * 0x40, 0x100 * (slot + 1)
        if w[0] == 'open':
            files[fa] = open(w[2], 'rb'); mu.mem_write(self, struct.pack('<I', VT)); mu.mem_write(self + 0x10, struct.pack('<I', fa))
        elif w[0] == 'close': files.pop(fa).close()
        elif w[0] == 'seek': files[fa].seek(int(w[2]))
        else:
            n = files[fa].read(1)[0]
            if n == 0: out.append(f'@{files[fa].tell()}'); continue
            esp = 0x10F000
            mu.mem_write(esp, struct.pack('<III', SENT, BUF, 2 * n)); mu.reg_write(UC_X86_REG_ESP, esp)
            mu.reg_write(UC_X86_REG_ECX, self); mu.reg_write(UC_X86_REG_EBP, 0x1234)
            for r in (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI): mu.reg_write(r, 0x5555)
            err.clear(); mu.emu_start(CAVE, SENT)
            assert mu.reg_read(UC_X86_REG_ESP) == esp + 12, 'stack'
            assert [mu.reg_read(r) for r in (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)] == [0x5555] * 3 + [0x1234], 'registers'
            if err: out.append('READ ERROR'); continue
            b = mu.mem_read(BUF, 2 * n)
            out.append(''.join(chr(c) if c < 128 else '?' for c in struct.unpack(f'<{n}H', b)) + f'@{files[fa].tell()}')
    return out

def run_c(exe, ops):
    r = subprocess.run([exe], input='\n'.join(ops) + '\n', capture_output=True, text=True)
    return r.stdout.split('\n')[:-1]

WID = {'a2.sav': 2, 'a4.sav': 4, 'm2.sav': 2, 'm4.sav': 4, 'e2.sav': 2}
def expect(ops, ex):
    cur, pos, out, i = {}, {}, [], 0
    for o in ops:
        w = o.split()
        if w[0] == 'open': cur[w[1]] = os.path.basename(w[2])
        elif w[0] == 'seek': pos[w[1]] = int(w[2])
        elif w[0] == 'read':
            t = ex[i]; i += 1
            out.append(f"{t}@{pos[w[1]] + 1 + len(t) * WID[cur[w[1]]]}")
    return out
print('the old rule (before 2026-10-09) fails the cases "nested read ... in a 2-byte save", "backward seek" and "load list"')
ok = True
for name, (ops, ex) in cases.items():
    ex = expect(ops, ex)
    res = {'c': run_c(f'{D}/reader', ops), 'x86': run_asm(f'{D}/cave.bin', ops)}
    line = ' '.join(f"{k}:{'ok' if v == ex else 'WRONG'}" for k, v in res.items())
    print(f'{name:45s} {line}')
    for k in res:
        if res[k] != ex: ok = False; print('   ', k, res[k], 'expected', ex)
sys.exit(0 if ok else 1)
