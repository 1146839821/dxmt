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
    parser.add_argument("--buffer", action="store_true", help="test typed buffer UAVs instead of textures")
    parser.add_argument("--scalar", action="store_true", help="use scalar buffer shader declarations")
    parser.add_argument("--unaligned", action="store_true", help="use an offset within the aligned texture view")
    parser.add_argument("--zero-offset", action="store_true", help="use a zero-offset texture view")
    parser.add_argument("--experimental-patch", action="store_true", help="enable the experimental MSC sidecar path")
    parser.add_argument("--production-gate", action="store_true", help="enable the opt-in MSC typed-buffer execution gate")
    parser.add_argument("--atomic", choices=("add", "and", "or", "xor", "min", "max", "exchange", "compare-exchange", "compare-store"), help="expect explicit sidecar PSO rejection for a real typed atomic shader; no GPU submission")
    parser.add_argument("--cache-isolation", choices=("patched", "live", "serialized", "wrong"), help="check gate transitions, PSO/library persistence and conversion-cache recovery")
    parser.add_argument("--no-compute-residency", action="store_true", help="verify rejection is independent of the MSC residency debug switch")
    parser.add_argument("--null-update", choices=("late-null", "late-live", "late-null-outside", "replay-null", "direct-null"), help="test submission-time typed NULL validation")
    parser.add_argument("--unused-null", action="store_true", help="put a typed NULL outside the bound root-table ranges")
    parser.add_argument("--late-copy", action="store_true", help="copy the buffer view after Close and before submission")
    parser.add_argument("--heap-switch", action="store_true", help="switch between heaps with distinct view lengths")
    parser.add_argument("--encoder-break", action="store_true", help="insert a blit between the two heap-switch dispatches")
    parser.add_argument("--pending-reset", action="store_true", help="reset the list while its submission waits on a fence")
    parser.add_argument("--indirect-reject", action="store_true", help="verify experimental indirect dispatch fails explicitly")
    parser.add_argument("--indirect", action="store_true", help="execute the scalar workload with indirect dispatch")
    parser.add_argument("--indirect-count", action="store_true", help="check multi-command and count-buffer dispatch")
    parser.add_argument("--indirect-count-wrong", action="store_true", help="negative control with deliberately wrong expected count")
    parser.add_argument("--indirect-pending", action="store_true", help="reset a pending multi-command indirect submission")
    parser.add_argument("--indirect-pending-wrong", action="store_true", help="pending submission with deliberately wrong expected count")
    parser.add_argument("--rerecord", action="store_true", help="re-record while pending and submit after completion")
    parser.add_argument("--rerecord-wrong", action="store_true", help="use a deliberately wrong second view dimension")
    parser.add_argument("--rerecord-zero", action="store_true", help="negative control with zero indirect command count")
    parser.add_argument("--indirect-late", action="store_true", help="copy the typed buffer descriptor after Close before indirect submission")
    parser.add_argument("--indirect-late-skip", action="store_true", help="negative control without the late descriptor copy")
    parser.add_argument("--indirect-heap", action="store_true", help="indirect dispatch across heap switch and encoder break")
    parser.add_argument("--indirect-heap-wrong", action="store_true", help="negative control with wrong heap dimension")
    parser.add_argument("--indirect-math", action="store_true", help="run bit operations during indirect heap transitions")
    parser.add_argument("--indirect-math-wrong", action="store_true", help="negative control with wrong reversebits result")
    parser.add_argument("--null-uav", action="store_true", help="verify null typed UAV reads and discarded writes")
    parser.add_argument("--null-uav-live", action="store_true", help="negative control using a live UAV")
    parser.add_argument("--srv", choices=("null", "live", "null-to-live", "live-to-null", "null-to-live-skip", "live-to-null-skip", "null-to-live-only", "null-to-live-only-skip", "null-to-live-only-pending", "null-to-live-only-pending-skip", "null-to-live-only-rerecord", "null-to-live-only-rerecord-wrong", "copy-to-live", "copy-to-null", "copy-to-live-skip", "copy-to-null-skip", "multi", "multi-skip", "multi-wrong", "multi-merge", "multi-merge-skip", "multi-split", "multi-split-skip"), help="test a typed SRV and optional post-Close update")
    args = parser.parse_args()
    if args.cache_isolation and (not args.buffer or not args.scalar or
                                 args.production_gate == args.experimental_patch or
                                 any(value for name, value in vars(args).items() if name not in (
                                     "stage", "prefix", "build", "converter", "output", "cache_isolation",
                                     "buffer", "scalar", "production_gate", "experimental_patch"))):
        parser.error("--cache-isolation requires --buffer --scalar and exactly one sidecar gate, with no other test mode")
    if args.atomic and (not args.buffer or not args.scalar or not args.production_gate or
                        any(value for name, value in vars(args).items() if name not in (
                            "stage", "prefix", "build", "converter", "output", "atomic",
                            "buffer", "scalar", "production_gate"))):
        parser.error("--atomic requires --buffer --scalar --production-gate and no other test mode")
    if args.no_compute_residency and args.null_update != "late-null":
        parser.error("--no-compute-residency is limited to the pre-submission rejection probe")
    if args.null_update and (args.unused_null or args.buffer or args.experimental_patch or args.production_gate):
        parser.error("--null-update requires the default texture path with the sidecar gate disabled")
    if args.unused_null and (args.buffer or args.experimental_patch or args.production_gate):
        parser.error("--unused-null requires the default texture path with the sidecar gate disabled")
    if args.experimental_patch and args.production_gate:
        parser.error("--experimental-patch and --production-gate are mutually exclusive")
    if args.srv and (not args.buffer or not args.scalar or any(getattr(args, name) for name in (
        "null_uav", "null_uav_live", "unaligned", "zero_offset", "late_copy", "heap_switch",
        "encoder_break", "pending_reset", "indirect_reject", "indirect", "indirect_count",
        "indirect_count_wrong", "indirect_pending", "indirect_pending_wrong", "rerecord",
        "rerecord_wrong", "rerecord_zero", "indirect_late", "indirect_late_skip",
        "indirect_heap", "indirect_heap_wrong", "indirect_math", "indirect_math_wrong"))):
        parser.error("--srv requires --buffer --scalar and cannot be combined with other test modes")
    if (args.scalar or args.unaligned or args.zero_offset or args.experimental_patch or args.late_copy or args.heap_switch or args.pending_reset or args.indirect_reject or args.indirect or args.indirect_count or args.indirect_count_wrong or args.indirect_pending or args.indirect_pending_wrong or args.rerecord or args.rerecord_wrong or args.rerecord_zero or args.indirect_late or args.indirect_late_skip or args.indirect_heap or args.indirect_heap_wrong or args.indirect_math or args.indirect_math_wrong or args.null_uav or args.null_uav_live) and not args.buffer:
        parser.error("--scalar, --unaligned and --zero-offset require --buffer")
    if sum((args.zero_offset, args.unaligned, args.late_copy, args.heap_switch, args.pending_reset, args.indirect_reject, args.indirect, args.indirect_count, args.indirect_count_wrong, args.indirect_pending, args.indirect_pending_wrong, args.rerecord, args.rerecord_wrong, args.rerecord_zero, args.indirect_late, args.indirect_late_skip, args.indirect_heap, args.indirect_heap_wrong, args.indirect_math, args.indirect_math_wrong, args.null_uav, args.null_uav_live)) > 1:
        parser.error("offset, late-copy and heap-switch modes are mutually exclusive")
    if args.encoder_break and not args.heap_switch:
        parser.error("--encoder-break requires --heap-switch")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    pairs = [("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_texture_sampler.exe", "dx12_texture_sampler.exe")]
    cases = [("uint", 42)] if args.null_update == "direct-null" else [("float", 41), ("uint", 42), ("sint", 43)]
    if args.atomic:
        cases = [("uint", 42), ("sint", 43)]
    if (args.indirect_math or args.indirect_math_wrong) and not args.scalar:
        parser.error("--indirect-math requires --scalar")
    if (args.null_uav or args.null_uav_live) and not args.scalar:
        parser.error("--null-uav requires --scalar")
    prefix_name = "typed_buffer_srv_multi" if args.srv and args.srv.startswith("multi") else "typed_buffer_srv" if args.srv else "typed_buffer_math" if args.indirect_math or args.indirect_math_wrong else "typed_buffer_scalar" if args.scalar else "typed_buffer_uav" if args.buffer else "texture_sampler_typed_uav"
    if args.atomic:
        prefix_name = "typed_buffer_atomic_" + args.atomic
    fixtures = {kind: "texture_sampler_direct_indexed.cs.cso" if args.null_update == "direct-null" else
                f"{prefix_name}_{kind}.cs.cso" for kind, _ in cases}
    pairs += [(f"tests/dx12/{name}", name) for name in fixtures.values()]
    atomic_fixtures = {kind: f"typed_buffer_atomic_add_{'sint' if kind == 'sint' else 'uint'}.cs.cso"
                       for kind, _ in cases} if args.cache_isolation else {}
    pairs += [(f"tests/dx12/{name}", name) for name in sorted(set(atomic_fixtures.values()))]
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
    if args.no_compute_residency:
        env["DXMT_MSC_COMPUTE_RESIDENCY"] = "0"
    if args.experimental_patch:
        env["DXMT_TEST_MSC_TYPED_BUFFER_PATCH"] = "1"
    if args.production_gate:
        env["DXMT_MSC_TYPED_BUFFER_EXECUTION"] = "1"
    else:
        env.pop("DXMT_MSC_TYPED_BUFFER_EXECUTION", None)
    if args.cache_isolation:
        env["DXMT_MSC_CACHE_TRACE"] = "1"
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    rows = []
    for kind, fmt in cases:
        env["DXMT_SHADER_CACHE_PATH"] = str(output / f"cache-{kind}")
        env["DXMT_LOG_PATH"] = str(output)
        command = [str(stage / "wine/bin/wine"), "dx12_texture_sampler.exe",
                   fixtures[kind], "--typed-buffer-null-live" if args.null_uav_live else "--typed-buffer-null" if args.null_uav else "--typed-buffer-indirect-math-wrong" if args.indirect_math_wrong else "--typed-buffer-indirect-math" if args.indirect_math else "--typed-buffer-indirect-heap-wrong" if args.indirect_heap_wrong else "--typed-buffer-indirect-heap" if args.indirect_heap else "--typed-buffer-indirect-late-skip" if args.indirect_late_skip else "--typed-buffer-indirect-late" if args.indirect_late else "--typed-buffer-rerecord-zero" if args.rerecord_zero else "--typed-buffer-rerecord-wrong" if args.rerecord_wrong else "--typed-buffer-rerecord" if args.rerecord else "--typed-buffer-indirect-pending-wrong" if args.indirect_pending_wrong else "--typed-buffer-indirect-pending" if args.indirect_pending else "--typed-buffer-indirect-count-wrong" if args.indirect_count_wrong else "--typed-buffer-indirect-count" if args.indirect_count else "--typed-buffer-indirect-reject" if args.indirect_reject else "--typed-buffer-indirect" if args.indirect else "--typed-buffer-pending-reset" if args.pending_reset else "--typed-buffer-heap-switch-encoder" if args.encoder_break else "--typed-buffer-heap-switch" if args.heap_switch else "--typed-buffer-late-copy" if args.late_copy else "--typed-buffer-zero" if args.zero_offset else "--typed-buffer-unaligned" if args.unaligned else "--typed-buffer" if args.buffer else "--typed-uav", str(fmt)]
        if args.srv:
            command[3] = "--typed-buffer-srv-" + args.srv
        if args.atomic:
            command[3] = "--typed-buffer-atomic-reject"
        if args.cache_isolation:
            command[3] = "--typed-buffer-cache-" + args.cache_isolation
            command.append(atomic_fixtures[kind])
        if args.unused_null:
            command[3] = "--typed-uav-null-outside"
        if args.null_update:
            command[3] = "--typed-uav-" + args.null_update
            if args.null_update == "direct-null":
                command = command[:3] + ["--direct-indexed-late-null"]
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
        passed = (result.returncode == 0 and expected in log and
                  (not args.buffer or "typed buffer guards and out-of-bounds load passed" in log) and
                  (not (args.heap_switch or args.indirect_heap or args.indirect_heap_wrong or args.indirect_math or args.indirect_math_wrong) or "heap switch dimensions=4,2 passed" in log) and
                  (not (args.encoder_break or args.indirect_heap or args.indirect_heap_wrong or args.indirect_math or args.indirect_math_wrong) or "encoder break copy passed" in log) and
                  (not (args.pending_reset or args.indirect_pending or args.indirect_pending_wrong or args.rerecord or args.rerecord_wrong or args.rerecord_zero) or "pending reset fence passed" in log))
        if args.unused_null:
            passed = passed and "typed NULL outside root tables slot=3" in log
        if args.atomic:
            passed = (result.returncode == 0 and
                      "typed atomic PSO rejected; no command list submitted; device healthy" in log and
                      "MSC typed-buffer test patch rejected: result=0 status=2" in log)
        cache_events = re.findall(r"MSC_CACHE event=(\w+) entries=(\d+) metallib_bytes=(\d+)", log)
        cache_pass = True
        if args.cache_isolation:
            cache_pass = (sum(e[0] == "miss" for e in cache_events) == 2 and
                          sum(e[0] == "insert" for e in cache_events) == 2 and
                          sum(e[0] == "hit" for e in cache_events) >= 5 and
                          cache_events[-1][1] == "2" and
                          len({e[2] for e in cache_events if e[1] == "2"}) == 1)
            passed = (passed and cache_pass and
                      "typed cache atomic failure-first/retry recovery passed; no atomic submission" in log and
                      "typed cache raw token/patched export/live+serialized stream isolation passed" in log and
                      f"typed cache descriptor elements={64 if args.cache_isolation in ('live', 'serialized') else 4} readback passed" in log and
                      "typed cache execution mode=--typed-buffer-cache-" + args.cache_isolation in log)
        if args.null_update:
            updated = "typed NULL post-Close update mode=--typed-uav-" + args.null_update in log
            rejected = "typed NULL submission rejected; readback guard unchanged" in log
            if args.null_update in ("late-null", "direct-null"):
                if args.null_update == "direct-null":
                    updated = "typed NULL post-Close direct-indexed update" in log
                passed = result.returncode == 0 and updated and rejected
            else:
                passed = passed and updated
                if args.null_update == "late-live":
                    passed = passed and "typed NULL recorded before live update" in log
                if args.null_update == "replay-null":
                    passed = passed and rejected and "typed NULL replay rejection passed" in log
        if args.rerecord or args.rerecord_wrong or args.rerecord_zero:
            passed = passed and "rerecord dimensions=2 passed" in log
        if args.indirect_math or args.indirect_math_wrong:
            passed = passed and "indirect countbits=1,1 passed" in log
            passed = passed and "indirect reversebits=20000000,40000000 passed" in log
        if args.indirect_count or args.indirect_count_wrong or args.indirect_pending or args.indirect_pending_wrong or args.rerecord or args.rerecord_wrong or args.rerecord_zero or args.indirect_late or args.indirect_late_skip:
            passed = passed and "indirect count sums=0,4,8,8,8 passed" in log
        if args.indirect_reject:
            passed = result.returncode == 0 and "typed buffer indirect rejected explicitly" in log
        if args.null_uav or args.null_uav_live:
            passed = result.returncode == 0 and "null UAV reads=0 dimensions=0 guard unchanged passed" in log
        if args.srv:
            passed = result.returncode == 0 and f"{args.srv.removesuffix('-skip').removesuffix('-wrong')} typed SRV read and dimensions passed" in log
            if "-pending" in args.srv or "-rerecord" in args.srv:
                passed = passed and "SRV pending gate held passed" in log
            if "-rerecord" in args.srv:
                passed = passed and "SRV rerecorded while pending passed" in log
        rows.append(dict(kind=kind, format=fmt, exit=result.returncode, case_pass=passed,
                         readback_pass=passed and not args.atomic,
                         oracle="PSO_UNSUPPORTED_NO_SUBMISSION" if args.atomic else "existing scenario",
                         identity_pass=identity, validation_pass=validation,
                         cache_pass=cache_pass, cache_events=cache_events))
        print(rows[-1], flush=True)
    summary = {"cases": rows, "converter_sha256": sha(converter),
               "artifacts": {target: sha(stage / target) for _, target in pairs}}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if all(r["case_pass"] and r["identity_pass"] and r["validation_pass"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
