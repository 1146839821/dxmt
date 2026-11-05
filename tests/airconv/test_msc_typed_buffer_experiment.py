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
sample_call = ('  %sample = call { <4 x float>, i8 } @air.sample_texture_2d.v4f32('
               '%tex addrspace(1)* null, i8 addrspace(2)* null, <2 x float> zeroinitializer, '
               'i1 true, <2 x i32> zeroinitializer, i1 true, float 0.0, float 0.0, i32 0)\n')
sample_decl = ('\ndeclare { <4 x float>, i8 } @air.sample_texture_2d.v4f32('
               '%tex addrspace(1)*, i8 addrspace(2)*, <2 x float>, i1, <2 x i32>, i1, float, float, i32)\n')
mixed_sample = sidecar_source.replace('  %data = call', sample_call + '  %data = call') + sample_decl
atomic_call = '  %atomic = call i32 @air.atomic.global.add.s.i32(i32 addrspace(1)* null, i32 1, i32 0, i32 2, i1 true)\n'
atomic_decl = '\ndeclare i32 @air.atomic.global.add.s.i32(i32 addrspace(1)*, i32, i32, i32, i1)\n'
mixed_atomic = sidecar_source.replace('  %data = call', atomic_call + '  %data = call') + atomic_decl
standalone_sampler = (sidecar_source.replace('%tex = type opaque', '%tex = type opaque\n%sampler = type opaque')
                      .replace('  %data = call',
                               '  %extra = call %sampler addrspace(2)* @air.get_read_sampler()\n  %data = call')
                      + '\ndeclare %sampler addrspace(2)* @air.get_read_sampler()\n')
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
        ("air_popcount", "declare i32 @air.popcount.i32(i32)\n"
                         "define i32 @main(i32 %x) { %y = call i32 @air.popcount.i32(i32 %x) ret i32 %y }\n", 7,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_popcount_bad_type", "declare i64 @air.popcount.i32(i64)\n"
                                  "define i64 @main(i64 %x) { %y = call i64 @air.popcount.i32(i64 %x) ret i64 %y }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_popcount_unknown", "declare i32 @air.popcount.other(i32)\n"
                                 "define i32 @main(i32 %x) { %y = call i32 @air.popcount.other(i32 %x) ret i32 %y }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_reverse", "declare i32 @air.reverse_bits.i32(i32)\n"
                        "define i32 @main(i32 %x) { %y = call i32 @air.reverse_bits.i32(i32 %x) ret i32 %y }\n", 7,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_reverse_bad_type", "declare i64 @air.reverse_bits.i32(i64)\n"
                                 "define i64 @main(i64 %x) { %y = call i64 @air.reverse_bits.i32(i64 %x) ret i64 %y }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_reverse_unknown", "declare i32 @air.reverse_bits.other(i32)\n"
                                "define i32 @main(i32 %x) { %y = call i32 @air.reverse_bits.other(i32 %x) ret i32 %y }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("air_reverse_vararg", "declare i32 @air.reverse_bits.i32(i32, ...)\n"
                               "define i32 @main(i32 %x) { %y = call i32 (i32, ...) @air.reverse_bits.i32(i32 %x) ret i32 %y }\n", 4,
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
        ("atomic_only", atomic_decl + "define i32 @main() { " + atomic_call + "ret i32 %atomic }\n", 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_unknown_external", sidecar_source.replace('  %data = call',
                                                          '  call void @external()\n  %data = call') +
                                   '\ndeclare void @external()\n', 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_unknown_air", sidecar_source.replace('  %data = call',
                                                     '  call void @air.unknown()\n  %data = call') +
                              '\ndeclare void @air.unknown()\n', 4,
         ["--metallib-classify", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_popcount", sidecar_source.replace('  %data = call',
                                                 '  %bits = call i32 @air.popcount.i32(i32 %index)\n  %data = call') +
                           '\ndeclare i32 @air.popcount.i32(i32)\n', 0,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_popcount_bad", sidecar_source.replace('  %data = call',
                                                     '  %bits = call i64 @air.popcount.i32(i64 1)\n  %data = call') +
                               '\ndeclare i64 @air.popcount.i32(i64)\n', 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_reverse", sidecar_source.replace('  %data = call',
                                                '  %bits = call i32 @air.reverse_bits.i32(i32 %index)\n  %data = call') +
                          '\ndeclare i32 @air.reverse_bits.i32(i32)\n', 0,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_reverse_bad", sidecar_source.replace('  %data = call',
                                                    '  %bits = call i64 @air.reverse_bits.i32(i64 1)\n  %data = call') +
                              '\ndeclare i64 @air.reverse_bits.i32(i64)\n', 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_sample", mixed_sample, 0, ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_sample_unknown", mixed_sample.replace('air.sample_texture_2d.v4f32', 'air.sample_texture_2d.unknown'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_sample_bad_address_space", mixed_sample.replace('i8 addrspace(2)* null', 'i8 addrspace(1)* null').replace(
            '%tex addrspace(1)*, i8 addrspace(2)*, <2 x float>', '%tex addrspace(1)*, i8 addrspace(1)*, <2 x float>'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_sample_bad_return", mixed_sample.replace('{ <4 x float>, i8 }', '{ <4 x float>, i32 }'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_sample_packed_return", mixed_sample.replace('{ <4 x float>, i8 }', '<{ <4 x float>, i8 }>'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic", mixed_atomic, 0, ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_six_args", mixed_atomic.replace('i32 2, i1 true', 'i32 2, i32 1, i1 true').replace(
            'i32, i32, i32, i1)', 'i32, i32, i32, i32, i1)'), 0,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_bad_extra", mixed_atomic.replace('i32 2, i1 true', 'i32 2, i64 1, i1 true').replace(
            'i32, i32, i32, i1)', 'i32, i32, i32, i64, i1)'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_unknown", mixed_atomic.replace('air.atomic.global.add.s.i32', 'air.atomic.global.add.u.i32'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_address_space", mixed_atomic.replace('i32 addrspace(1)*', 'i32 addrspace(2)*'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_bad_flag", mixed_atomic.replace('i1 true', 'i32 1').replace('i32, i1)', 'i32, i32)'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("mixed_atomic_bad_result", mixed_atomic.replace('call i32 @air.atomic', 'call i64 @air.atomic').replace(
            'declare i32 @air.atomic', 'declare i64 @air.atomic'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("dynamic", source, 0, []),
        ("unknown", source.replace("air.get_width_texture_buffer_1d",
                                    "air.unknown_texture_buffer_1d"), 4, []),
        ("bad_field", source.replace("i64 %slot, i32 1", "i64 %slot, i32 0"), 4, []),
        ("sidecar_dynamic", sidecar_source, 0, ["--sidecar", "0", "8"]),
        ("sidecar_read_sampler", sampler_source, 0, ["--sidecar", "0", "8"]),
        ("standalone_read_sampler", standalone_sampler, 0, ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("standalone_sampler_wrong_space", standalone_sampler.replace('%sampler addrspace(2)*', '%sampler addrspace(1)*'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("standalone_sampler_wrong_return", standalone_sampler.replace('%sampler addrspace(2)*', 'i32'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("standalone_sampler_argument", standalone_sampler.replace('@air.get_read_sampler()', '@air.get_read_sampler(i32 1)', 1).replace(
            'declare %sampler addrspace(2)* @air.get_read_sampler()', 'declare %sampler addrspace(2)* @air.get_read_sampler(i32)'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("standalone_sampler_defined", standalone_sampler.replace(
            'declare %sampler addrspace(2)* @air.get_read_sampler()',
            'define %sampler addrspace(2)* @air.get_read_sampler() { ret %sampler addrspace(2)* null }'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("standalone_sampler_vararg", standalone_sampler.replace(
            'call %sampler addrspace(2)* @air.get_read_sampler()',
            'call %sampler addrspace(2)* (...) @air.get_read_sampler()').replace(
            'declare %sampler addrspace(2)* @air.get_read_sampler()',
            'declare %sampler addrspace(2)* @air.get_read_sampler(...)'), 4,
         ["--metallib-api", "8", "16", "--sidecar", "0", "8"]),
        ("read_sampler_wrong_space", sampler_source.replace('%sampler addrspace(2)*', '%sampler addrspace(1)*'), 4,
         ["--sidecar", "0", "8"]),
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
        ("sidecar_unknown_read", sidecar_source.replace('air.read_texture_buffer_1d.s.v4i32',
                                                        'air.read_texture_buffer_1d.unknown'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_width_bad_return", sidecar_source.replace('call i32 @air.get_width_texture_buffer_1d',
            'call i64 @air.get_width_texture_buffer_1d').replace('declare i32 @air.get_width_texture_buffer_1d',
            'declare i64 @air.get_width_texture_buffer_1d').replace('ret i32 %width', 'ret i32 0'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_unknown_write", sidecar_source.replace('air.write_texture_buffer_1d.s.v4i32',
                                                         'air.write_texture_buffer_1d.unknown'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_read_bad_vector", sidecar_source.replace('{ <4 x i32>, i8 }', '{ <4 x float>, i8 }').replace(
            '%vector = extractvalue { <4 x float>, i8 } %data, 0',
            '%unused = extractvalue { <4 x float>, i8 } %data, 0\n  %vector = insertelement <4 x i32> zeroinitializer, i32 0, i32 0'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_read_packed", sidecar_source.replace('{ <4 x i32>, i8 }', '<{ <4 x i32>, i8 }>'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_write_bad_vector", sidecar_source.replace('<4 x i32> %vector, i32 3)', '<2 x i32> zeroinitializer, i32 3)').replace(
            '@air.write_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, i32, <4 x i32>, i32)',
            '@air.write_texture_buffer_1d.s.v4i32(%tex addrspace(1)*, i32, <2 x i32>, i32)'), 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_fence_null", sidecar_source.replace('  %data = call',
            '  call void @air.fence_texture_buffer_1d(%tex addrspace(1)* %texture)\n'
            '  %null_test = call i1 @air.is_null_texture_buffer_1d(%tex addrspace(1)* %texture)\n  %data = call') +
            '\ndeclare void @air.fence_texture_buffer_1d(%tex addrspace(1)*)\n'
            'declare i1 @air.is_null_texture_buffer_1d(%tex addrspace(1)*)\n', 0,
         ["--sidecar", "0", "8"]),
        ("sidecar_fence_bad", sidecar_source.replace('  %data = call',
            '  call void @air.fence_texture_buffer_1d(i32 0)\n  %data = call') +
            '\ndeclare void @air.fence_texture_buffer_1d(i32)\n', 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_null_bad", sidecar_source.replace('  %data = call',
            '  %null_test = call i32 @air.is_null_texture_buffer_1d(%tex addrspace(1)* %texture)\n  %data = call') +
            '\ndeclare i32 @air.is_null_texture_buffer_1d(%tex addrspace(1)*)\n', 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_null_unused_bad", sidecar_source +
            '\ndeclare i32 @air.is_null_texture_buffer_1d(%tex addrspace(1)*)\n', 4,
         ["--sidecar", "0", "8"]),
        ("sidecar_null_name_global", sidecar_source +
            '\n@air.is_null_texture_buffer_1d = external global i8\n', 4,
         ["--sidecar", "0", "8"]),
        ("multi", multi_source, 0, ["--sidecar", "0", "16", "--sidecar", "8", "24"]),
        ("multi_missing", multi_source, 4, ["--sidecar", "0", "16"]),
        ("multi_duplicate_table", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "0", "24"]),
        ("multi_duplicate_sidecar", multi_source, 2, ["--sidecar", "0", "16", "--sidecar", "8", "16"]),
        ("multi_cross_overlap", multi_source, 2, ["--sidecar", "0", "8", "--sidecar", "8", "24"]),
    ]
    # MSC 4.0.3 lowers RWBuffer integer atomics to texture-buffer AIR calls,
    # unlike the allowed global pointer atomic above. Reject the entire module
    # before modifying earlier supported reads/writes or publishing a metallib.
    typed_atomics = ["fetch_" + op for op in ("add", "and", "or", "xor", "min", "max")]
    typed_atomics += ["exchange", "compare_exchange_weak"]
    for operation in typed_atomics:
        signs = ("u", "s") if operation in ("fetch_min", "fetch_max", "compare_exchange_weak") else ("u",)
        for sign in signs:
            symbol = f"air.atomic_{operation}_explicit_texture_buffer_1d.{sign}.v4i32"
            compare = operation == "compare_exchange_weak"
            result_type = "i1" if compare else "<4 x i32>"
            values = ("<4 x i32>* null, <4 x i32> zeroinitializer, i32 0, i32 0, i32 3" if compare else
                      "<4 x i32> zeroinitializer, i32 0, i32 3")
            types = "<4 x i32>*, <4 x i32>, i32, i32, i32" if compare else "<4 x i32>, i32, i32"
            call = f"  %atomic = call {result_type} @{symbol}(%tex addrspace(1)* %texture, i32 %index, {values})\n"
            declaration = f"\ndeclare {result_type} @{symbol}(%tex addrspace(1)*, i32, {types})\n"
            text = sidecar_source.replace("  ret i32 %width", call + "  ret i32 %width") + declaration
            for mode, options in (("unchanged", ["--sidecar", "0", "8"]),
                                  ("no_output", ["--metallib-api", "8", "16", "--sidecar", "0", "8"])):
                cases.append((f"typed_atomic_{operation}_{sign}_{mode}", text, 4, options))
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
        marker = 'metallib API patched' if '--metallib-api' in options else f'patched {count} typed-buffer operations'
        if expected == 0 and marker not in result.stdout:
            raise SystemExit(f"{name}: unexpected operation count: {result.stdout}")
        print(name, "passed")
