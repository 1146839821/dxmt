#!/usr/bin/env python3
"""Show why loaded-image identity cannot be inferred from UUID or path alone."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def run(*args, input=None):
    return subprocess.run(args, input=input, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def uuid_offset(data):
    magic, _, _, _, count, size = struct.unpack_from("<6I", data)
    if magic != 0xfeedfacf or size > len(data) - 32:
        raise ValueError("expected a bounded thin 64-bit Mach-O image")
    offset = 32
    for _ in range(count):
        kind, length = struct.unpack_from("<2I", data, offset)
        if length < 8 or offset + length > 32 + size:
            raise ValueError("invalid load command")
        if kind == 0x1b and length >= 24:
            return offset + 8
        offset += length
    raise ValueError("image has no UUID")


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


def main():
    probe_source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="dxmt-darwin-image-") as temp:
        root = Path(temp).resolve()
        probe = root / "probe"
        run("xcrun", "clang", "-Wall", "-Wextra", "-o", str(probe), str(probe_source))
        for name, value in (("a", 11), ("b", 22)):
            source = root / f"{name}.c"
            source.write_text(f"int marker(void) {{ return {value}; }}\n")
            run("xcrun", "clang", "-dynamiclib", "-o", str(root / f"{name}.dylib"), str(source))
        a = (root / "a.dylib").read_bytes()
        b = bytearray((root / "b.dylib").read_bytes())
        auuid = a[uuid_offset(a):uuid_offset(a) + 16]
        buuid = b[uuid_offset(b):uuid_offset(b) + 16]
        if auuid == buuid:
            raise AssertionError("test libraries unexpectedly have identical UUIDs")
        for same_uuid in (False, True):
            live = root / "live.dylib"
            replacement = root / "replacement.dylib"
            shutil.copyfile(root / "a.dylib", live)
            data = bytearray(b)
            if same_uuid:
                offset = uuid_offset(data)
                data[offset:offset + 16] = auuid
            replacement.write_bytes(data)
            run("codesign", "--force", "--sign", "-", str(replacement))
            control = run(str(probe), str(replacement), input=b"\n")
            expected_uuid = auuid if same_uuid else buuid
            if not control.stdout.decode().startswith(f"22 {expected_uuid.hex()} "):
                raise AssertionError("replacement is not independently loadable")
            before = digest(live)
            process = subprocess.Popen([str(probe), str(live)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            first = process.stdout.readline().strip()
            os.replace(replacement, live)
            after = digest(live)
            stdout, stderr = process.communicate("\n", timeout=20)
            if process.returncode:
                raise AssertionError(f"probe failed: {process.returncode}, {stderr}")
            second = stdout.strip()
            if first != second or first != f"11 {auuid.hex()} {live}":
                raise AssertionError(f"mapped image changed unexpectedly: {first!r}, {second!r}")
            on_disk = live.read_bytes()
            disk_uuid = on_disk[uuid_offset(on_disk):uuid_offset(on_disk) + 16]
            if before == after or (disk_uuid == auuid) != same_uuid:
                raise AssertionError("replacement control did not produce expected identity")
        print("mapped image survives path replacement; UUID collision control verified")


if __name__ == "__main__":
    main()
