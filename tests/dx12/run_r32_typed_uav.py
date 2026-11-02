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
    args = parser.parse_args()
    if (args.scalar or args.unaligned or args.zero_offset or args.experimental_patch or args.late_copy or args.heap_switch or args.pending_reset or args.indirect_reject or args.indirect or args.indirect_count or args.indirect_count_wrong or args.indirect_pending or args.indirect_pending_wrong) and not args.buffer:
        parser.error("--scalar, --unaligned and --zero-offset require --buffer")
    if sum((args.zero_offset, args.unaligned, args.late_copy, args.heap_switch, args.pending_reset, args.indirect_reject, args.indirect, args.indirect_count, args.indirect_count_wrong, args.indirect_pending, args.indirect_pending_wrong)) > 1:
        parser.error("offset, late-copy and heap-switch modes are mutually exclusive")
    if args.encoder_break and not args.heap_switch:
        parser.error("--encoder-break requires --heap-switch")
    stage, prefix, build, converter = (getattr(args, name).resolve(strict=True)
                                       for name in ("stage", "prefix", "build", "converter"))
    pairs = [("src/d3d12/d3d12.dll", "d3d12.dll"), ("src/dxgi/dxgi.dll", "dxgi.dll"),
             ("src/winemetal/winemetal.dll", "winemetal.dll"),
             ("src/winemetal/unix/winemetal.so", "wine/lib/wine/x86_64-unix/winemetal.so"),
             ("tests/dx12/dx12_texture_sampler.exe", "dx12_texture_sampler.exe")]
    cases = [("float", 41), ("uint", 42), ("sint", 43)]
    prefix_name = "typed_buffer_scalar" if args.scalar else "typed_buffer_uav" if args.buffer else "texture_sampler_typed_uav"
    pairs += [(f"tests/dx12/{prefix_name}_{kind}.cs.cso",
               f"{prefix_name}_{kind}.cs.cso") for kind, _ in cases]
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
    if args.experimental_patch:
        env["DXMT_TEST_MSC_TYPED_BUFFER_PATCH"] = "1"
    identities = [f"LOADED_CODE {dll} {symbol} {pe_export_bytes(stage / dll, symbol)}" for dll, symbol in
                  (("d3d12.dll", "D3D12CreateDevice"), ("winemetal.dll", "DXMTMSCPatchSamplerLOD"))]
    rows = []
    for kind, fmt in cases:
        env["DXMT_SHADER_CACHE_PATH"] = str(output / f"cache-{kind}")
        env["DXMT_LOG_PATH"] = str(output)
        command = [str(stage / "wine/bin/wine"), "dx12_texture_sampler.exe",
                   f"{prefix_name}_{kind}.cs.cso", "--typed-buffer-indirect-pending-wrong" if args.indirect_pending_wrong else "--typed-buffer-indirect-pending" if args.indirect_pending else "--typed-buffer-indirect-count-wrong" if args.indirect_count_wrong else "--typed-buffer-indirect-count" if args.indirect_count else "--typed-buffer-indirect-reject" if args.indirect_reject else "--typed-buffer-indirect" if args.indirect else "--typed-buffer-pending-reset" if args.pending_reset else "--typed-buffer-heap-switch-encoder" if args.encoder_break else "--typed-buffer-heap-switch" if args.heap_switch else "--typed-buffer-late-copy" if args.late_copy else "--typed-buffer-zero" if args.zero_offset else "--typed-buffer-unaligned" if args.unaligned else "--typed-buffer" if args.buffer else "--typed-uav", str(fmt)]
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
                  (not args.heap_switch or "heap switch dimensions=4,2 passed" in log) and
                  (not args.encoder_break or "encoder break copy passed" in log) and
                  (not (args.pending_reset or args.indirect_pending or args.indirect_pending_wrong) or "pending reset fence passed" in log))
        if args.indirect_count or args.indirect_count_wrong or args.indirect_pending or args.indirect_pending_wrong:
            passed = passed and "indirect count sums=0,4,8,8,8 passed" in log
        if args.indirect_reject:
            passed = result.returncode == 0 and "typed buffer indirect rejected explicitly" in log
        rows.append(dict(kind=kind, format=fmt, exit=result.returncode, readback_pass=passed,
                         identity_pass=identity, validation_pass=validation))
        print(rows[-1], flush=True)
    summary = {"cases": rows, "converter_sha256": sha(converter),
               "artifacts": {target: sha(stage / target) for _, target in pairs}}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return 0 if all(r["readback_pass"] and r["identity_pass"] and r["validation_pass"] for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
