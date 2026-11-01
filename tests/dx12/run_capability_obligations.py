#!/usr/bin/env python3
"""Run the capability obligation gate with verified isolated Wine artifacts."""
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
    pairs = (("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_capability_obligations.exe", "dx12_capability_obligations.exe"))
    for source, target in pairs:
        expected = (build / source).read_bytes()
        if source in ("src/d3d12/d3d12.dll", "src/dxgi/dxgi.dll") and expected[64:80] == b"Wine builtin DLL":
            expected = expected[:64] + b" " * 16 + expected[80:]
        if expected != (stage / target).read_bytes():
            raise SystemExit(f"stage/build mismatch: {target}")
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = {key: value for key, value in os.environ.items() if not key.startswith("DYLD_") and
           not key.startswith("DXMT_TEST_")}
    env.update(WINEPREFIX=str(prefix), WINEDLLPATH=str(stage / "wine/lib/wine"),
               WINEDLLOVERRIDES="d3d12=n;dxgi=n;winemetal=b", WINEDEBUG="+loaddll",
               DXMT_METALIRCONVERTER_PATH=str(converter), DXMT_TEST_PRINT_LOADED_CODE="1",
               DXMT_SHADER_CACHE_PATH=str(output / "cache"), DXMT_LOG_PATH=str(output),
               MTL_DEBUG_LAYER="1", MTL_SHADER_VALIDATION="1", MTL_SHADER_VALIDATION_REPORT_TO_STDERR="1",
               DYLD_PRINT_LIBRARIES="1")
    result = subprocess.run([str(stage / "wine/bin/wine"), "dx12_capability_obligations.exe"], cwd=stage, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    log = result.stdout.decode(errors="replace")
    (output / "run.log").write_text(log)
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    loaded = []
    for dll, load_type in (("d3d12.dll", "native"), ("dxgi.dll", "native"), ("winemetal.dll", "builtin")):
        path = ("Z:" + str(stage / dll).replace("/", "\\")).replace("\\", "\\\\").lower()
        loaded.append(any(('loaded l"' + path + '"') in line.lower() and (": " + load_type) in line.lower()
                          for line in log.splitlines()))
    identity_ok = (all(loaded) and all(identity in log for identity in identities) and
                   str(stage / "wine/lib/wine/x86_64-unix/winemetal.so") in log and str(converter) in log)
    validation_ok = ("Metal API Validation Enabled" in log and "Metal GPU Validation Enabled" in log and
                     not re.search(r"(?i)(metal (?:api|gpu|shader) validation error|validation error:|invalid metal usage:)", log))
    gate_ok = result.returncode == 0 and "D3D12 capability obligations passed" in log
    observations = [line for line in log.splitlines() if line.startswith("capability.")]
    summary = {"gate_pass": gate_ok, "identity_pass": identity_ok, "validation_pass": validation_ok,
               "exit": result.returncode, "converter_sha256": sha(converter),
               "artifacts": {target: sha(stage / target) for _, target in pairs}, "observations": observations}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"capability gate={gate_ok} identity={identity_ok} validation={validation_ok} exit={result.returncode}")
    return 0 if gate_ok and identity_ok and validation_ok else 1


if __name__ == "__main__":
    sys.exit(main())
