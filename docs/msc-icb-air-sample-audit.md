# MSC ICB constant-load sample audit

This is a small local corpus inspection, not a capability claim or a general
MSC output survey. The experimental inspector operates offline; it does not
modify the input or participate in runtime shader compilation.

The samples below were inspected on macOS 27.0.1 using the LLVM 15-based
`msc-icb-constant-experiment --inspect` tool. Each had an `air64` target and
reported a 64-bit address-space-2 pointer index. The layout itself specifies a
64-bit default pointer layout and no separate address-space-2 pointer layout.
The MSC converter version and the compiler options of all samples have not
been independently verified.

| Local sample | SHA-256 | Dynamic AS2 GEP loads | Direct-global loads | Eligible loads |
|---|---|---:|---:|---:|
| `n79-vertexid-repro/cached0.metallib` | `d5a810b785c652ec18eee87f4421a4d3f69b6cb173b26d2260ca57a630f6d3fb` | 2 | 2 | 2 |
| `n79-vertexid-repro/cached1.metallib` | `3750bde7e33b10b94af3f70e5b586fad2702fac650a12f615dd5ba3caec4aeea` | 0 | 0 | 0 |
| `n79-vertexid-repro/stage/fragment-original.metallib` | `be176c097f58fcccb4a8aced29a8399d8740818f20da1d5f9c15ddcdc1864a5e` | 0 | 0 | 0 |
| `n25-lod-pass-20261030/dynamic.metallib` | `f0bde82502e07512c5431dbdd8f23f202ed7472ac3d56b5c8dc16b5733185124` | 0 | 0 | 0 |

Paths are relative to the local `dxmt-test` directory and are not repository
fixtures. The first sample contains a six-element float constant array and two
dynamic scalar loads. The other three samples have no loads in the inspector's
narrowly defined direct two-index AS2 GEP category. This does not rule out
other constant-access representations. Run `--inspect` on additional metallibs
and record their identities before extending these conclusions.

Prior GPU validation of the first sample's rewritten metallib covered direct
and indirect rendering under Metal validation. It does not establish GPU
behavior for all array shapes, out-of-range inputs, or converter versions.

## Reproducible CLI corpus

`tests/airconv/run_msc_icb_corpus.py` compiles three checked-in vertex shaders
with `vs_6_0`, both `-O0` and `-O3`, and invokes MSC with its default CLI
options. It creates a new output directory and records the exact command
arguments, source and binary hashes, tool hashes, versions and inspector output
in `manifest.json`. It requires a prepared isolated Wine prefix and does not
install DLLs. Failed runs may leave partial files in their new output directory.

A local run with Wine 11.0, DXC 1.9.2602.17 and metal-irconverter 4.0.3
produced six metallibs targeting `air64-apple-macosx14.0.0`. Both
`texture_lod_vertexid.hlsl` variants had two eligible loads; both variants of
`texture_dimensions_sm6.hlsl` (compiled with `MS_WRITE=1`) and `dx12_hdr.hlsl`
had none. The detailed local manifest is at
`dxmt-test/n85-msc-corpus/repro2/manifest.json`. These are compile-and-inspect
results, not GPU execution results, and the CLI converter settings do not prove
identity with DXMT's runtime MSC configuration.

## Runtime configuration comparison

At repository HEAD `6fc5ef38`, the WineMetal bridge loads the MSC dylib using
`DXMT_METALIRCONVERTER_PATH`, if set, or its ordered default paths
(`src/winemetal/unix/metalirconverter.c`, `dxmt_msc_initialize`). Its log's
`API 4.0.1` comes from the repository's `IR_VERSION_*` header macros; it is a
compile-time API-header version, not a queried version of the loaded library.
The recorded n84 dyld log reports `/usr/local/lib/libmetalirconverter.dylib`.
That path currently has SHA-256
`cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`;
the historical log did not record its hash, so the current hash is not proof
of the historical file's identity. The CLI reports 4.0.3, but `otool -L`
shows no dependency on `libmetalirconverter.dylib`; it does not establish the
loaded runtime library's version.

The recorded n84 runtime log reports GPU family 1007, macOS target 27.0.1,
compatibility mask 4, validation mask 4, debug information retained, and both
special resource spaces disabled. The bridge applies these through optional
MSC API symbols; the sample log reports those configuration symbols available.
Mask 4 corresponds to texture minimum LOD compatibility and DXIL validation,
respectively. Root signature, stage-in and other stage-specific options also
participate in conversion and must be accounted for in any equivalence claim.

A local CLI run of the same `vertexid-O3.dxil`, using
`--deployment-os=macOS --minimum-gpu-family=Apple7
--minimum-os-build-version=27.0.1 --textureMinLODClamp --validateDXIL`,
produced an `air64-apple-macosx27.0.1` metallib with two eligible loads and
SHA-256 `825e46f49ee0df7fce1a5bc591b89f0a91fcca7aa54b618ce6cf4d347ff63957`.
The previously recorded runtime-cache sample has SHA-256
`d5a810b785c652ec18eee87f4421a4d3f69b6cb173b26d2260ca57a630f6d3fb`;
the bytes differ. Matching these selected options does not establish matching
library implementations, root signatures, other options, or GPU behavior.

### Persistent cache identity risk

`MakeMSCConversionCacheKey` includes the reported `ir_version_*` and optional
symbol mask (`src/d3d12/d3d12_shader_converter.cpp`) but the reported version
is populated from the compile-time header, not from the loaded dylib. Two
runtime dylibs with the same optional-symbol mask may therefore share a cache
key even when their generated output differs. This is a source-level risk, not
an observed stale-cache reproduction. Before enabling any production AIR
rewrite, define and validate a stable loaded-library identity for persistent
cache invalidation; if identity cannot be established, avoid persistent reuse
across potentially different libraries.
