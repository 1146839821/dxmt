#!/usr/bin/env python3
"""Show why loaded-image identity cannot be inferred from UUID or path alone."""
import hashlib
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import tempfile


def run(*args, input=None):
    if args[:2] == ("xcrun", "clang"):
        args = args[:2] + ("-arch", platform.machine()) + args[2:]
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
            prior = first.split()
            current = second.split()
            if (len(prior) != 9 or len(current) != 9 or prior[:5] != current[:5] or
                    prior[:5] != ["11", auuid.hex(), original_hash, original_signature, str(live)]):
                raise AssertionError(f"mapped image changed unexpectedly: {first!r}, {second!r}")
            if prior[5:7] != prior[7:9] or current[5:7] != prior[5:7] or current[7:9] == current[5:7]:
                raise AssertionError(f"mapped and replaced file identities were not distinguished: {first!r}, {second!r}")
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
        # The signature remains unchanged but no longer seals the file. Whether
        # dyld accepts this depends on architecture and process security mode.
        invalid = subprocess.run(["codesign", "--verify", str(data_b)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if invalid.returncode == 0:
            raise AssertionError("modified fixture unexpectedly has a valid signature")
        tampered = subprocess.run([str(probe), str(data_b)], input=b"\n",
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        original = run(str(probe), str(data_a), input=b"\n").stdout.decode().splitlines()[0].split()
        if tampered.returncode == 0:
            unsafe = tampered.stdout.decode().splitlines()[0].split()
            if int(unsafe[0]) != 22 or unsafe[1:4] != original[1:4]:
                raise AssertionError("tampered image did not preserve the expected mapped identifiers")
            print("tampered signed image loaded with unchanged mapped signature")
        else:
            print("tampered signed image rejected by loader or platform")
        run("codesign", "--force", "--sign", "-", str(data_b))
        outputs = [run(str(probe), str(path), input=b"\n").stdout.decode().splitlines()[0].split()
                   for path in (data_a, data_b)]
        if [int(row[0]) for row in outputs] != [11, 22] or outputs[0][1:3] != outputs[1][1:3]:
            raise AssertionError("data-only control did not preserve UUID and __TEXT digest")
        if outputs[0][3] == outputs[1][3]:
            raise AssertionError("mapped signature did not distinguish changed initialized data")
        # In-place writes retain the inode even though the file contents differ.
        inplace = root / "inplace.dylib"
        shutil.copyfile(data_a, inplace)
        mapped = subprocess.Popen([str(probe), str(inplace)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        initial = mapped.stdout.readline().strip().split()
        if len(initial) != 9 or initial[5:7] != initial[7:9]:
            mapped.kill()
            mapped.wait()
            raise AssertionError("in-place fixture was not mapped from its original inode")
        identity_before = inplace.stat()
        hash_before = digest(inplace)
        with inplace.open("r+b") as writable:
            writable.seek(location)
            writable.write(struct.pack("<I", 22))
            writable.flush()
            os.fsync(writable.fileno())
        identity_after = inplace.stat()
        if ((identity_before.st_dev, identity_before.st_ino) != (identity_after.st_dev, identity_after.st_ino)
                or hash_before == digest(inplace)):
            raise AssertionError("in-place control did not preserve inode and alter contents")
        post_output, _ = mapped.communicate("\n", timeout=20)
        if mapped.returncode == 0:
            post = post_output.strip().split()
            if len(post) != 9 or post[5:7] != initial[5:7] or post[5:7] != post[7:9]:
                raise AssertionError("in-place mapping unexpectedly changed device or inode")
            print(f"in-place write preserved inode; marker changed {initial[0]} -> {post[0]}")
        else:
            print(f"in-place write preserved inode; mapped child exit={mapped.returncode}")
        print("mapped signatures stable across processes and distinguish changed initialized data")


if __name__ == "__main__":
    main()
