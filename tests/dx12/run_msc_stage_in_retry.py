#!/usr/bin/env python3
"""Verify an injected stage-in size increase through Wine and GPU readback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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
    parser.add_argument("--mode", choices=("baseline", "grow", "metallib"), required=True)
    parser.add_argument("--small-metallib", type=Path)
    args = parser.parse_args()
    if (args.mode == "metallib") != bool(args.small_metallib):
        parser.error("--small-metallib is required only for metallib mode")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[2]
    pairs = (("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_graphics_sm6.exe", "dx12_graphics_sm6.exe"))
    for relative, deployed in pairs:
        built = (build / relative).read_bytes()
        if relative in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and built[64:80] == b"Wine builtin DLL":
            built = built[:64] + b" " * 16 + built[80:]
        if built != (stage / deployed).read_bytes():
            raise AssertionError(f"stage/build mismatch: {relative}")
    wrapper = output / "wrapper.dylib"
    subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-dynamiclib", "-I", str(repository / "include"),
                    str(repository / "tests/dx12/msc_conversion_trace_wrapper.c"), str(converter),
                    "-Wl,-rpath," + str(converter.parent), "-o", str(wrapper)], check=True, timeout=30)
    symbols = (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in symbols]
    env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_")}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(wrapper), DXMT_TEST_PRINT_LOADED_CODE="1",
               DXMT_SHADER_CACHE_PATH=str(output / "cache"), DXMT_LOG_PATH=str(output),
               MTL_DEBUG_LAYER="1", MTL_SHADER_VALIDATION="1", MTL_SHADER_VALIDATION_REPORT_TO_STDERR="1",
               DYLD_PRINT_LIBRARIES="1")
    env.pop("DXMT_TEST_TRACE_GROW_ENTRY", None)
    env.pop("DXMT_TEST_TRACE_LATE_IMAGE", None)
    if args.mode == "grow":
        env["DXMT_TEST_TRACE_GROW_STAGE_IN"] = "1"
    else:
        env.pop("DXMT_TEST_TRACE_GROW_STAGE_IN", None)
    if args.mode == "metallib":
        small = args.small_metallib.resolve(strict=True)
        verifier = output / "verify-metallib"
        subprocess.run(["xcrun", "clang", "-arch", "x86_64", "-framework", "Foundation", "-framework", "Metal",
                        str(repository / "tests/dx12/validate_test_metallib.m"), "-o", str(verifier)],
                       check=True, timeout=30)
        result = subprocess.run([str(verifier), str(small)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, check=True)
        (output / "verify-metallib.log").write_bytes(result.stdout)
        env["DXMT_TEST_TRACE_SMALL_METALLIB"] = str(small)
    else:
        env.pop("DXMT_TEST_TRACE_SMALL_METALLIB", None)
    command = [str(stage / "wine/bin/wine"), "dx12_graphics_sm6.exe", "--gs-differential",
               "geometry_differential_3.vs.cso", "geometry_differential_3.ps.cso",
               "geometry_differential_3.gs.cso", "triangle", "none"]
    results = []
    for label, option, expected_exit in (("readback", [], 0), ("wrong", ["--wrong-expected"], 1)):
        result = subprocess.run(command + option, cwd=stage, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        log = result.stdout.decode(errors="replace")
        (output / (label + ".log")).write_text(log)
        loaded = []
        for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
            path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
            loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                              for line in log.splitlines()))
        if (result.returncode != expected_exit or any(item not in log for item in identities) or
                not all(loaded) or
                str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") not in log or str(wrapper) not in log or
                str(converter) not in log or "Metal API Validation Enabled" not in log or
                "Metal GPU Validation Enabled" not in log or
                re.search(r"(?i)(metal (?:api|gpu|shader) validation error|validation error:|invalid metal usage:)", log)):
            raise AssertionError(f"{label}: execution, identity or validation failed")
        if label == "readback" and (log.count("exact_bytes=3072 passed") != 3 or "released refs passed" not in log):
            raise AssertionError("exact geometry readback did not pass")
        if label == "wrong" and "FAIL GS kind=triangle index=none pass=0 byte=0 got=1 expected=0" not in log:
            raise AssertionError("deliberately wrong readback was not rejected")
        injected = "DXMT_MSC_TRACE_ZERO_STAGE_IN" in log
        if injected != (args.mode == "grow"):
            raise AssertionError(f"{label}: stage-in size control was not observed")
        small_injected = "DXMT_MSC_TRACE_SMALL_METALLIB" in log
        if small_injected != (args.mode == "metallib"):
            raise AssertionError(f"{label}: metallib size control was not observed")
        if log.count("DXMT_MSC_TRACE_END ok") < (7 if injected or small_injected else 6):
            raise AssertionError(f"{label}: expected compilation and retry count was not observed")
        results.append({"case": label, "exit": result.returncode, "stage_in_injected": injected,
                        "metallib_injected": small_injected,
                        "successful_conversions": log.count("DXMT_MSC_TRACE_END ok")})
    (output / "summary.json").write_text(json.dumps({"mode": args.mode, "converter_sha256": sha(converter),
        "wrapper_sha256": sha(wrapper), "artifacts": {path: sha(stage / path) for _, path in pairs},
        "fixtures": {name: sha(stage / name) for name in command[3:6]},
        "small_metallib_sha256": sha(small) if args.mode == "metallib" else None, "results": results}, indent=2) + "\n")
    print("MSC stage-in", args.mode, "passed:", output)


if __name__ == "__main__":
    main()
