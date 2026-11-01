#!/usr/bin/env python3
"""Compile independent test images and exercise the dyld event counter."""
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


def run(*args):
    if args[:2] == ("xcrun", "clang"):
        args = args[:2] + ("-arch", platform.machine()) + args[2:]
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    driver, source = (Path(p).resolve() for p in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="dxmt-dyld-epoch-") as temp:
        root = Path(temp).resolve()
        binary = root / "probe"
        run("xcrun", "clang", "-std=c11", "-DDXMT_IMAGE_EPOCH_TESTING", "-I", str(source.parent),
            str(driver), str(source), "-o", str(binary))
        images = []
        for i in range(6):
            src, lib = root / f"{i}.c", root / f"{i}.dylib"
            src.write_text(f"int marker_{i}(void) {{ return {i}; }}\n")
            run("xcrun", "clang", "-dynamiclib", str(src), "-o", str(lib))
            images.append(str(lib))
        result = run(str(binary), *images)
        print(result.stdout.decode().strip())


if __name__ == "__main__":
    main()
