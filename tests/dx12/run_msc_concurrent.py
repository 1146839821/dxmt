#!/usr/bin/env python3
"""Measure real MSC calls for simultaneous identical compute pipelines."""
import argparse
import hashlib
import json
import os
import re
import statistics
from pathlib import Path
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
    parser.add_argument("--expected-calls", type=int, required=True)
    parser.add_argument("--fail-first", action="store_true")
    parser.add_argument("--two-shaders", action="store_true")
    parser.add_argument("--force-serial", action="store_true")
    parser.add_argument("--no-delay", action="store_true", help="omit the test-only compiler delay")
    args = parser.parse_args()
    if args.fail_first and args.two_shaders:
        parser.error("--fail-first and --two-shaders are separate controls")
    if args.force_serial and not args.two_shaders:
        parser.error("--force-serial requires --two-shaders")
    if args.no_delay and (args.fail_first or args.two_shaders or args.force_serial):
        parser.error("--no-delay is only supported by the identical-shader timing case")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    repository = Path(__file__).resolve().parents[2]
    pairs = (("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_msc_concurrent.exe", "dx12_msc_concurrent.exe"),
             ("tests/dx12/shader_embedded.compute.cs.cso", "shader_embedded.compute.cs.cso"))
    if args.two_shaders:
        pairs += (("tests/dx12/shader_embedded.compute.second.cs.cso", "shader_embedded.compute.second.cs.cso"),)
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
    env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_") and
           not key.startswith("DXMT_TEST_TRACE_")}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(wrapper), DYLD_PRINT_LIBRARIES="1",
               DXMT_SHADER_CACHE_PATH=str(output / "cache"), DXMT_LOG_PATH=str(output))
    if not args.no_delay:
        env["DXMT_TEST_TRACE_DELAY"] = "1"
    if args.fail_first:
        env["DXMT_TEST_TRACE_FAIL_FIRST"] = "1"
    if args.force_serial:
        env["DXMT_TEST_TRACE_FORCE_SERIAL"] = "1"
    command = [str(stage / "wine/bin/wine"), "dx12_msc_concurrent.exe", "shader_embedded.compute.cs.cso"]
    if args.fail_first:
        command.append("--fail-first")
    if args.two_shaders:
        command.extend(("--second", "shader_embedded.compute.second.cs.cso"))
    result = subprocess.run(command,
                            cwd=stage, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    log = result.stdout.decode(errors="replace")
    (output / "run.log").write_text(log)
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    loaded = []
    for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
        path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
        loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                          for line in log.splitlines()))
    calls = log.count("DXMT_MSC_TRACE_END ok")
    timings = {}
    for phase in ("CONCURRENT", "REPEAT"):
        matches = re.findall(r'^PSO_' + phase + r'_NS (\d+) (\d+)\s*$', log, re.M)
        if [int(index) for index, _ in matches] != list(range(8)):
            raise AssertionError(f"missing {phase} PSO timing samples")
        values = [int(value) for _, value in matches]
        if any(value <= 0 for value in values):
            raise AssertionError(f"invalid {phase} PSO timing sample")
        ordered = sorted(values)
        timings[phase.lower()] = {"samples_ns": values, "median_ns": statistics.median(values),
                                  "p95_nearest_rank_ns": ordered[-1],
                                  "min_ns": ordered[0], "max_ns": ordered[-1]}
    after_concurrent = log.split("PSO_CONCURRENT_NS 7 ", 1)[1]
    active = max_active = 0
    for line in log.splitlines():
        if line == "DXMT_MSC_TRACE_BEGIN":
            active += 1
            max_active = max(max_active, active)
        elif line.startswith("DXMT_MSC_TRACE_END ") or line == "DXMT_MSC_TRACE_FORCED_FAILURE":
            active -= 1
    expected_failures = 1 if args.fail_first else 0
    if (result.returncode or f"concurrent MSC PSOs=8 failures={expected_failures}" not in log or
            any(value not in log for value in identities) or not all(loaded) or
            str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") not in log or
            str(wrapper) not in log or str(converter) not in log or calls != args.expected_calls or
            "DXMT_MSC_TRACE_BEGIN" in after_concurrent or
            log.count("DXMT_MSC_TRACE_FORCED_FAILURE") != expected_failures or active != 0 or
            (args.two_shaders and not args.force_serial and max_active < 2) or
            (args.force_serial and max_active != 1)):
        raise AssertionError(f"concurrent test failed: exit={result.returncode}, compile calls={calls}")
    (output / "summary.json").write_text(json.dumps({"compile_calls": calls, "max_overlapping_calls": max_active,
        "exit": result.returncode, "injected_delay": not args.no_delay, "pso_creation_timing": timings,
        "converter_sha256": sha(converter), "wrapper_sha256": sha(wrapper),
        "artifacts": {path: sha(stage / path) for _, path in pairs}}, indent=2) + "\n")
    print(f"eight concurrent PSOs: {calls} real MSC compilation calls")


if __name__ == "__main__":
    main()
