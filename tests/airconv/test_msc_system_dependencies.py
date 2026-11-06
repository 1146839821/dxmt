#!/usr/bin/env python3
"""Check whether the candidate file identity can cover system dylibs.

This is an observation of explicitly selected images, not a closure scanner.
"""
import ctypes
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    driver, component, cache_driver = (Path(p).resolve() for p in sys.argv[1:])
    libraries = (
        ("/usr/lib/libbz2.1.0.dylib", "BZ2_bzlibVersion"),
        ("/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation", "CFGetTypeID"),
        ("/usr/lib/libarchive.2.dylib", "archive_version_number"),
        ("/usr/lib/libc++.1.dylib", "__cxa_throw"),
        ("/usr/lib/libSystem.B.dylib", "malloc"),
    )
    process = ctypes.CDLL(None)
    try:
        cache_contains = process._dyld_shared_cache_contains_path
        cache_contains.argtypes, cache_contains.restype = [ctypes.c_char_p], ctypes.c_bool
        image_header = process._dyld_get_dlopen_image_header
        image_header.argtypes, image_header.restype = [ctypes.c_void_p], ctypes.c_void_p
        region_filename = process.proc_regionfilename
        region_filename.argtypes = [ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32]
        region_filename.restype = ctypes.c_int
    except AttributeError:
        cache_contains = image_header = region_filename = None
    with tempfile.TemporaryDirectory(prefix="dxmt-system-identity-") as tmp:
        root = Path(tmp)
        probe, src, fixture = root / "probe", root / "fixture.c", root / "fixture.dylib"
        arch = platform.machine()
        run("xcrun", "clang", "-arch", arch, "-Wno-deprecated-declarations", "-I", str(component.parent),
            str(driver), str(component), "-o", str(probe))
        src.write_text("int marker(void) { return 7; }\n")
        run("xcrun", "clang", "-arch", arch, "-dynamiclib", str(src), "-o", str(fixture))
        cache_probe = root / "cache-probe"
        run("xcrun", "clang", "-arch", arch, str(cache_driver), "-o", str(cache_probe))
        snapshots = [run(str(cache_probe), str(fixture)).stdout.strip() for _ in range(2)]
        if snapshots[0] != b"UNAVAILABLE" and snapshots[1] != b"UNAVAILABLE" and snapshots[0] != snapshots[1]:
            raise AssertionError("shared-cache UUID changed across identical probes")
        print(f"shared-cache UUID observations: {snapshots[0].decode()} {snapshots[1].decode()}")
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
            provenance = "cache_api=UNAVAILABLE"
            if cache_contains and image_header and region_filename:
                header = image_header(library._handle)
                if not header:
                    raise AssertionError(f"no image header for {path}")
                buffer = ctypes.create_string_buffer(4096)
                length = region_filename(os.getpid(), header, buffer, len(buffer))
                mapped_path = os.fsdecode(buffer.value) if length > 0 else "UNAVAILABLE"
                provenance = f"cache_contains={int(cache_contains(os.fsencode(path)))} mapping={mapped_path}"
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
            print(f"{path}: {status} {provenance}")


if __name__ == "__main__":
    main()
