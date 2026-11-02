#!/usr/bin/env python3
"""Exercise preflight and failure atomicity for the offline AIR experiment."""
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture, sidecar_fixture, multi_fixture = (Path(arg).resolve() for arg in sys.argv[1:])
source = fixture.read_text()
sidecar_source = sidecar_fixture.read_text()
multi_source = multi_fixture.read_text()
sampler_source = (sidecar_source.replace('%tex = type opaque', '%tex = type opaque\n%sampler = type opaque')
                  .replace('  %data = call { <4 x i32>, i8 }',
                           '  %sampler = call %sampler addrspace(2)* @air.get_read_sampler()\n'
                           '  %data = call { <4 x i32>, i8 }')
                  .replace('(%tex addrspace(1)* %texture, i32 %index, i32 3)',
                           '(%tex addrspace(1)* %texture, %sampler addrspace(2)* %sampler, i32 %index, i32 3)')
                  .replace('@air.read_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, i32, i32)',
                           '@air.read_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, %sampler addrspace(2)*, i32, i32)')
                  + '\ndeclare %sampler addrspace(2)* @air.get_read_sampler()\n')
wrong_pointer = (sidecar_source.replace('i8 addrspace(2)* %root, i64 %slot',
                                        'i8 addrspace(2)* %root, i8 addrspace(2)* %other, i64 %slot')
                 .replace('i32 (i8 addrspace(2)*, i64, i32)* @main',
                          'i32 (i8 addrspace(2)*, i8 addrspace(2)*, i64, i32)* @main')
                 .replace('!3 = !{i32 0,', '!3 = !{i32 1,'))
with tempfile.TemporaryDirectory(prefix="dxmt-typed-buffer-ir-") as temp:
    root = Path(temp)
    cases = [
        ("no_typed_calls", "define i32 @main(i32 %x) { %y = add i32 %x, 1 ret i32 %y }\n", 7,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("internal_call", "define i32 @helper(i32 %x) { ret i32 %x }\n"
                          "define i32 @main(i32 %x) { %y = call i32 @helper(i32 %x) ret i32 %y }\n", 7,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("llvm_intrinsic", "declare i32 @llvm.bswap.i32(i32)\n"
                           "define i32 @main(i32 %x) { %y = call i32 @llvm.bswap.i32(i32 %x) ret i32 %y }\n", 7,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("unclassified_intrinsic", "declare i32 @llvm.ctpop.i32(i32)\n"
                                   "define i32 @main(i32 %x) { %y = call i32 @llvm.ctpop.i32(i32 %x) ret i32 %y }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("unknown_external", "declare void @external()\n define void @main() { call void @external() ret void }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("indirect_call", "define void @main(void ()* %f) { call void %f() ret void }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("fence_only", "declare void @air.fence_texture_buffer_1d()\n"
                       "define void @main() { call void @air.fence_texture_buffer_1d() ret void }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("null_only", "declare i1 @air.is_null_texture_buffer_1d()\n"
                      "define i1 @main() { %v = call i1 @air.is_null_texture_buffer_1d() ret i1 %v }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("unknown_air", "declare void @air.unknown()\n define void @main() { call void @air.unknown() ret void }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_unknown_external", sidecar_source.replace('  %data = call',
                                                          '  call void @external()\n  %data = call') +
                                   '\ndeclare void @external()\n', 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_unknown_air", sidecar_source.replace('  %data = call',
                                                     '  call void @air.unknown()\n  %data = call') +
                              '\ndeclare void @air.unknown()\n', 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("dynamic", source, 0, []),
        ("unknown", source.replace("air.get_width_texture_buffer_1d",
                                    "air.unknown_texture_buffer_1d"), 4, []),
        ("bad_field", source.replace("i64 %slot, i32 1", "i64 %slot, i32 0"), 4, []),
        ("sidecar_dynamic", sidecar_source, 0, ["--sidecar", "0", "8"]),
        ("sidecar_read_sampler", sampler_source, 0, ["--sidecar", "0", "8"]),
        ("sidecar_read_sampler_unknown", sampler_source.replace('air.get_read_sampler', 'air.unknown_sampler'), 4,
         ["--sidecar", "0", "8"]),
        ("metallib_api", sidecar_source, 0, ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("metallib_api_small", sidecar_source, 4, ["--metallib-api", "8", "8", "--sidecar", "0", "8"]),
        ("metallib_api_no_root", sidecar_source.split("!air.kernel")[0], 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("sidecar_status_used", sidecar_source.replace(
            '%vector = extractvalue { <4 x i32>, i8 } %data, 0',
            '%status = extractvalue { <4 x i32>, i8 } %data, 1\n'
            '  %vector = extractvalue { <4 x i32>, i8 } %data, 0'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_aggregate_used", sidecar_source.replace(
            '%vector = extractvalue { <4 x i32>, i8 } %data, 0',
            '%slot_result = alloca { <4 x i32>, i8 }\n'
            '  store { <4 x i32>, i8 } %data, { <4 x i32>, i8 }* %slot_result\n'
            '  %vector = extractvalue { <4 x i32>, i8 } %data, 0'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_bad_stride", sidecar_source.replace(
            '%desc = type { %tex addrspace(1)*, %tex addrspace(1)*, i64 }',
            '%desc = type { [2 x i64], %tex addrspace(1)*, i64 }'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_volatile", sidecar_source.replace('%texture = load %tex',
                                                     '%texture = load volatile %tex'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_bad_index_type", sidecar_source.replace(
            '%texture, i32 %index, i32 3)', '%texture, i64 0, i32 3)').replace(
            '@air.read_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, i32, i32)',
            '@air.read_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, i64, i32)'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_no_metadata", sidecar_source.split("!air.kernel")[0], 4, ["--sidecar", "0", "8"]),
        ("sidecar_wrong_binding", sidecar_source.replace('!"air.location_index", i32 2',
                                                        '!"air.location_index", i32 1'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_wrong_argument", sidecar_source.replace('!3 = !{i32 0,', '!3 = !{i32 1,'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_wrong_pointer", wrong_pointer, 4, ["--sidecar", "0", "8"]),
        ("sidecar_ambiguous_binding", sidecar_source.replace('!2 = !{!3}', '!2 = !{!3, !3}'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_multiple_stages", sidecar_source + '\n!air.fragment = !{!0}\n', 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_wrong_table", sidecar_source, 4, ["--sidecar", "16", "8"]),
        ("sidecar_unaligned", sidecar_source, 2, ["--sidecar", "0", "7"]),
        ("sidecar_negative", sidecar_source, 2, ["--sidecar", "0", "-8"]),
        ("sidecar_overlap", sidecar_source, 2, ["--sidecar", "0", "0"]),
        ("sidecar_unknown", sidecar_source.replace("air.get_width_texture_buffer_1d",
                                                    "air.unknown_texture_buffer_1d"), 4,
         ["--sidecar", "0", "8"]),
        ("multi", multi_source, 0, ["--sidecar", "0", "16", "--sidecar", "8", "24"]),
        ("multi_missing", multi_source, 4, ["--sidecar", "0", "16"]),
        ("multi_duplicate_table", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "0", "24"]),
        ("multi_duplicate_sidecar", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "8", "16"]),
        ("multi_cross_overlap", multi_source, 2, ["--sidecar", "0", "8", "--sidecar", "8", "24"]),
    ]
    for name, text, expected, options in cases:
        ir = root / (name + ".ll")
        output = root / (name + ".metallib")
        ir.write_text(text)
        result = subprocess.run([str(exe), str(ir), str(output), *options], capture_output=True, text=True)
        if result.returncode != expected:
            raise SystemExit(f"{name}: exit {result.returncode}; {result.stdout} {result.stderr}")
        if expected == 7 and "metallib API not applicable" not in result.stdout:
            raise SystemExit(f"{name}: no-op status missing")
        if (output.exists() and output.stat().st_size > 0) != (expected == 0):
            raise SystemExit(f"{name}: unexpected output state")
        count = 6 if name == "multi" else 3
        marker = 'metallib API patched' if name == 'metallib_api' else f'patched {count} typed-buffer operations'
        if expected == 0 and marker not in result.stdout:
            raise SystemExit(f"{name}: unexpected operation count: {result.stdout}")
        print(name, "passed")
