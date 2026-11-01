#!/usr/bin/env python3
"""Run the three required R32 typed texture UAV GPU readback cases."""
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
    args = parser.parse_args()
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    pairs = [("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_texture_sampler.exe", "dx12_texture_sampler.exe")]
    cases = [("float", 41), ("uint", 42), ("sint", 43)]
    pairs += [(f"tests/dx12/texture_sampler_typed_uav_{kind}.cs.cso",
               f"texture_sampler_typed_uav_{kind}.cs.cso") for kind, _ in cases]
    for source, target in pairs:
        expected = (build / source).read_bytes()
        if source in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and expected[64:80] == b"Wine builtin DLL":
            expected = expected[:64] + b" " * 16 + expected[80:]
        if expected != (stage / target).read_bytes():
            raise SystemExit(f"stage/build mismatch: {target}")
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DYLD_", "DXMT_TEST_"))}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(converter), DXMT_TEST_PRINT_LOADED_CODE="1",
               MTL_DEBUG_LAYER="1", MTL_SHADER_VALIDATION="1", MTL_SHADER_VALIDATION_REPORT_TO_STDERR="1",
               DYLD_PRINT_LIBRARIES="1")
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    rows = []
    for kind, fmt in cases:
        env["DXMT_SHADER_CACHE_PATH"] = str(output / f"cache-{kind}")
        env["DXMT_LOG_PATH"] = str(output)
        command = [str(stage / "wine/bin/wine"), "dx12_texture_sampler.exe",
                   f"texture_sampler_typed_uav_{kind}.cs.cso", "--typed-uav", str(fmt)]
        result = subprocess.run(command, cwd=stage, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        log = result.stdout.decode(errors="replace")
        (output / f"{kind}.log").write_text(log)
        loaded = []
        for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
            path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
            loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                              for line in log.splitlines()))
        identity = (all(loaded) and all(value in log for value in identities) and
                    str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") in log and str(converter) in log)
        validation = ("Metal API Validation Enabled" in log and "Metal GPU Validation Enabled" in log and
                      not re.search(r"(?i)(validation error|invalid metal usage:|execution of the command buffer was aborted)", log))
        expected = f"typed UAV format={fmt} load/store checks=20 passed"
        passed = result.returncode == 0 and expected in log
        rows.append(dict(kind=kind, format=fmt, exit=result.returncode, readback_pass=passed,
                         identity_pass=identity, validation_pass=validation))
        print(rows[-1], flush=True)
    summary = {"cases": rows, "converter_sha256": sha(converter),
               "artifacts": {target: sha(stage / target) for _, target in pairs}}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if all(r["readback_pass"] and r["identity_pass"] and r["validation_pass"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
