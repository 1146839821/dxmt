#!/usr/bin/env python3
"""Check whether the candidate file identity can cover system dylibs.

This is an observation of explicitly selected images, not a closure scanner.
"""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    driver, component = (Path(p).resolve() for p in sys.argv[1:])
    libraries = (
        ("/usr/lib/libbz2.1.0.dylib", "BZ2_bzlibVersion"),
        ("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", "CFGetTypeID"),
        ("/usr/lib/libarchive.2.dylib", "archive_version_number"),
        ("/usr/lib/libc++.1.dylib", "__cxa_throw"),
        ("/usr/lib/libSystem.B.dylib", "malloc"),
    )
    with tempfile.TemporaryDirectory(prefix="dxmt-system-identity-") as tmp:
        root = Path(tmp)
        probe, src, fixture = root / "probe", root / "fixture.c", root / "fixture.dylib"
        run("xcrun", "clang", "-Wno-deprecated-declarations", "-I", str(component.parent),
            str(driver), str(component), "-o", str(probe))
        src.write_text("int marker(void) { return 7; }\n")
        run("xcrun", "clang", "-dynamiclib", str(src), "-o", str(fixture))
        positive = subprocess.run([str(probe), str(fixture), "marker"], input=b"\n\n",
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20)
        if positive.returncode or b"BOUND 1\nSTABLE 1\n" not in positive.stdout:
            raise AssertionError("file-backed control did not bind and revalidate")
        msc_path = os.environ.get("DXMT_TEST_MSC_SYSTEM_DEPENDENCY_ROOT")
        if msc_path:
            msc = ctypes.CDLL(msc_path, mode=os.RTLD_LOCAL | os.RTLD_NOW)
            print(f"MSC root loaded: {msc_path}")
        for path, symbol in libraries:
            library = ctypes.CDLL(path, mode=os.RTLD_LOCAL | os.RTLD_NOW)
            if not getattr(library, symbol):
                raise AssertionError(f"missing symbol {symbol} in {path}")
            result = subprocess.run([str(probe), path, symbol], input=b"\n\n", stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=20)
            if not os.path.isfile(path):
                if result.returncode != 2:
                    raise AssertionError(f"non-file path unexpectedly accepted: {path}")
                status = "LOADED FILE_UNAVAILABLE"
            elif result.returncode == 0:
                status = result.stdout.decode().strip().replace("\n", " ")
            else:
                status = f"LOADED PROBE_UNAVAILABLE exit={result.returncode}"
            print(f"{path}: {status}")


if __name__ == "__main__":
    main()
