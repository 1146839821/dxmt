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
