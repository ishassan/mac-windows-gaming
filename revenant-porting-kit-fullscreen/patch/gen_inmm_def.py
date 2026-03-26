#!/usr/bin/env python3
"""
Generate _inmm.def forwarding file from the game's original _inmm.dll.

Parses the PE export table and generates a .def file that forwards
all exported functions to _inmm_real.dll (the renamed original).

Usage:
    python3 gen_inmm_def.py /path/to/_inmm.dll [output.def]

Output defaults to _inmm.def in the current directory.
"""

import struct
import sys
import os


def rva_to_offset(data, rva, pe_offset):
    """Convert a Relative Virtual Address to a file offset."""
    num_sections = struct.unpack_from("<H", data, pe_offset + 0x06)[0]
    opt_header_size = struct.unpack_from("<H", data, pe_offset + 0x14)[0]
    section_start = pe_offset + 0x18 + opt_header_size

    for i in range(num_sections):
        sec_off = section_start + i * 40
        vaddr = struct.unpack_from("<I", data, sec_off + 12)[0]
        vsize = struct.unpack_from("<I", data, sec_off + 8)[0]
        raw_off = struct.unpack_from("<I", data, sec_off + 20)[0]
        if vaddr <= rva < vaddr + vsize:
            return rva - vaddr + raw_off
    return rva


def extract_exports(dll_path):
    """Extract exported function names from a PE DLL."""
    with open(dll_path, "rb") as f:
        data = f.read()

    if data[:2] != b"MZ":
        raise ValueError(f"{dll_path} is not a valid PE file (missing MZ header)")

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\x00\x00":
        raise ValueError(f"{dll_path} is not a valid PE file (missing PE signature)")

    export_rva = struct.unpack_from("<I", data, pe_offset + 0x78)[0]
    if export_rva == 0:
        raise ValueError(f"{dll_path} has no export directory")

    exp_off = rva_to_offset(data, export_rva, pe_offset)
    num_names = struct.unpack_from("<I", data, exp_off + 24)[0]
    name_rva_table = struct.unpack_from("<I", data, exp_off + 32)[0]

    names = []
    for i in range(num_names):
        name_rva = struct.unpack_from(
            "<I", data, rva_to_offset(data, name_rva_table, pe_offset) + i * 4
        )[0]
        name_off = rva_to_offset(data, name_rva, pe_offset)
        name = b""
        while data[name_off] != 0:
            name += bytes([data[name_off]])
            name_off += 1
        names.append(name.decode("ascii"))

    return names


def generate_def(names, output_path):
    """Generate a .def file that forwards all exports to _inmm_real.dll."""
    with open(output_path, "w") as f:
        f.write("LIBRARY _INMM\n")
        f.write("EXPORTS\n")
        for name in names:
            f.write(f"    {name} = _inmm_real.{name}\n")
    return len(names)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path/to/_inmm.dll> [output.def]")
        print()
        print("Generates _inmm.def with forwarding exports to _inmm_real.dll.")
        sys.exit(1)

    dll_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else "_inmm.def"

    if not os.path.exists(dll_path):
        print(f"Error: {dll_path} not found")
        sys.exit(1)

    names = extract_exports(dll_path)
    count = generate_def(names, output_path)
    print(f"Generated {output_path} with {count} forwarded exports")


if __name__ == "__main__":
    main()
