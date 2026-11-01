#!/usr/bin/env python3
"""Trace actual Wine/DXMT MSC compilation with a test-only forwarding dylib."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "converter", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", action="append", choices=("baseline", "late", "grow"))
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    root = args.output.absolute()
    root.mkdir(parents=True, exist_ok=False)
    # Native overrides need the Wine builtin DOS-stub marker removed.  Verify
    # this isolated deployment differs from the build only by that marker.
    snapshot = root / "verified-build"
    for relative, deployed in (("src/d3d12/d3d12.dll", stage / "d3d12.dll"),
                               ("src/dxgi/dxgi.dll", stage / "dxgi.dll"),
                               ("src/winemetal/winemetal.dll", stage / "winemetal.dll"),
                               ("src/winemetal/unix/winemetal.so", stage / "wine/lib/wine/x86_64-unix/winemetal.so"),
                               ("tests/dx12/dx12_texture_lod_clamp.exe", stage / "dx12_texture_lod_clamp.exe")):
        built = (build / relative).read_bytes()
        actual = deployed.read_bytes()
        if relative in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and built[64:80] == b"Wine builtin DLL":
            built = built[:64] + b" " * 16 + built[80:]
        if actual != built:
            raise AssertionError(f"deployed artifact differs from build: {relative}")
        target = snapshot / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(deployed, target)
    repository = Path(__file__).resolve().parents[2]
    wrapper_source = repository / "tests/dx12/msc_conversion_trace_wrapper.c"
    wrapper, late, source = root / "wrapper.dylib", root / "late.dylib", root / "late.c"
    source.write_text("int dxmt_test_late_marker(void) { return 42; }\n")
    for command in (["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", str(source), "-o", str(late)],
                    ["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", "-I", str(repository / "include"),
                     str(wrapper_source), str(converter), "-Wl,-rpath," + str(converter.parent), "-o", str(wrapper)]):
        subprocess.run(command, check=True, timeout=30)
    base = [sys.executable, str(repository / "tests/dx12/run_msc_lod_experiment.py"),
            "--stage", str(stage), "--prefix", str(prefix), "--build", str(snapshot),
            "--fragment-ps", "lod_ps.cso", "--fragment-vs", "lod_vs.cso",
            "--case", "fragment", "--case", "fragment-control"]
    observations = {}
    for mode in args.mode or ("baseline", "late", "grow"):
        logs = root / mode
        env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_") and
               not key.startswith("DXMT_TEST_TRACE_")}
        env["DXMT_METALIRCONVERTER_PATH"] = str(wrapper)
        if mode == "late":
            env["DXMT_TEST_TRACE_LATE_IMAGE"] = str(late)
        else:
            env.pop("DXMT_TEST_TRACE_LATE_IMAGE", None)
        if mode == "grow":
            env["DXMT_TEST_TRACE_GROW_ENTRY"] = "1"
        else:
            env.pop("DXMT_TEST_TRACE_GROW_ENTRY", None)
        result = subprocess.run(base + ["--logs", str(logs)], env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, text=True)
        (root / (mode + ".runner.log")).write_text(result.stdout)
        if result.returncode:
            raise AssertionError(f"{mode}: DXMT/Wine regression failed; see runner log")
        directories = list(logs.glob("msc-lod-*"))
        if len(directories) != 1:
            raise AssertionError(f"{mode}: expected one isolated run")
        log = (directories[0] / "fragment.log").read_text(errors="replace")
        if str(wrapper) not in log or str(converter) not in log or "DXMT_MSC_TRACE_LATE_FAILED" in log:
            raise AssertionError(f"{mode}: missing loaded-image evidence")
        intervals = re.findall(r"DXMT_MSC_TRACE_BEGIN\n(.*?)DXMT_MSC_TRACE_END ok", log, flags=re.S)
        if not intervals:
            raise AssertionError(f"{mode}: no successful real conversion was observed")
        if mode == "grow" and ("DXMT_MSC_TRACE_SHORT_ENTRY" not in log or len(intervals) < 5):
            raise AssertionError("grow: no forced undersized query and retry were observed")
        late_in_interval = any(str(late) in interval for interval in intervals)
        if late_in_interval != (mode == "late"):
            raise AssertionError(f"{mode}: unexpected late-load observation")
        dyld_lines = [line for interval in intervals for line in interval.splitlines() if re.match(r"dyld\[\d+\]:", line)]
        observations[mode] = {"successful_conversions": len(intervals), "dyld_lines": dyld_lines,
                              "late_image_observed": late_in_interval,
                              "runtime_summary": str(directories[0] / "summary.json")}
    (root / "summary.json").write_text(json.dumps({"converter_sha256": sha(converter), "wrapper_sha256": sha(wrapper),
                                                    "late_sha256": sha(late), "observations": observations}, indent=2) + "\n")
    print("Wine/DXMT MSC conversion trace and injected late-load control passed:", root)


if __name__ == "__main__":
    main()
