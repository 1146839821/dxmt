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
        suffixes = ["d3d12_device.cpp", "cache.c"]
        if (build / "tests/dx12/dx12_backend_failure.exe").is_file():
            suffixes += ["dx12_backend_failure.cpp", "d3d12_pipeline_compute.cpp", "d3d12_pipeline_graphics.cpp", "d3d12_shader_converter.cpp", "d3d12_pipeline_persistence.cpp"]
        for suffix in suffixes:
            matches = [entry for entry in commands if entry["file"].endswith("/" + suffix)]
            if not matches:
                return {"status": UNVERIFIED, "reason": "compile command missing: " + suffix}
            for entry in matches:
                command = entry.get("command", " ".join(entry.get("arguments", [])))
                if ("-DDXMT_NO_PRIVATE_API" in command) != (variant == "no-private"):
                    return {"status": FAIL, "reason": "build variant mismatch: " + suffix}
        if wine:
            expected = build / "src/winemetal/unix/winemetal.so"
            wine_root = Path(wine).resolve().parent.parent
            installed = wine_root / "lib/wine/x86_64-unix/winemetal.so"
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            if digest(expected) != digest(installed):
                return {"status": UNVERIFIED, "reason": "installed winemetal.so does not match build"}
            prefix = os.environ.get("WINEPREFIX")
            if not prefix:
                return {"status": UNVERIFIED, "reason": "explicit WINEPREFIX required for installed DLL provenance"}
            hashes = runtime_hashes(build / "src")
            # Wine builtin-marked DLLs can resolve through the installed runtime,
            # even when a matching DLL was copied beside the fixture executable.
            for root in (wine_root / "lib/wine/x86_64-windows", Path(prefix) / "drive_c/windows/system32"):
                for dll, expected_hash in hashes.items():
                    if digest(root / (dll + ".dll")) != expected_hash:
                        return {"status": UNVERIFIED, "reason": "installed DLL does not match build: " + str(root / (dll + ".dll"))}
            return {"status": PASS, "reason": "compile flags, installed PE DLLs and Unix runtime matched",
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
        executable_hash = hashlib.sha256((stage / name).read_bytes()).hexdigest()
        if runtime is not None:
            for dll in dlls:
                shutil.copy2(runtime / dll / (dll + ".dll"), stage / (dll + ".dll"))
                staged_hashes[dll] = hashlib.sha256((stage / (dll + ".dll")).read_bytes()).hexdigest()
        compiler = directory / "d3dcompiler_47.dll"
        if compiler.is_file():
            shutil.copy2(compiler, stage / compiler.name)
        command = ([wine] if wine else []) + [str(stage / name)] + list(args)
        try:
            # Wine helpers may inherit output handles after the probe exits.
            # A file records output without waiting for pipe EOF from helpers.
            with tempfile.TemporaryFile(mode="w+t", errors="replace") as log:
                result = subprocess.run(
                    command, cwd=stage, stdout=log, stderr=subprocess.STDOUT,
                    timeout=timeout,
                    env={**os.environ, "DXMT_SHADER_CACHE": "0",
                         "WINEDLLOVERRIDES": os.environ.get("WINEDLLOVERRIDES", "") + ";d3d12,dxgi,winemetal=n,b"},
                )
                log.seek(0)
                output = log.read()
        except (OSError, subprocess.TimeoutExpired) as error:
            return {"status": FAIL, "reason": str(error)}
        status = PASS if result.returncode == 0 and all(marker in output for marker in required) else FAIL
        if result.returncode == 77:
            status = UNVERIFIED
        return {"status": status, "returncode": result.returncode, "output": output,
                "reason": "fresh execution; required markers checked", "runtime_sha256": staged_hashes,
                "executable_sha256": executable_hash}


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
        status = aggregate([row("execution", status, ""), row("hash_consistency", UNVERIFIED, "")])
    return {"status": status, "reason": "rejection contracts and ordinary GPU controls; not min/max GPU acceptance",
            "runtime_sha256": hashes[0] if hashes else {}, "cases": cases}


def run_typed_uav_matrix(directory, wine, timeout, runtime):
    cases = {
        "policy": run_fixture(directory, wine, "dx12_typed_uav_policy.exe", (),
                              ("typed UAV policy contracts passed",), timeout, runtime),
        "api": run_fixture(directory, wine, "dx12_typed_uav_formats.exe", ("--api-policy",),
                           ("typed UAV API contracts passed",), timeout, runtime, ()),
        "submission_residency": run_fixture(directory, wine, "dx12_typed_buffer_residency.exe", (),
                                             ("typed buffer submission residency contracts passed",), timeout, runtime),
    }
    for backend in ("dxbc", "dxil"):
        files = ("typed_uav_formats.hlsl",) if backend == "dxbc" else tuple(
            "typed_uav_%d_%d.cso" % (type_index, shape) for type_index in range(8) for shape in range(6))
        cases[backend] = run_fixture(directory, wine, "dx12_typed_uav_formats.exe", ("--" + backend,),
                                     ("typed UAV matrix: passed=144 failed=0",), timeout, runtime, files)
        view_files = ("typed_uav_formats.hlsl",) if backend == "dxbc" else tuple(
            "typed_uav_%d_0.cso" % type_index for type_index in range(8))
        cases[backend + "_views"] = run_fixture(
            directory, wine, "dx12_typed_uav_formats.exe", ("--" + backend, "--view-contract"),
            ("typed UAV view contracts: passed=126 failed=0",), timeout, runtime, view_files)
        srv_files = ("typed_uav_formats.hlsl",) if backend == "dxbc" else tuple(
            "typed_uav_srv_%d.cso" % type_index for type_index in range(8))
        cases[backend + "_srv_views"] = run_fixture(
            directory, wine, "dx12_typed_uav_formats.exe", ("--" + backend, "--srv-view-contract"),
            ("typed SRV view contracts: passed=126 failed=0",), timeout, runtime, srv_files)
    status = aggregate([row(name, case["status"], "") for name, case in cases.items()])
    hashes = [case.get("runtime_sha256") for case in cases.values() if case.get("runtime_sha256")]
    if hashes and any(digest != hashes[0] for digest in hashes):
        status = aggregate([row("execution", status, ""), row("hash_consistency", UNVERIFIED, "")])
    return {"status": status, "reason": "18 formats, six UAV shapes and three buffer offsets; both backends required",
            "runtime_sha256": hashes[0] if hashes else {}, "cases": cases}


def run_invocation_modes(directory, wine, timeout, runtime, specifications, reason):
    cases = {}
    def probe_digest():
        try:
            return hashlib.sha256((directory / "dx12_backend_failure.exe").read_bytes()).hexdigest()
        except OSError:
            return None
    probe_hash = probe_digest()
    for mode, shaders in specifications:
        cases[mode] = run_fixture(directory, wine, "dx12_backend_failure.exe", (mode,) + shaders,
                                  ("backend failure " + mode + ":", "status=PASS"), timeout, runtime, shaders)
    status = aggregate([row(name, case["status"], "") for name, case in cases.items()])
    hashes = [case.get("runtime_sha256") for case in cases.values() if case.get("runtime_sha256")]
    if hashes and any(digest != hashes[0] for digest in hashes):
        status = aggregate([row("execution", status, ""), row("hash_consistency", UNVERIFIED, "")])
    if not probe_hash or probe_digest() != probe_hash or any(
            case.get("executable_sha256") != probe_hash for case in cases.values()):
        status = aggregate([row("execution", status, ""), row("probe_provenance", UNVERIFIED, "")])
    return {"status": status, "reason": reason,
            "probe_sha256": probe_hash, "runtime_sha256": hashes[0] if hashes else {}, "cases": cases}


def run_backend_failure_oracle(directory, wine, timeout, runtime):
    modes = ("air-control", "air-init-failure", "air-compile-failure", "air-wrong-stage", "empty",
             "msc-control", "msc-invalid", "msc-unsupported", "msc-memory", "msc-second-pass", "msc-wrong-stage")
    specifications = [(mode, ("shader_embedded.graphics.vs.cso" if mode == "msc-wrong-stage" else "compute_sm6.cs.cso",))
                      for mode in modes]
    return run_invocation_modes(directory, wine, timeout, runtime, specifications,
                                "test-linked production compute routing; not GPU dispatch acceptance")


def run_graphics_failure_oracle(directory, wine, timeout, runtime):
    modes = ["graphics-air-control", "graphics-msc-control", "graphics-mixed-air-vs", "graphics-mixed-msc-vs"]
    for backend in ("air", "msc"):
        for stage in ("vs", "ps"):
            modes.append("graphics-" + backend + "-wrong-" + stage)
            for failure in (("init", "compile") if backend == "air" else ("invalid", "unsupported", "memory", "second-pass")):
                modes.append("graphics-" + backend + "-" + stage + "-" + failure)
    specifications = [(mode, ("backend_failure.vs.cso", "backend_failure.ps.cso")) for mode in modes]
    return run_invocation_modes(directory, wine, timeout, runtime, specifications,
                                "test-linked ordinary VS/PS ordered compiler traces; not GPU draw acceptance")


def run_tessellation_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend in ("air", "msc"):
        modes.append("tess-" + backend + "-control")
        for stage in ("hs", "ds"):
            modes += ["tess-" + backend + "-mixed-" + stage, "tess-" + backend + "-wrong-" + stage]
            for failure in (("init", "compile") if backend == "air" else ("invalid", "unsupported", "memory", "second-pass")):
                modes.append("tess-" + backend + "-" + stage + "-" + failure)
    specifications = [(mode, ("backend_failure.vs.cso", "backend_failure.ps.cso",
                              "shader_backend.tessellation.hs.cso", "shader_backend.tessellation.ds.cso",
                              "shader_backend_stages.hlsl")) for mode in modes]
    return run_invocation_modes(directory, wine, timeout, runtime, specifications,
                                "test-linked HS/DS and combined AIRCONV compiler traces; not GPU tessellation acceptance")


def run_geometry_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend in ("air", "msc"):
        modes += ["geom-" + backend + "-control", "geom-" + backend + "-mixed-gs", "geom-" + backend + "-wrong-gs"]
        for failure in (("init", "compile", "object-compile") if backend == "air" else ("invalid", "unsupported", "memory", "second-pass")):
            modes.append("geom-" + backend + "-gs-" + failure)
    specifications = [(mode, ("backend_failure.vs.cso", "backend_failure.ps.cso",
                              "shader_backend.geometry.gs.cso", "shader_backend_stages.hlsl")) for mode in modes]
    return run_invocation_modes(directory, wine, timeout, runtime, specifications,
                                "test-linked GS and combined VS/GS compiler traces; not GPU geometry acceptance")


def run_mesh_failure_oracle(directory, wine, timeout, runtime):
    modes = ["mesh-control-no-as", "mesh-control-as", "mesh-empty-ms"]
    for stage in ("ms", "as", "ps"):
        modes += ["mesh-wrong-" + stage, "mesh-dxbc-" + stage]
        modes += ["mesh-" + stage + "-" + failure for failure in ("invalid", "unsupported", "memory", "second-pass")]
    specifications = [(mode, ("mesh_sm6.ms.cso", "mesh_sm6.as.cso", "mesh_sm6.ps.cso")) for mode in modes]
    return run_invocation_modes(directory, wine, timeout, runtime, specifications,
                                "test-linked native MS/AS/PS compiler traces; not GPU mesh draw acceptance")


def run_pipeline_library_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend, failures in (("air", ("init", "compile")),
                              ("msc", ("invalid", "unsupported", "memory", "second-pass"))):
        modes += ["library-" + backend + "-" + operation
                  for operation in ("retained", "reload", "missing", "mismatch") + failures]
    return run_invocation_modes(directory, wine, timeout, runtime,
                                [(mode, ("compute_sm6.cs.cso",)) for mode in modes],
                                "compute retained hits, metadata reload, failure/retry compiler traces; not native binary cache or GPU acceptance")


def run_graphics_library_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend, failures in (("air", ("init", "compile")),
                              ("msc", ("invalid", "unsupported", "memory", "second-pass"))):
        prefix = "library-graphics-" + backend + "-"
        modes += [prefix + operation for operation in ("retained", "reload", "missing", "mismatch")]
        modes += [prefix + stage + "-" + failure for stage in ("vs", "ps") for failure in failures]
    return run_invocation_modes(directory, wine, timeout, runtime,
                                [(mode, ("backend_failure.vs.cso", "backend_failure.ps.cso")) for mode in modes],
                                "ordinary graphics library hit/reload/failure/retry traces; not HS/DS/GS library or GPU acceptance")


def run_tessellation_library_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend, failures in (("air", ("init", "compile")),
                              ("msc", ("invalid", "unsupported", "memory", "second-pass"))):
        prefix = "library-tess-" + backend + "-"
        modes += [prefix + operation for operation in ("retained", "reload", "missing", "mismatch")]
        modes += [prefix + stage + "-" + failure for stage in ("hs", "ds") for failure in failures]
    files = ("backend_failure.vs.cso", "backend_failure.ps.cso", "shader_backend.tessellation.hs.cso",
             "shader_backend.tessellation.ds.cso", "shader_backend_stages.hlsl")
    return run_invocation_modes(directory, wine, timeout, runtime, [(mode, files) for mode in modes],
                                "HS/DS library hit/reload/failure/retry traces; not GPU tessellation acceptance")


def run_geometry_library_failure_oracle(directory, wine, timeout, runtime):
    modes = []
    for backend, failures in (("air", ("init", "compile", "object-compile")),
                              ("msc", ("invalid", "unsupported", "memory", "second-pass"))):
        prefix = "library-geom-" + backend + "-"
        modes += [prefix + operation for operation in ("retained", "reload", "missing", "mismatch")]
        modes += [prefix + "gs-" + failure for failure in failures]
    files = ("backend_failure.vs.cso", "backend_failure.ps.cso",
             "shader_backend.geometry.gs.cso", "shader_backend_stages.hlsl")
    return run_invocation_modes(directory, wine, timeout, runtime, [(mode, files) for mode in modes],
                                "GS library hit/reload/mesh-object failure/retry traces; not GPU geometry acceptance")


def run_shader_library_failure_oracle(directory, wine, timeout, runtime):
    modes = ["shaderlib-" + stage + "-" + operation
             for stage in ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable")
             for operation in ("control", "invalid", "unsupported", "memory", "second-pass")]
    modes += ["shaderlib-" + rejection for rejection in ("legacy", "ordinary", "empty-entry", "qualifiers")]
    files = ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso")
    return run_invocation_modes(directory, wine, timeout, runtime, [(mode, files) for mode in modes],
                                "six ray-stage library converter traces/retry/cache; not state-object or GPU tracing acceptance")


def run_state_object_failure_oracle(directory, wine, timeout, runtime):
    stages = ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
              "hint-anyhit", "hint-closesthit")
    modes = ["state-" + stage + "-" + operation for stage in stages
             for operation in ("control", "invalid", "unsupported", "memory", "second-pass")]
    modes += ["state-" + rejection for rejection in ("legacy", "ordinary", "qualifiers", "missing-export")]
    modes += ["state-load-" + stage + "-" + operation for stage in stages
              for operation in ("control", "library", "function")]
    modes += ["state-multi-" + stage + "-" + operation for stage in stages
              for operation in ("control", "library", "function", "unsupported", "second-pass")]
    files = ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso")
    return run_invocation_modes(directory, wine, timeout, runtime, [(mode, files) for mode in modes],
                                "state-object compiler/load probing/hints/retry/cache; not GPU tracing acceptance")


def run_state_object_addition_failure_oracle(directory, wine, timeout, runtime):
    stages = ("raygen", "miss", "closesthit", "anyhit", "intersection", "callable",
              "hint-anyhit", "hint-closesthit")
    modes = ["state-add-" + stage + "-" + operation for stage in stages
             for operation in ("control", "invalid", "unsupported", "memory", "second-pass")]
    modes += ["state-add-" + rejection for rejection in
              ("legacy", "ordinary", "qualifiers", "missing-export", "duplicate", "disallowed")]
    modes += ["state-add-load-" + stage + "-" + operation for stage in stages
              for operation in ("control", "library", "function")]
    modes += ["state-add-multi-" + stage + "-" + operation for stage in stages
              for operation in ("control", "library", "function", "unsupported", "second-pass")]
    files = ("ray_stages_sm6.lib.cso", "compute_sm6.cs.cso", "ray_payload_qualifiers_sm6.lib.cso")
    return run_invocation_modes(directory, wine, timeout, runtime, [(mode, files) for mode in modes],
                                "addition failures/parent immutability/retry/cache; not dispatch synthesis or GPU tracing acceptance")


def run_ray_synthesis_failure_oracle(directory, wine, timeout, runtime):
    modes = ["synth-" + path + "-" + operation for path in ("dispatch", "intersection")
             for operation in ("control", "unsupported", "unavailable", "memory", "invalid", "second-pass")]
    return run_invocation_modes(directory, wine, timeout, runtime,
                                [(mode, ("ray_stages_sm6.lib.cso",)) for mode in modes],
                                "lazy synthesis failure/retry/retained-state traces; not GPU raytracing acceptance")


def run_ray_metal_failure_oracle(directory, wine, timeout, runtime):
    modes = ["metal-" + path + "-" + operation for path in ("dispatch", "intersection")
             for operation in ("control", "pso", "vft", "ift", "visible-handle")]
    modes.append("metal-intersection-intersection-handle")
    modes.extend("load-" + path + "-" + operation for path in ("dispatch", "intersection")
                 for operation in ("control", "dispatch-library", "dispatch-function"))
    modes.extend(("load-intersection-intersection-library", "load-intersection-intersection-function"))
    return run_invocation_modes(directory, wine, timeout, runtime,
                                [(mode, ("ray_stages_sm6.lib.cso",)) for mode in modes],
                                "Metal library/function/PSO/table/handle failure and retry; not GPU tracing acceptance")


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
    typed = probes.get("typed_uav_matrix")
    typed_api = options is not None and options["typed"] == 1
    if typed and typed["status"] == PASS and typed_api:
        for requirement in fl0:
            if requirement["name"] == "typed_uav_additional_formats":
                requirement.update(status=PASS, reason="API and complete typed UAV GPU matrix passed")
    fl0.append(row("typed_uav_mandatory_gpu_matrix",
                   typed["status"] if typed and typed["status"] != PASS else
                   PASS if typed and typed_api else FAIL if typed else UNVERIFIED,
                   "requires complete GPU matrix and advertised additional-format support"))
    minmax = probes.get("minmax_sampler_contract")
    fl0.append(row("min_max_reduction_filtering",
                   BLOCKED if minmax and minmax["status"] == PASS else
                   minmax["status"] if minmax else UNVERIFIED,
                   "rejection contract only; full min/max shader implementation absent"))
    for name in ("mandatory_raster_matrix", "mandatory_format_matrix",
                 "dxbc_mandatory_shader_paths", "dxil_mandatory_shader_paths",
                 "dxbc_tessellation", "dxil_tessellation", "geometry_shader_stream_output"):
        fl0.append(row(name, UNVERIFIED, "complete mandatory GPU readback matrix not registered"))
    isolation = aggregate([row(name, probes.get(name, {"status": UNVERIFIED})["status"], "")
                           for name in ("shader_validation", "shader_container", "shader_stage_matrix")])
    invocation = probes.get("backend_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("compute_backend_failure_invocations", invocation["status"],
                   "test-linked production factory: exact AIRCONV/MSC failure call counts; no GPU dispatch"))
    graphics = probes.get("graphics_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("graphics_backend_failure_invocations", graphics["status"],
                   "ordinary VS/PS ordered compiler traces and mixed/wrong-stage precompiler rejection"))
    tessellation = probes.get("tessellation_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("tessellation_backend_failure_invocations", tessellation["status"],
                   "HS/DS ordered compiler traces and mixed/wrong-stage precompiler rejection"))
    geometry = probes.get("geometry_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("geometry_backend_failure_invocations", geometry["status"],
                   "GS and combined VS/GS ordered compiler traces; mixed/wrong-stage precompiler rejection"))
    mesh = probes.get("mesh_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("mesh_backend_failure_invocations", mesh["status"],
                   "native MS/AS/PS compiler traces; DXBC/wrong-stage precompiler rejection"))
    library = probes.get("pipeline_library_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("compute_pipeline_library_failure_invocations", library["status"],
                   "compute retained hits, metadata reload, backend failure and same-library retry"))
    graphics_library = probes.get("graphics_library_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("ordinary_graphics_pipeline_library_failure_invocations", graphics_library["status"],
                   "ordinary VS/PS retained hits, metadata reload, selected failures and same-library retry"))
    tess_library = probes.get("tessellation_library_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("tessellation_pipeline_library_failure_invocations", tess_library["status"],
                   "HS/DS retained hits, metadata reload, selected failures and same-library retry"))
    geom_library = probes.get("geometry_library_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("geometry_pipeline_library_failure_invocations", geom_library["status"],
                   "GS retained hits, metadata reload, combined failures and same-library retry"))
    shader_library = probes.get("shader_library_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("shader_library_converter_failure_invocations", shader_library["status"],
                   "six ray stages, selected-pass errors, converter retry/cache and precompiler rejection"))
    state_object = probes.get("state_object_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("state_object_failure_invocations", state_object["status"],
                   "six-stage candidate probing, AH/CH hints, failed publication and retry/cache"))
    addition = probes.get("state_object_addition_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("state_object_addition_failure_invocations", addition["status"],
                   "same-parent addition failure/retry/cache, inherited identifiers and parent immutability"))
    synthesis = probes.get("ray_synthesis_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("ray_synthesis_failure_invocations", synthesis["status"],
                   "dispatch/intersection query/materialization failures, same-object retry and retained state"))
    metal = probes.get("ray_metal_failure_oracle", {"status": UNVERIFIED})
    fl0.append(row("ray_metal_failure_invocations", metal["status"],
                   "lazy library/function-load and PSO/table/handle failures, complete-state publication and retry"))
    isolation = aggregate([row("PSO_contracts", isolation, ""), row("compute_invocations", invocation["status"], ""),
                           row("graphics_invocations", graphics["status"], ""),
                           row("tessellation_invocations", tessellation["status"], ""),
                           row("geometry_invocations", geometry["status"], ""), row("mesh_invocations", mesh["status"], ""),
                           row("compute_library_invocations", library["status"], ""),
                           row("ordinary_graphics_library_invocations", graphics_library["status"], ""),
                           row("tessellation_library_invocations", tess_library["status"], ""),
                           row("geometry_library_invocations", geom_library["status"], ""),
                           row("shader_library_converter_invocations", shader_library["status"], ""),
                           row("state_object_invocations", state_object["status"], ""),
                           row("state_object_addition_invocations", addition["status"], ""),
                           row("ray_synthesis_invocations", synthesis["status"], ""),
                           row("ray_metal_invocations", metal["status"], "")])
    fl0.append(row("backend_isolation", PARTIAL if isolation == PASS else isolation,
                   "bounded invocation probes; malformed/ambiguous precompiler rejection traces remain unverified"))
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
    probes["backend_failure_oracle"] = run_backend_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["graphics_failure_oracle"] = run_graphics_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["tessellation_failure_oracle"] = run_tessellation_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["geometry_failure_oracle"] = run_geometry_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["mesh_failure_oracle"] = run_mesh_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["pipeline_library_failure_oracle"] = run_pipeline_library_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["graphics_library_failure_oracle"] = run_graphics_library_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["tessellation_library_failure_oracle"] = run_tessellation_library_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["geometry_library_failure_oracle"] = run_geometry_library_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["shader_library_failure_oracle"] = run_shader_library_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["state_object_failure_oracle"] = run_state_object_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["state_object_addition_failure_oracle"] = run_state_object_addition_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["ray_synthesis_failure_oracle"] = run_ray_synthesis_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["ray_metal_failure_oracle"] = run_ray_metal_failure_oracle(directory, args.wine, args.timeout, runtime)
    probes["typed_uav_matrix"] = run_typed_uav_matrix(directory, args.wine, args.timeout, runtime)
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
