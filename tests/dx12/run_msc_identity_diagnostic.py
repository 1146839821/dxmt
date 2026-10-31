#!/usr/bin/env python3
"""Run opt-in MSC identity diagnostics using an isolated converter copy.

The stage and prefix must already exist; the PE and Unix WineMetal pair and
probe must match --build. No existing files or Wine installation are changed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "converter", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    pairs = ((stage / "winemetal.dll", build / "src/winemetal/winemetal.dll"),
             (stage / "wine/lib/wine/x86_64-unix/winemetal.so", build / "src/winemetal/unix/winemetal.so"),
             (stage / "dx12_msc_identity_thunk.exe", build / "tests/dx12/dx12_msc_identity_thunk.exe"))
    for actual, expected in pairs:
        if sha(actual) != sha(expected):
            raise SystemExit(f"stage and build differ: {actual}")
    export_bytes = pe_export_bytes(pairs[0][0], "DXMTMSCGetIdentityV1")
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    copy = output / "converter.dylib"
    shutil.copyfile(converter, copy)
    environment = os.environ.copy()
    environment.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
                       WINEDLLOVERRIDES="winemetal=b", WINEDEBUG="+loaddll", DYLD_PRINT_LIBRARIES="1",
                       DXMT_METALIRCONVERTER_PATH=str(copy), DXMT_TEST_MSC_IDENTITY_SNAPSHOT="1")
    evidence = {"converter_sha256": sha(copy), "artifacts": {str(a): sha(a) for a, _ in pairs}, "runs": {}}
    for mode in ("stable", "changed"):
        stderr_path = output / (mode + ".stderr.log")
        with stderr_path.open("w") as stderr:
            process = subprocess.Popen([str(stage / "wine/bin/wine"), "dx12_msc_identity_thunk.exe", "new-wait"],
                                       cwd=stage, env=environment, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=stderr, bufsize=0)
            try:
                lines = []
                while True:
                    ready, _, _ = select.select([process.stdout], [], [], 90)
                    if not ready:
                        raise TimeoutError(f"{mode}: no wait marker")
                    line = process.stdout.readline()
                    if not line:
                        raise RuntimeError(f"{mode}: process exited before wait marker")
                    lines.append(line)
                    if b"IDENTITY_WAIT" in line:
                        break
                if mode == "changed":
                    st = copy.stat()
                    os.utime(copy, ns=(st.st_atime_ns, st.st_mtime_ns))
                rest, _ = process.communicate(b"\n", timeout=90)
                lines.append(rest)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
        stdout = b"".join(lines).decode(errors="replace")
        (output / (mode + ".stdout.log")).write_text(stdout)
        log = stderr_path.read_text(errors="replace")
        expected = "stable" if mode == "stable" else "unavailable"
        if (process.returncode != 0 or "MSC identity bridge unavailable and version checks passed" not in stdout or
                "QUERY_CODE " + export_bytes not in stdout or
                "MSC identity diagnostic snapshot bound" not in log or
                log.count("MSC identity diagnostic revalidation " + expected) < 2 or
                str(pairs[1][0]) not in log or str(copy) not in log):
            raise AssertionError(f"{mode}: expected diagnostic or loaded library evidence missing")
        evidence["runs"][mode] = {"exit": process.returncode, "revalidation": expected}
    if sha(copy) != evidence["converter_sha256"]:
        raise AssertionError("converter contents unexpectedly changed")
    (output / "summary.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("stable and changed identity diagnostics passed:", output)


if __name__ == "__main__":
    main()
