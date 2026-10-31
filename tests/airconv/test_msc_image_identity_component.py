#!/usr/bin/env python3
"""Exercise candidate native identity code with real dyld file replacements."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from test_darwin_loaded_image_identity import data_section
from test_darwin_pinned_image import advance, finish, run, start


def main():
    driver, component = map(lambda p: Path(p).resolve(), sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="dxmt-identity-component-") as directory:
        root = Path(directory).resolve()
        probe = root / "probe"
        run("xcrun", "clang", "-Wall", "-Wextra", "-Wno-deprecated-declarations", "-I", str(component.parent),
            "-o", str(probe), str(driver), str(component))
        for name, value in (("a", 11), ("b", 22)):
            source = root / (name + ".c")
            source.write_text(f"int value = {value}; int marker(void) {{ return value; }}\n")
            run("xcrun", "clang", "-dynamiclib", "-o", str(root / (name + ".dylib")), str(source))
            run("codesign", "--force", "--sign", "-", str(root / (name + ".dylib")))
        original, other, live = root / "a.dylib", root / "b.dylib", root / "live.dylib"
        shutil.copyfile(original, live)
        p = start(probe, live)
        if advance(p) != "BOUND 1" or finish(p) != "STABLE 1":
            raise AssertionError("unchanged loaded image did not validate")
        shutil.copyfile(original, live)
        p = start(probe, live)
        replacement = root / "replacement.dylib"
        shutil.copyfile(other, replacement)
        os.replace(replacement, live)
        if advance(p) != "BOUND 0":
            raise AssertionError("replacement before load was accepted")
        if finish(p) != "STABLE 0":
            raise AssertionError("unbound identity was accepted during revalidation")
        shutil.copyfile(original, live)
        p = start(probe, live)
        if advance(p) != "BOUND 1":
            raise AssertionError("initial image binding failed")
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
            raise AssertionError("in-place mutation was accepted")
        for invalid in (root, root / "missing"):
            result = subprocess.run([str(probe), str(invalid), "marker"], input=b"\n\n",
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode != 2 or b"BOUND 1" in result.stdout:
                raise AssertionError("invalid input produced an accepted identity")
        print("candidate identity component accepted stable and rejected modified images")


if __name__ == "__main__":
    main()
