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
import re
import select
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def descriptor_paths(pid):
    listing = subprocess.run(["lsof", "-a", "-p", str(pid), "-Fn"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=20, check=True)
    current = None
    paths = set()
    for field in listing.stdout.decode(errors="replace").splitlines():
        if field.startswith("f"):
            current = field[1:]
        elif field.startswith("n") and current and re.fullmatch(r"[0-9]+[a-zA-Z]*", current):
            paths.add(field[1:])
    if not paths:
        raise AssertionError("could not inspect any file descriptors")
    return paths


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

    invalid = output / "invalid.dylib"
    invalid.write_bytes(b"not a dynamic library\n")
    dummy_source = output / "dummy.c"
    dummy_source.write_text("int unrelated(void) { return 1; }\n")
    dummy = output / "missing-symbols.dylib"
    subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", "-o", str(dummy), str(dummy_source)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for mode, arg, selected in (("concurrent", "concurrent", copy), ("fallback", "new", invalid),
                                ("missing-symbols", "unavailable", dummy)):
        run_environment = environment.copy()
        run_environment["DXMT_METALIRCONVERTER_PATH"] = str(selected)
        result = subprocess.run([str(stage / "wine/bin/wine"), "dx12_msc_identity_thunk.exe", arg],
                                cwd=stage, env=run_environment, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=120)
        stdout, log = result.stdout.decode(errors="replace"), result.stderr.decode(errors="replace")
        (output / (mode + ".stdout.log")).write_text(stdout)
        (output / (mode + ".stderr.log")).write_text(log)
        if (result.returncode != 0 or "QUERY_CODE " + export_bytes not in stdout or
                str(pairs[1][0]) not in log):
            raise AssertionError(f"{mode}: exit or loaded bridge evidence incorrect")
        if mode == "concurrent":
            if ("Concurrent MSC identity queries passed" not in stdout or
                    log.count("MSC identity diagnostic snapshot bound") != 1 or
                    log.count("MSC identity diagnostic revalidation stable") != 8 or str(copy) not in log):
                raise AssertionError("concurrent query evidence missing")
        elif mode == "fallback":
            if ("MSC identity bridge unavailable and version checks passed" not in stdout or
                    "failed to load DXMT_METALIRCONVERTER_PATH" not in log or
                    "MSC identity diagnostic snapshot unavailable" not in log or
                    log.count("MSC identity diagnostic revalidation unavailable") < 2):
                raise AssertionError("invalid path fallback evidence missing")
        elif ("MSC unavailable path passed" not in stdout or
              "Metal Shader Converter is missing symbol" not in log or str(dummy) not in log):
            raise AssertionError("missing symbol rejection evidence missing")
        evidence["runs"][mode] = {"exit": result.returncode}

    # The handle may resolve required MSC symbols from a dependent image.
    # The root file must not be accepted as the provider in that case.
    forward_source = output / "forward.c"
    forward_source.write_text("extern void *IRCompilerCreate(void); void *force_dependency(void) { return IRCompilerCreate(); }\n")
    forward = output / "forward.dylib"
    subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", str(forward_source), str(converter),
                    "-Wl,-rpath," + str(converter.parent), "-o", str(forward)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run(["codesign", "--force", "--sign", "-", str(forward)],
                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    defined = subprocess.check_output(["nm", "-gU", str(forward)], text=True)
    undefined = subprocess.check_output(["nm", "-u", str(forward)], text=True)
    if "_IRCompilerCreate" in defined or "_IRCompilerCreate" not in undefined:
        raise AssertionError("wrapper unexpectedly defines the converter symbol")
    forward_env = environment.copy()
    forward_env["DXMT_METALIRCONVERTER_PATH"] = str(forward)
    result = subprocess.run([str(stage / "wine/bin/wine"), "dx12_msc_identity_thunk.exe", "new"],
                            cwd=stage, env=forward_env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=120)
    stdout, log = result.stdout.decode(errors="replace"), result.stderr.decode(errors="replace")
    (output / "forward.stdout.log").write_text(stdout)
    (output / "forward.stderr.log").write_text(log)
    if (result.returncode != 0 or "QUERY_CODE " + export_bytes not in stdout or
            "MSC identity bridge unavailable and version checks passed" not in stdout or
            str(pairs[1][0]) not in log or str(forward) not in log or str(converter) not in log or
            "MSC identity diagnostic snapshot unavailable" not in log or
            log.count("MSC identity diagnostic revalidation unavailable") < 2):
        raise AssertionError("dependency-provided converter symbol was not safely rejected")
    evidence["runs"]["dependency_provider"] = {"exit": result.returncode, "wrapper_sha256": sha(forward)}

    # First establish that lsof observes the deliberately retained snapshot.
    with (output / "descriptor-positive.stderr.log").open("w") as stderr:
        process = subprocess.Popen([str(stage / "wine/bin/wine"), "dx12_msc_identity_thunk.exe", "new-wait"],
                                   cwd=stage, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=stderr, bufsize=0)
        try:
            while True:
                ready, _, _ = select.select([process.stdout], [], [], 90)
                if not ready:
                    raise TimeoutError("descriptor positive: no wait marker")
                line = process.stdout.readline()
                if not line:
                    raise RuntimeError("descriptor positive: process exited")
                if b"IDENTITY_WAIT" in line:
                    break
            if str(copy) not in descriptor_paths(process.pid):
                raise AssertionError("retained snapshot descriptor was not observed")
            process.communicate(b"\n", timeout=90)
            if process.returncode != 0:
                raise AssertionError("descriptor positive process failed")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    # Keep the failed-loader process alive while checking its open descriptors.
    environment["DXMT_METALIRCONVERTER_PATH"] = str(dummy)
    with (output / "descriptor.stderr.log").open("w") as stderr:
        process = subprocess.Popen([str(stage / "wine/bin/wine"), "dx12_msc_identity_thunk.exe", "unavailable-wait"],
                                   cwd=stage, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=stderr, bufsize=0)
        try:
            lines = []
            while True:
                ready, _, _ = select.select([process.stdout], [], [], 90)
                if not ready:
                    raise TimeoutError("descriptor: no wait marker")
                line = process.stdout.readline()
                if not line:
                    raise RuntimeError("descriptor: process exited before wait marker")
                lines.append(line)
                if b"UNAVAILABLE_WAIT" in line:
                    break
            if str(dummy) in descriptor_paths(process.pid):
                raise AssertionError("failed-loader converter descriptor remains open")
            rest, _ = process.communicate(b"\n", timeout=90)
            lines.append(rest)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    stdout = b"".join(lines).decode(errors="replace")
    (output / "descriptor.stdout.log").write_text(stdout)
    log = (output / "descriptor.stderr.log").read_text(errors="replace")
    if (process.returncode != 0 or "QUERY_CODE " + export_bytes not in stdout or
            f"dyld[{process.pid}]" not in log or str(dummy) not in log or
            "Metal Shader Converter is missing symbol" not in log):
        raise AssertionError("failed-loader descriptor control lacks identity evidence")
    evidence["runs"]["descriptor_cleanup"] = {"exit": process.returncode, "pid": process.pid}
    if sha(copy) != evidence["converter_sha256"]:
        raise AssertionError("converter contents unexpectedly changed")
    (output / "summary.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print("identity diagnostics, concurrency and loader failure controls passed:", output)


if __name__ == "__main__":
    main()
