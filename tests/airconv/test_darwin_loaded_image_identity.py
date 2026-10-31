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


def data_section(data):
    count = struct.unpack_from("<I", data, 16)[0]
    offset = 32
    for _ in range(count):
        kind, length = struct.unpack_from("<2I", data, offset)
        if kind == 0x19:
            sections = struct.unpack_from("<I", data, offset + 64)[0]
            for i in range(sections):
                section = offset + 72 + i * 80
                name = bytes(data[section:section + 16]).rstrip(b"\0")
                segment = bytes(data[section + 16:section + 32]).rstrip(b"\0")
                if name == b"__data" and segment == b"__DATA":
                    size = struct.unpack_from("<Q", data, section + 40)[0]
                    file_offset = struct.unpack_from("<I", data, section + 48)[0]
                    return file_offset, size
        offset += length
    raise ValueError("data section missing")


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
            run("codesign", "--force", "--sign", "-", str(root / f"{name}.dylib"))
        a = (root / "a.dylib").read_bytes()
        b = bytearray((root / "b.dylib").read_bytes())
        auuid = a[uuid_offset(a):uuid_offset(a) + 16]
        buuid = b[uuid_offset(b):uuid_offset(b) + 16]
        if auuid == buuid:
            raise AssertionError("test libraries unexpectedly have identical UUIDs")
        controls = [run(str(probe), str(root / "a.dylib"), input=b"\n").stdout.decode().splitlines()[0]
                    for _ in range(2)]
        original_hash = controls[0].split()[2]
        original_signature = controls[0].split()[3]
        if controls[0] != controls[1]:
            raise AssertionError("mapped text fingerprint changed between processes")
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
            replacement_hash = control.stdout.decode().splitlines()[0].split()[2]
            if replacement_hash == original_hash:
                raise AssertionError("different executable code has the same text fingerprint")
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
            if first != second or first != f"11 {auuid.hex()} {original_hash} {original_signature} {live}":
                raise AssertionError(f"mapped image changed unexpectedly: {first!r}, {second!r}")
            on_disk = live.read_bytes()
            disk_uuid = on_disk[uuid_offset(on_disk):uuid_offset(on_disk) + 16]
            if before == after or (disk_uuid == auuid) != same_uuid:
                raise AssertionError("replacement control did not produce expected identity")
        # Same binary and UUID, with one initialized-data value changed.
        source = root / "data.c"
        source.write_text("int value = 11; int marker(void) { return value; }\n")
        data_a = root / "data-a.dylib"
        data_b = root / "data-b.dylib"
        run("xcrun", "clang", "-dynamiclib", "-o", str(data_a), str(source))
        run("codesign", "--force", "--sign", "-", str(data_a))
        image = bytearray(data_a.read_bytes())
        offset, size = data_section(image)
        location = image.find(struct.pack("<I", 11), offset, offset + size)
        if location < 0:
            raise AssertionError("initialized data fixture not found")
        image[location:location + 4] = struct.pack("<I", 22)
        data_b.write_bytes(image)
        run("codesign", "--force", "--sign", "-", str(data_b))
        outputs = [run(str(probe), str(path), input=b"\n").stdout.decode().splitlines()[0].split()
                   for path in (data_a, data_b)]
        if [int(row[0]) for row in outputs] != [11, 22] or outputs[0][1:3] != outputs[1][1:3]:
            raise AssertionError("data-only control did not preserve UUID and __TEXT digest")
        if outputs[0][3] == outputs[1][3]:
            raise AssertionError("mapped signature did not distinguish changed initialized data")
        print("mapped signatures stable across processes and distinguish changed initialized data")


if __name__ == "__main__":
    main()
