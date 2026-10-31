#!/usr/bin/env python3
"""Compile and inspect a small MSC vertex corpus into a new output directory.

Requires a pre-existing isolated Wine prefix. No DLL installation is performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[2]
CASES = (
    ("vertexid", "tests/dx12/texture_lod_vertexid.hlsl", "main", ()),
    ("dimensions", "tests/dx12/texture_dimensions_sm6.hlsl", "vertex_main", ("-D", "MS_WRITE=1")),
    ("hdr", "tests/dx12/dx12_hdr.hlsl", "vs_main", ()),
)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, env=None):
    result = subprocess.run([str(x) for x in args], env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {args}\n{result.stdout}")
    return result.stdout.strip()


def wine_path(path):
    return "Z:" + str(path.resolve()).replace("/", "\\")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("wine", "prefix", "dxc", "converter", "inspector", "output"):
        parser.add_argument("--" + arg, required=True, type=Path)
    args = parser.parse_args()
    for name in ("wine", "prefix", "dxc", "converter", "inspector"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    output = args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env.update(WINEPREFIX=str(args.prefix), WINEDEBUG="-all")
    compiler_dll = args.dxc.parent / "dxcompiler.dll"
    manifest = {
        "wine_version": run([args.wine, "--version"], env),
        "dxc_version": run([args.wine, args.dxc, "--version"], env),
        "converter_version": run([args.converter, "--version"]),
        "tool_sha256": {"wine": sha256(args.wine), "dxc": sha256(args.dxc),
                        "dxcompiler": sha256(compiler_dll), "converter": sha256(args.converter),
                        "inspector": sha256(args.inspector)},
        "cases": [],
    }
    for name, source_name, entry, extra in CASES:
        source = ROOT / source_name
        for optimization in ("O0", "O3"):
            stem = f"{name}-{optimization}"
            dxil = output / (stem + ".dxil")
            metallib = output / (stem + ".metallib")
            dxc_args = ["-T", "vs_6_0", "-E", entry, "-" + optimization, *extra,
                        "-Fo", wine_path(dxil), wine_path(source)]
            run([args.wine, args.dxc, *dxc_args], env)
            converter_args = [str(dxil), "-o", str(metallib)]
            run([args.converter, *converter_args])
            report = run([args.inspector, "--inspect", metallib])
            manifest["cases"].append({
                "name": stem, "source": source_name, "source_sha256": sha256(source),
                "dxc_args": dxc_args, "converter_args": converter_args,
                "dxil_sha256": sha256(dxil), "metallib_sha256": sha256(metallib),
                "inspection": report,
            })
            print(stem, report.splitlines()[-1], flush=True)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("manifest:", output / "manifest.json")


if __name__ == "__main__":
    main()
