#!/usr/bin/env python3
"""Fail-closed FL12 evidence ledger; never changes advertised capabilities."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

PASS = "PASS"
PARTIAL = "PARTIAL"
FAIL = "FAIL"
BLOCKED = "BLOCKED_BY_ARCHITECTURE"
UNVERIFIED = "UNVERIFIED"
STATUSES = {PASS, PARTIAL, FAIL, BLOCKED, UNVERIFIED}


def aggregate(rows):
    statuses = [row["status"] for row in rows]
    if not statuses or any(status not in STATUSES for status in statuses):
        raise ValueError("empty gate or invalid status")
    for status in (FAIL, BLOCKED, UNVERIFIED, PARTIAL):
        if status in statuses:
            return status
    return PASS


def row(name, status, reason):
    return {"name": name, "status": status, "reason": reason}


def runtime_hashes(runtime):
    return {dll: hashlib.sha256((runtime / dll / (dll + ".dll")).read_bytes()).hexdigest()
            for dll in ("d3d12", "dxgi", "winemetal")}


def verify_build(build, variant, wine):
    """Bind the variant label and Unix runtime to this build, not ambient DLLs."""
    try:
        commands = json.loads((build / "compile_commands.json").read_text())
        for suffix in ("d3d12_device.cpp", "cache.c"):
            matches = [entry for entry in commands if entry["file"].endswith("/" + suffix)]
            if not matches:
                return {"status": UNVERIFIED, "reason": "compile command missing: " + suffix}
            for entry in matches:
                command = entry.get("command", " ".join(entry.get("arguments", [])))
                if ("-DDXMT_NO_PRIVATE_API" in command) != (variant == "no-private"):
                    return {"status": FAIL, "reason": "build variant mismatch: " + suffix}
        if wine:
            expected = build / "src/winemetal/unix/winemetal.so"
            installed = Path(wine).resolve().parent.parent / "lib/wine/x86_64-unix/winemetal.so"
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            if digest(expected) != digest(installed):
                return {"status": UNVERIFIED, "reason": "installed winemetal.so does not match build"}
            return {"status": PASS, "reason": "compile flags and installed Unix runtime matched",
                    "unix_sha256": digest(expected), "runtime_sha256": runtime_hashes(build / "src")}
        return {"status": PASS, "reason": "compile flags matched; native Windows, no Unix runtime",
                "runtime_sha256": runtime_hashes(build / "src")}
    except (OSError, ValueError, KeyError) as error:
        return {"status": UNVERIFIED, "reason": str(error)}


def run_fixture(directory, wine, name, args, required, timeout, runtime=None, stage_files=None):
    files = (name,) + (tuple(args) if stage_files is None else tuple(stage_files))
    missing = [file for file in files if not (directory / file).is_file()]
    if missing:
        return {"status": UNVERIFIED, "reason": "missing: " + ", ".join(missing)}
    dlls = ("d3d12", "dxgi", "winemetal")
    if runtime is not None:
        missing = [dll for dll in dlls if not (runtime / dll / (dll + ".dll")).is_file()]
        if missing:
            return {"status": UNVERIFIED, "reason": "missing runtime DLLs: " + ", ".join(missing)}
    # Never load stale test-local DXMT DLLs; use the explicitly deployed runtime.
    with tempfile.TemporaryDirectory(prefix="dxmt-fl12-") as staging:
        stage = Path(staging)
        staged_hashes = {}
        for file in files:
            shutil.copy2(directory / file, stage / file)
        if runtime is not None:
            for dll in dlls:
                shutil.copy2(runtime / dll / (dll + ".dll"), stage / (dll + ".dll"))
                staged_hashes[dll] = hashlib.sha256((stage / (dll + ".dll")).read_bytes()).hexdigest()
        compiler = directory / "d3dcompiler_47.dll"
        if compiler.is_file():
            shutil.copy2(compiler, stage / compiler.name)
        command = ([wine] if wine else []) + [str(stage / name)] + list(args)
        try:
            result = subprocess.run(
                command, cwd=stage, capture_output=True, text=True,
                errors="replace", timeout=timeout,
                env={**os.environ, "DXMT_SHADER_CACHE": "0",
                     "WINEDLLOVERRIDES": os.environ.get("WINEDLLOVERRIDES", "") + ";d3d12,dxgi,winemetal=n,b"},
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            return {"status": FAIL, "reason": str(error)}
        output = result.stdout + result.stderr
        status = PASS if result.returncode == 0 and all(marker in output for marker in required) else FAIL
        if result.returncode == 77:
            status = UNVERIFIED
        return {"status": status, "returncode": result.returncode, "output": output,
                "reason": "fresh execution; required markers checked", "runtime_sha256": staged_hashes}


def run_minmax_contract(directory, wine, timeout, runtime):
    cases = {
        "filter_matrix": run_fixture(directory, wine, "dx12_sampler_filter.exe", (),
                                    ("D3D12 sampler filter contracts passed: 72",), timeout, runtime),
    }
    for backend, shader, files in (("dxbc", "--dxbc", ()),
                                  ("dxil", "texture_sampler.cs.cso", ("texture_sampler.cs.cso",))):
        for mode in ("dynamic", "static"):
            control_args = (shader,) if mode == "dynamic" else (shader, "--static-sampler")
            cases[backend + "_" + mode + "_control"] = run_fixture(
                directory, wine, "dx12_texture_sampler.exe", control_args,
                ("texture sampler readback passed: 255",), timeout, runtime, files)
            for reduction in ("minimum", "maximum"):
                flag = "--" + ("static-" if mode == "static" else "") + reduction
                name = backend + "_" + mode + "_" + reduction
                cases[name] = run_fixture(
                    directory, wine, "dx12_texture_sampler.exe", (shader, flag, "--expect-unsupported"),
                    ("minmax " + mode + " rejected without fallback",), timeout, runtime, files)
    hashes = [case.get("runtime_sha256") for case in cases.values() if case.get("runtime_sha256")]
    status = aggregate([row(name, case["status"], "") for name, case in cases.items()])
    if hashes and any(digest != hashes[0] for digest in hashes):
        status = UNVERIFIED
    return {"status": status, "reason": "rejection contracts and ordinary GPU controls; not min/max GPU acceptance",
            "runtime_sha256": hashes[0] if hashes else {}, "cases": cases}


def build_report(probes, variant, provenance=None):
    feature = probes["feature_support"]
    options = None
    if feature["status"] == PASS:
        match = re.search(
            r"options: tiled=(\d+) binding=(\d+) stencilRef=(\d+) logicOp=(\d+) "
            r"typedUAV=(\d+) ROV=(\d+) conservative=(\d+) heap=(\d+)",
            feature.get("output", ""),
        )
        if match:
            options = dict(zip(("tiled", "binding", "stencil", "logic", "typed", "rov", "conservative", "heap"),
                               map(int, match.groups())))

    def api(name, key, minimum):
        if options is None:
            return row(name, UNVERIFIED, "no valid current API query; semantics also unverified")
        meets = options[key] >= minimum
        return row(name, PARTIAL if meets else FAIL,
                   f"API {key}={options[key]}, required >= {minimum}; full GPU matrix not validated")

    fl0 = [
        row("feature_query_contract", feature["status"], "fresh feature probe must succeed independently"),
        api("resource_binding_tier2", "binding", 2),
        api("tiled_resources_tier2", "tiled", 2),
        api("typed_uav_additional_formats", "typed", 1),
        api("output_merger_logic_op", "logic", 1),
    ]
    if provenance is not None:
        fl0.append(row("build_runtime_provenance", provenance["status"], provenance["reason"]))
    minmax = probes.get("minmax_sampler_contract")
    fl0.append(row("min_max_reduction_filtering",
                   BLOCKED if minmax and minmax["status"] == PASS else
                   minmax["status"] if minmax else UNVERIFIED,
                   "rejection contract only; full min/max shader implementation absent"))
    for name in ("mandatory_raster_matrix", "mandatory_format_matrix",
                 "dxbc_mandatory_shader_paths", "dxil_mandatory_shader_paths",
                 "dxbc_tessellation", "dxil_tessellation", "geometry_shader_stream_output"):
        fl0.append(row(name, UNVERIFIED, "complete mandatory GPU readback matrix not registered"))
    isolation = aggregate([row(name, probes[name]["status"], "")
                           for name in ("shader_validation", "shader_container", "shader_stage_matrix")
                           if name in probes])
    fl0.append(row("backend_isolation", PARTIAL if isolation == PASS else isolation,
                   "PSO family/stage rejection covered; compiler-failure invocation/fallback oracle missing"))
    fl1 = [
        row("FL12_0_dependency", aggregate(fl0), "all FL12_0 requirements must PASS"),
        api("dxbc_rov", "rov", 1),
        api("dxil_rov", "rov", 1),
        api("conservative_rasterization_tier1", "conservative", 1),
        row("fl12_1_full_contract", UNVERIFIED, "full raster/format/backend semantic matrix missing"),
    ]
    blockers = [
        row("tiled_raw_structured_buffer", BLOCKED, "flat-VA sparse shader ABI gap; see resource audit"),
        row("tiled_packed_mip_multi_tile", BLOCKED, "packed multi-tile residency/aliasing gap; see resource audit"),
        row("tiled_clamp_and_windows_oracle", UNVERIFIED, "independent clamp/oracle evidence required"),
    ]
    return {"schema_version": 1, "build_variant": variant, "capability_changes": False,
            "FL12_0_GATE": {"status": aggregate(fl0), "requirements": fl0},
            "FL12_1_GATE": {"status": aggregate(fl1), "requirements": fl1},
            "architecture_watchlist": blockers, "probes": probes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True, help="Meson build directory")
    parser.add_argument("--wine", help="Wine executable; omit on native Windows")
    parser.add_argument("--variant", choices=("normal", "no-private"), required=True,
                        help="label only; caller must independently verify build configuration")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--output", type=Path, help="save full JSON evidence; print compact status")
    args = parser.parse_args()
    directory = args.build_dir.resolve() / "tests" / "dx12"
    runtime = args.build_dir.resolve() / "src"
    provenance = verify_build(args.build_dir.resolve(), args.variant, args.wine)
    fixtures = ("shader_embedded.compute.cs.cso", "compute_sm6.cs.cso",
                "shader_embedded.graphics.vs.cso", "shader_embedded.graphics.ps.cso",
                "shader_embedded.graphics.mismatch.ps.cso")
    probes = {
        "feature_support": run_fixture(directory, args.wine, "dx12_feature_support.exe", (),
                                      ("D3D12 feature support contract passed",), args.timeout, runtime),
        "shader_validation": run_fixture(directory, args.wine, "dx12_shader_validation.exe", (),
                                        ("D3D12 shader validation contracts passed",), args.timeout, runtime),
        "shader_container": run_fixture(
            directory, args.wine, "dx12_shader_container.exe", fixtures,
            ("container legacy-explicit-root passed", "container mixed-dxbc-vs-dxil-ps passed",
             "container mixed-dxil-vs-dxbc-ps passed", "container duplicate-legacy-executable passed",
             "container synthetic-legacy-plus-dxil-hybrid passed"), args.timeout, runtime),
        "shader_stage_matrix": run_fixture(
            directory, args.wine, "dx12_shader_container.exe",
            (fixtures[0], fixtures[2], fixtures[3], fixtures[4],
             "shader_backend.geometry.gs.cso", "shader_backend.tessellation.hs.cso",
             "shader_backend.tessellation.ds.cso", "shader_backend.library.lib.cso"),
            ("container mixed-legacy-vs-dxil-gs passed", "container mixed-dxil-vs-legacy-gs passed",
             "container mixed-legacy-vs-dxil-hs-ds passed", "container mixed-dxil-vs-legacy-hs-ds passed",
             "container dxil-library-in-ordinary-graphics-slot passed"), args.timeout, runtime),
    }
    probes["minmax_sampler_contract"] = run_minmax_contract(directory, args.wine, args.timeout, runtime)
    if verify_build(args.build_dir.resolve(), args.variant, args.wine) != provenance:
        provenance = {"status": UNVERIFIED, "reason": "build/runtime provenance changed during probes"}
    expected_hashes = provenance.get("runtime_sha256")
    if expected_hashes and any(probe.get("runtime_sha256", expected_hashes) != expected_hashes
                               for probe in probes.values()):
        provenance = {"status": UNVERIFIED, "reason": "staged DLL hashes differ between probes"}
    report = build_report(probes, args.variant, provenance)
    report["build_provenance"] = provenance
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({name: report[name]["status"] for name in ("FL12_0_GATE", "FL12_1_GATE")} |
                         {"probes": {name: probe["status"] for name, probe in probes.items()}}, indent=2))
    else:
        print(json.dumps(report, indent=2))
    return 0 if all(report[gate]["status"] == PASS for gate in ("FL12_0_GATE", "FL12_1_GATE")) else 1


if __name__ == "__main__":
    raise SystemExit(main())
