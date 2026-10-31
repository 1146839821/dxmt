#!/usr/bin/env python3
"""Run the opt-in MSC LOD compute experiment against an isolated Wine stage.

The stage and prefix must already be prepared as described in DEVELOPMENT.md.
This runner never copies or installs DLLs. Its logs are written to a new
directory under --logs. A native (not Wine-builtin-marked) d3d12.dll is needed.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def pe_export_bytes(path, symbol):
    """Read the first 16 bytes of an x64 PE export from its on-disk image."""
    data = path.read_bytes()
    u16 = lambda offset: struct.unpack_from("<H", data, offset)[0]
    u32 = lambda offset: struct.unpack_from("<I", data, offset)[0]
    pe = u32(0x3c)
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0" or u16(pe + 4) != 0x8664:
        raise ValueError(f"not an x64 PE: {path}")
    sections, optional_size = u16(pe + 6), u16(pe + 20)
    optional = pe + 24
    if u16(optional) != 0x20b:
        raise ValueError(f"not PE32+: {path}")
    section_table = optional + optional_size

    def file_offset(rva):
        for i in range(sections):
            entry = section_table + i * 40
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, entry + 8)
            if virtual_address <= rva < virtual_address + min(virtual_size, raw_size):
                return raw_offset + rva - virtual_address
        raise ValueError(f"unmapped PE RVA {rva:#x} in {path}")

    export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
    export = file_offset(export_rva)
    count = u32(export + 24)
    functions = file_offset(u32(export + 28))
    names = file_offset(u32(export + 32))
    ordinals = file_offset(u32(export + 36))
    for i in range(count):
        name = file_offset(u32(names + 4 * i))
        end = data.index(b"\0", name)
        if data[name:end].decode("ascii") == symbol:
            ordinal = u16(ordinals + 2 * i)
            if ordinal >= u32(export + 20):
                raise ValueError(f"invalid export ordinal in {path}")
            target = u32(functions + 4 * ordinal)
            if export_rva <= target < export_rva + export_size:
                raise ValueError(f"forwarded export {symbol} in {path}")
            offset = file_offset(target)
            result = data[offset:offset + 16]
            if len(result) != 16:
                raise ValueError(f"short export {symbol} in {path}")
            return result.hex()
    raise ValueError(f"missing export {symbol} in {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("stage", "prefix", "build", "logs"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--sample", default="sample.cso")
    parser.add_argument("--multi", default="multi.cso")
    parser.add_argument("--grad", default="grad.cso")
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--case", action="append", help="run only the named case; may be repeated")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    stage, prefix, build = (path.resolve(strict=True) for path in (args.stage, args.prefix, args.build))
    wine = stage / "wine/bin/wine"
    pairs = {
        "d3d12": (stage / "d3d12.dll", build / "src/d3d12/d3d12.dll"),
        "dxgi": (stage / "dxgi.dll", build / "src/dxgi/dxgi.dll"),
        "winemetal_pe": (stage / "winemetal.dll", build / "src/winemetal/winemetal.dll"),
        "winemetal_unix": (stage / "wine/lib/wine/x86_64-unix/winemetal.so", build / "src/winemetal/unix/winemetal.so"),
        "probe": (stage / "dx12_texture_lod_clamp.exe", build / "tests/dx12/dx12_texture_lod_clamp.exe"),
    }
    for label, (deployed, built) in pairs.items():
        if not deployed.is_file() or not built.is_file() or sha256(deployed) != sha256(built):
            raise SystemExit(f"{label}: missing or mismatched stage/build binary")
    for path in (wine, stage / args.sample, stage / args.multi, stage / args.grad):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    try:
        identities = {"d3d12.dll D3D12CreateDevice": pe_export_bytes(pairs["d3d12"][0], "D3D12CreateDevice"),
                      "winemetal.dll DXMTMSCPatchSamplerLOD": pe_export_bytes(pairs["winemetal_pe"][0], "DXMTMSCPatchSamplerLOD")}
    except (ValueError, IndexError, UnicodeDecodeError, struct.error) as exc:
        raise SystemExit(f"cannot verify PE exports: {exc}") from exc
    args.logs.mkdir(parents=True, exist_ok=True)
    logdir = Path(tempfile.mkdtemp(prefix="msc-lod-", dir=args.logs.resolve()))
    cases = [
        ("sample", args.sample, None, 0, "matrix passed"),
        ("empty", args.sample, "--sample-empty", 0, "matrix passed"),
        ("sampler-max", args.sample, "--sampler-max", 0, "matrix passed"),
        ("same-root", args.sample, "--same-root", 0, "matrix passed"),
        ("pso-switch", args.sample, "--pso-switch", 0, "matrix passed"),
        ("dynamic-late", args.sample, "--dynamic-late", 0, "matrix passed"),
        ("dynamic-stale", args.sample, "--dynamic-stale", 1, "expected:"),
        ("dynamic-recover", args.sample, "--dynamic-recover", 0, "matrix passed"),
        ("dynamic-fail", args.sample, "--dynamic-fail-late", 0, "late sampler sidecar failure rejected"),
        ("dynamic-lifetime", args.sample, "--dynamic-lifetime", 0, "matrix passed"),
        ("multi-queue", args.sample, "--multi-queue", 0, "matrix passed"),
        ("multi-queue-control", args.sample, "--multi-queue-control", 1, "expected:"),
        ("heap-rebind", args.sample, "--heap-rebind", 0, "matrix passed"),
        ("heap-control", args.sample, "--heap-rebind-control", 1, "expected:"),
        ("resubmit", args.sample, "--resubmit", 0, "matrix passed"),
        ("resubmit-control", args.sample, "--resubmit-control", 1, "expected:"),
        ("multi", args.multi, "--multi-sampler", 0, "matrix passed"),
        ("multi-control", args.multi, "--multi-swapped", 1, "expected:"),
        ("mismatch", args.sample, "--mismatch-root", 0, "root mismatch rejected"),
        ("cache", args.sample, "--cache-guard", 0, "pipeline cache guards passed"),
        ("unsupported", args.grad, "--patch-unsupported", 0, "unsupported shader patch rejected"),
        ("unsupported-control", args.sample, "--patch-unsupported", 1, "unsupported shader was not rejected"),
    ]
    if args.case:
        unknown = set(args.case) - {case[0] for case in cases}
        if unknown:
            parser.error("unknown case(s): " + ", ".join(sorted(unknown)))
        cases = [case for case in cases if case[0] in args.case]
    env = os.environ.copy()
    env.update({
        "WINEPREFIX": str(prefix), "WINEDLLPATH": str(stage / "wine/lib/wine"),
        "WINEDLLOVERRIDES": "d3d12=n;dxgi=n;winemetal=b", "WINEDEBUG": "+loaddll",
        "DXMT_TEST_MSC_LOD_PATCH": "1", "DXMT_SHADER_CACHE_PATH": str(logdir / "cache"),
        "DXMT_TEST_PRINT_LOADED_CODE": "1",
        "DXMT_LOG_PATH": str(logdir), "MTL_DEBUG_LAYER": "1", "MTL_SHADER_VALIDATION": "1",
        "MTL_SHADER_VALIDATION_REPORT_TO_STDERR": "1", "DYLD_PRINT_LIBRARIES": "1",
    })
    env.pop("DXMT_TEST_MSC_LOD_UPLOAD", None)
    env.pop("DXMT_TEST_FAIL_SAMPLER_LOD", None)
    results = []
    for name, shader, mode, expected, marker in cases:
        command = [str(wine), "./dx12_texture_lod_clamp.exe", shader] + ([mode] if mode else [])
        try:
            completed = subprocess.run(command, cwd=stage, env=env, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, timeout=args.timeout, check=False)
            output = completed.stdout.decode("utf-8", errors="replace")
            exit_code = completed.returncode
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or b"").decode("utf-8", errors="replace") + "\nTIMEOUT\n"
            exit_code = None
        (logdir / (name + ".log")).write_text(output)
        # Wine reports the Windows path with backslashes; the Unix companion is
        # independently checked via dyld's actual loaded image path.
        windows_path = ("Z:" + str(pairs["d3d12"][0]).replace("/", "\\")).replace("\\", "\\\\")
        pe_marker = 'loaded l"' + windows_path.lower() + '"'
        pe_loaded = any(pe_marker in line.lower() and ': native' in line.lower() for line in output.splitlines())
        dxgi_path = ("Z:" + str(pairs["dxgi"][0]).replace("/", "\\")).replace("\\", "\\\\")
        dxgi_marker = 'loaded l"' + dxgi_path.lower() + '"'
        dxgi_loaded = any(dxgi_marker in line.lower() and ': native' in line.lower() for line in output.splitlines())
        bridge_path = ("Z:" + str(pairs["winemetal_pe"][0]).replace("/", "\\")).replace("\\", "\\\\")
        bridge_marker = 'loaded l"' + bridge_path.lower() + '"'
        bridge_loaded = any(bridge_marker in line.lower() and ': builtin' in line.lower() for line in output.splitlines())
        unix_loaded = str(pairs["winemetal_unix"][0]) in output
        validation = "Metal API Validation Enabled" in output and "Metal GPU Validation Enabled" in output
        validation_error = re.search(r"(?i)(metal (?:api|gpu|shader) validation error|validation error:)", output)
        code_matches = all(f"LOADED_CODE {name} {code}" in output for name, code in identities.items())
        ok = (exit_code == expected and marker in output and pe_loaded and dxgi_loaded and bridge_loaded and unix_loaded and code_matches
              and validation and not validation_error)
        results.append({"case": name, "exit": exit_code, "expected_exit": expected, "marker": marker in output,
                        "d3d12_loaded": pe_loaded, "dxgi_loaded": dxgi_loaded, "bridge_loaded": bridge_loaded,
                        "unix_loaded": unix_loaded, "loaded_code_matches": code_matches,
                        "validation_enabled": validation, "validation_error": bool(validation_error), "pass": ok})
        print(f"{'PASS' if ok else 'FAIL'} {name} (exit={exit_code})", flush=True)
    wine_version = subprocess.run([str(wine), "--version"], cwd=stage, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True, timeout=args.timeout, check=False)
    summary = {"platform": platform.platform(), "machine": platform.machine(),
               "wine_version": wine_version.stdout.strip(),
               "binaries": {name: sha256(pair[0]) for name, pair in pairs.items()},
               "fixtures": {name: sha256(stage / path) for name, path in
                            (("sample", args.sample), ("multi", args.multi), ("grad", args.grad))},
               "results": results, "stage": str(stage), "prefix": str(prefix)}
    (logdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"logs: {logdir}")
    return 0 if all(result["pass"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
