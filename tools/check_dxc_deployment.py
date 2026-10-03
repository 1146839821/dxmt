#!/usr/bin/env python3
"""Validate PE deployment inputs; this does not establish redistribution rights."""

import pathlib
import struct
import sys


def check(directory, architecture):
    expected = {"x86": (0x14C, 0x10B), "x86_64": (0x8664, 0x20B),
                "aarch64": (0xA641, 0x20B)}[architecture]
    source = pathlib.Path(directory)
    if not source.is_absolute():
        raise ValueError("DXC deployment directory must be absolute")
    if not (source / "LICENSE.txt").is_file() or not (source / "LICENSE.txt").stat().st_size:
        raise ValueError("DXC deployment requires a nonempty supplied LICENSE.txt")
    for name in ("dxcompiler.dll", "dxil.dll"):
        data = (source / name).read_bytes()
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError(f"{name}: invalid DOS header")
        offset = struct.unpack_from("<I", data, 60)[0]
        if offset < 64 or offset > len(data) - 26 or data[offset:offset + 4] != b"PE\0\0":
            raise ValueError(f"{name}: invalid PE header")
        machine = struct.unpack_from("<H", data, offset + 4)[0]
        optional_size, flags, magic = struct.unpack_from("<HHH", data, offset + 20)
        if optional_size < 2 or offset + 24 + optional_size > len(data) or not flags & 0x2000:
            raise ValueError(f"{name}: invalid DLL optional header")
        if (machine, magic) != expected:
            raise ValueError(f"{name}: incompatible PE machine/magic {machine:#x}/{magic:#x} for {architecture}")


if __name__ == "__main__":
    try:
        check(sys.argv[1], sys.argv[2])
    except (OSError, ValueError, KeyError, IndexError, struct.error) as error:
        sys.exit(str(error))
