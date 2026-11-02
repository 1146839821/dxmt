#!/usr/bin/env python3
"""Measure complete compute PSO calls; cold means first call in a new process."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
from run_msc_lod_experiment import pe_export_bytes


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summary(values):
    ordered = sorted(values)
    return {"count": len(values), "median_ms": statistics.median(values),
            "p95_ms": ordered[math.ceil(len(ordered) * 0.95) - 1],
            "min_ms": ordered[0], "max_ms": ordered[-1]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "converter", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--processes", type=int, default=5)
    parser.add_argument("--samples", type=int, default=20)
    args = parser.parse_args()
    if args.processes < 2 or args.samples < 2 or args.processes > 100 or args.samples > 10000:
        parser.error("require 2..100 processes and 2..10000 samples")
    stage, prefix, build, converter = (getattr(args, key).resolve(strict=True)
                                       for key in ("stage", "prefix", "build", "converter"))
    pairs = [("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_msc_pso_benchmark.exe", "dx12_msc_pso_benchmark.exe"),
             ("tests/dx12/shader_embedded.compute.cs.cso", "shader_embedded.compute.cs.cso")]
    for source, target in pairs:
        expected = (build / source).read_bytes()
        if source in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and expected[64:80] == b"Wine builtin DLL":
            expected = expected[:64] + b" " * 16 + expected[80:]
        if expected != (stage / target).read_bytes():
            raise SystemExit(f"stage/build mismatch: {target}")
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    cold, warm, roots, runs = [], [], [], []
    for i in range(args.processes):
        env = {key: value for key, value in os.environ.items() if not key.startswith(("DYLD_", "DXMT_TEST_"))}
        env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
                   WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
                   DXMT_METALIRCONVERTER_PATH=str(converter), DYLD_PRINT_LIBRARIES="1",
                   DXMT_SHADER_CACHE_PATH=str(output / f"cache-{i}"), DXMT_LOG_PATH=str(output / f"run-{i}"))
        command = [str(stage / "wine/bin/wine"), "dx12_msc_pso_benchmark.exe",
                   "shader_embedded.compute.cs.cso", str(args.samples)]
        result = subprocess.run(command, cwd=stage, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180, text=True, errors="replace")
        (output / f"run-{i}.log").write_text(result.stdout)
        log = result.stdout
        loaded = []
        for dll, mode in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
            path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
            loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + mode) in line.lower()
                              for line in log.splitlines()))
        samples = [(int(index), float(value)) for index, value in
                   re.findall(r"^PSO_MS (\d+) ([0-9.eE+-]+)\s*$", log, flags=re.M)]
        root = re.findall(r"^ROOT_MS ([0-9.eE+-]+)\s*$", log, flags=re.M)
        identity_ok = (all(loaded) and all(marker in log for marker in identities) and
                       str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") in log and str(converter) in log)
        if result.returncode or not identity_ok or len(root) != 1 or [n for n, _ in samples] != list(range(args.samples)):
            raise SystemExit(f"run {i} failed identity or sampling; see {output / f'run-{i}.log'}")
        values = [value for _, value in samples]
        roots.append(float(root[0])); cold.append(values[0]); warm.extend(values[1:])
        runs.append({"root_ms": roots[-1], "pso_ms": values})
        print(f"run {i}: cold={cold[-1]:.3f} ms, warm median={statistics.median(values[1:]):.3f} ms", flush=True)
    data = {"schema": 1, "description": "complete CreateComputePipelineState call; first is process-cold only",
            "processes": args.processes, "samples_per_process": args.samples,
            "host_hardware": subprocess.check_output(["sysctl", "-n", "machdep.cpu.brand_string"], text=True).strip(),
            "host_os": subprocess.check_output(["sw_vers", "-productVersion"], text=True).strip(),
            "wine_version": subprocess.check_output([str(stage / "wine/bin/wine"), "--version"], text=True,
                env={**os.environ, "WINEPREFIX": str(prefix)}, stderr=subprocess.DEVNULL).strip(),
            "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"],
                cwd=Path(__file__).resolve().parents[2], text=True).strip(),
            "converter_sha256": sha(converter), "artifacts": {target: sha(stage / target) for _, target in pairs},
            "command": command, "cold": summary(cold), "warm_pooled": summary(warm), "root": summary(roots),
            "runs": runs, "not_measured": ["GPU output/time", "Metal counters", "cache hits/misses",
            "classification", "conversion", "reflection", "Metal function", "single-flight wait", "memory"]}
    (output / "summary.json").write_text(json.dumps(data, indent=2) + "\n")
    (output / "summary.md").write_text("# MSC PSO baseline\n\n" + data["description"] + "\n\n" +
        "| Metric | count | median ms | p95 ms | min ms | max ms |\n| --- | ---: | ---: | ---: | ---: | ---: |\n" +
        "".join(f"| {label} | {item['count']} | {item['median_ms']:.6f} | {item['p95_ms']:.6f} | {item['min_ms']:.6f} | {item['max_ms']:.6f} |\n"
                for label, item in (("process-cold PSO", data["cold"]), ("warm PSO pooled", data["warm_pooled"]), ("root creation", data["root"]))) +
        "\nUnmeasured: " + ", ".join(data["not_measured"]) + ".\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
