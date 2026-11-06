#!/usr/bin/env python3
"""Compare a cached path with the actual provider under an isolated override."""
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


def run(*args, env=None):
    return subprocess.run(args, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)


def observe(probe, path, env):
    lines = run(str(probe), path, env=env).stdout.decode().splitlines()
    return dict(line.split(" ", 1) for line in lines)


def main():
    source = Path(sys.argv[1]).resolve()
    system_path = "/usr/lib/libbz2.1.0.dylib"
    with tempfile.TemporaryDirectory(prefix="dxmt-cache-provider-") as temp:
        root = Path(temp).resolve()
        probe, fake, fake_source = root / "probe", root / "libbz2.1.0.dylib", root / "fake.c"
        fake_source.write_text('const char *BZ2_bzlibVersion(void) { return "DXMT_TEST_OVERRIDE"; }\n')
        arch = platform.machine()
        run("xcrun", "clang", "-arch", arch, str(source), "-o", str(probe))
        run("xcrun", "clang", "-arch", arch, "-dynamiclib", str(fake_source), "-o", str(fake))
        clean = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
        baseline = observe(probe, system_path, clean)
        override = observe(probe, system_path, {**clean, "DYLD_LIBRARY_PATH": str(root)})
        if baseline["VALUE"] == "DXMT_TEST_OVERRIDE":
            raise AssertionError("control unexpectedly selected test library")
        if override["VALUE"] != "DXMT_TEST_OVERRIDE" or Path(override["PROVIDER"]).resolve() != fake:
            print("SKIP: loader policy did not select the isolated override")
            return 77
        if baseline["CACHE"] != override["CACHE"]:
            raise AssertionError("cache membership changed between controls")
        print(f"cache={baseline['CACHE']} baseline_provider={baseline['PROVIDER']}")
        print(f"override_provider={override['PROVIDER']} override_value={override['VALUE']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
