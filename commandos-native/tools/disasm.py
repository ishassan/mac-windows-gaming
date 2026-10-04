#!/usr/bin/env python3
"""Print the x86 code of build/srw/comandos.exe at an address.

Usage: disasm.py <hex address> [bytes before] [bytes after]
"""
import sys
import capstone
import pefile

pe = pefile.PE("build/srw/comandos.exe", fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
addr = int(sys.argv[1], 16)
before = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x40
after = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x40
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
start = addr - before
for i in md.disasm(pe.get_data(start - base, before + after), start):
    print("%s%08x  %-8s %s" % ("=>" if i.address == addr else "  ", i.address, i.mnemonic, i.op_str))
