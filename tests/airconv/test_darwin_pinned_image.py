#!/usr/bin/env python3
"""Exercise a pre-opened file/mapping binding prototype under replacement."""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from test_darwin_loaded_image_identity import data_section


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def start(probe, live):
    process = subprocess.Popen([str(probe), str(live), "marker"], stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    opening = process.stdout.readline().strip()
    expected = hashlib.sha256(live.read_bytes()).hexdigest()
    if opening != "OPEN " + expected:
        raise AssertionError(f"bad initial digest: {opening}")
    return process


def advance(process):
    process.stdin.write("\n")
    process.stdin.flush()
    return process.stdout.readline().strip()


def finish(process):
    output, error = process.communicate("\n", timeout=20)
    if process.returncode:
        raise AssertionError(f"probe failed ({process.returncode}): {error}")
    return output.strip()


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="dxmt-pinned-image-") as directory:
        root = Path(directory).resolve()
        probe = root / "probe"
        run("xcrun", "clang", "-Wno-deprecated-declarations", "-o", str(probe), str(source))
        for name, value in (("a", 11), ("b", 22)):
            src = root / (name + ".c")
            src.write_text(f"int value = {value}; int marker(void) {{ return value; }}\n")
            run("xcrun", "clang", "-dynamiclib", "-o", str(root / (name + ".dylib")), str(src))
            run("codesign", "--force", "--sign", "-", str(root / (name + ".dylib")))
        original, other, live = root / "a.dylib", root / "b.dylib", root / "live.dylib"

        shutil.copyfile(original, live)
        p = start(probe, live)
        if advance(p) != "BOUND 1" or finish(p) != "STABLE 1":
            raise AssertionError("unchanged file did not bind stably")

        alias = root / "alias.dylib"
        alias.symlink_to(original)
        p = start(probe, alias)
        if advance(p) != "BOUND 1" or finish(p) != "STABLE 1":
            raise AssertionError("symlink alias did not bind stably")

        shutil.copyfile(original, live)
        p = start(probe, live)
        replacement = root / "replacement.dylib"
        shutil.copyfile(other, replacement)
        os.replace(replacement, live)
        if advance(p) != "BOUND 0":
            raise AssertionError("replacement before dlopen was not rejected")
        finish(p)

        shutil.copyfile(original, live)
        p = start(probe, live)
        if advance(p) != "BOUND 1":
            raise AssertionError("mapping did not bind before mutation")
        initial = live.stat()
        image = live.read_bytes()
        offset, size = data_section(image)
        position = image.find(struct.pack("<I", 11), offset, offset + size)
        if position < 0:
            raise AssertionError("data fixture missing")
        with live.open("r+b") as stream:
            stream.seek(position)
            stream.write(struct.pack("<I", 22))
            stream.flush()
            os.fsync(stream.fileno())
        os.utime(live, ns=(initial.st_atime_ns, initial.st_mtime_ns))
        if finish(p) != "STABLE 0":
            raise AssertionError("in-place modification escaped status and hash checks")
        print("pre-open binding accepts stable image and rejects both replacement controls")


if __name__ == "__main__":
    main()
