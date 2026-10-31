#!/usr/bin/env python3
"""Show that a primary dylib digest alone does not identify dependencies."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args, input=None):
    return subprocess.run(args, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)


def main():
    mapped_probe, identity_probe, identity_impl = (Path(p).resolve() for p in sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="dxmt-identity-dependency-") as temp:
        root = Path(temp).resolve()
        inspect, identity = root / "inspect", root / "identity"
        run("xcrun", "clang", "-o", str(inspect), str(mapped_probe))
        run("xcrun", "clang", "-Wno-deprecated-declarations", "-I", str(identity_impl.parent),
            "-o", str(identity), str(identity_probe), str(identity_impl))
        dependency_source = root / "dep.c"
        dependency = root / "libdependency.dylib"
        replacement = root / "replacement.dylib"
        for value, output in ((11, dependency), (22, replacement)):
            dependency_source.write_text(f"int dependency(void) {{ return {value}; }}\n")
            run("xcrun", "clang", "-dynamiclib", "-Wl,-install_name,@rpath/libdependency.dylib",
                "-o", str(output), str(dependency_source))
            run("codesign", "--force", "--sign", "-", str(output))
        src = root / "main.c"
        src.write_text("extern int dependency(void); int marker(void) { return dependency(); }\n")
        primary = root / "primary.dylib"
        run("xcrun", "clang", "-dynamiclib", "-Wl,-rpath,@loader_path", "-L", str(root),
            "-o", str(primary), str(src), "-ldependency")
        run("codesign", "--force", "--sign", "-", str(primary))
        digest = hashlib.sha256(primary.read_bytes()).hexdigest()
        results = []
        for expected in (11, 22):
            if expected == 22:
                os.replace(replacement, dependency)
            observed = run(str(inspect), str(primary), input=b"\n").stdout.decode().splitlines()[0].split()
            identity_result = run(str(identity), str(primary), "marker", input=b"\n\n").stdout.decode().splitlines()
            if int(observed[0]) != expected or identity_result != ["OPEN " + digest, "BOUND 1", "STABLE 1"]:
                raise AssertionError(f"unexpected load or identity: {observed!r} {identity_result!r}")
            results.append(observed)
        if results[0][1:4] != results[1][1:4] or hashlib.sha256(primary.read_bytes()).hexdigest() != digest:
            raise AssertionError("primary image changed during dependency-only control")
        print("primary identity remained equal while dependency changed its behavior")


if __name__ == "__main__":
    main()
