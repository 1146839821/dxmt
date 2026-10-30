# D3D12 MSC texture minimum LOD: observed gaps

This is a focused diagnostic record, not a feature claim. The tested baseline
was `0ac349686eea9cdac00ca1c379b06e0f5242e280` on Apple M1 Max, macOS
27.0.1, with the locally installed Metal shader converter runtime reporting API
4.0.1. Other GPUs, OS versions, native Windows, shader stages, texture types,
and sampler configurations remain unverified.

## Required behavior

The [D3D11.3 functional specification, section 5.8](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
describes resource-space minimum LOD clamping for SRV reads and sampling,
including fractional and empty-view cases. D3D12 moves this clamp to the SRV
view descriptor. A load from a mip excluded by the clamp returns an
out-of-bounds value; a sample honors the minimum clamp.

## Runtime evidence

`tests/dx12/dx12_texture_lod_clamp.cpp` runs the explicit-level fixture
`texture_lod_clamp.hlsl` and the gradient fixture
`texture_lod_clamp_grad.hlsl` against the same clamp/readback matrix. With
matched WineMetal PE/Unix binaries and Metal API/shader validation enabled,
SampleLevel returned mip 0 for nonzero clamp values where the matrix expected
filtered results; SampleGrad produced the expected results for both ordinary
and reserved 2D textures. The gradient values were chosen to select mip 0
before clamping for these fixture dimensions. They are not a general-purpose
replacement for explicit LOD.

`--load-late` demonstrates a separate load gap: record with a zero-clamp SRV,
close, replace that volatile descriptor with a clamp of 1, then execute. The
observed mip-0 value is 1.0; the expected out-of-bounds value is 0. This mode
currently returns failure deliberately. The existing `--load-unsupported`
mode verifies the recording-time rejection for an already nonzero clamp.

## Converter evidence

An independent x86_64 process linked the same `/usr/local/lib/libmetalirconverter.dylib`
(SHA-256 `cc487a757f431c83483346c06a3ae8558910ecc510ce59b1e4f905d5d6d72bd4`),
set Apple7 and macOS 27.0.1 targets, and compiled the fixtures with an explicit
version 1.1 root signature containing the same SRV/UAV descriptor table and
static sampler shape. For the SampleLevel fixture, `metal-objdump` showed the
same AIR instructions with `IRCompatibilityFlagTextureMinLODClamp` on and
off, excluding the disassembly's filename line: five explicit-level samples
and no loads of descriptor clamp metadata. For SampleGrad with the flag on,
AIR loads each descriptor's metadata and passes the converted float to the
gradient sample operation. This evidence isolates the observed fixtures to
converter code generation; it does not prove behavior for all variants or
converter versions.

A direct Metal experiment on this GPU rejected a texture-view descriptor with
nonzero `minLOD` under API validation because the GPU lacks
`TextureViewMinLOD`. The macOS 27 view API therefore cannot serve as a general
fallback for this device. A compatible implementation needs a verified
cross-version strategy, plus correct descriptor mutation and lifetime handling.
Do not remove the current load rejection or promote feature claims solely on
these observations.

The local reproduction artifacts, converted AIR and binary identities are in
`dxmt-test/n19-metal-minlod-20261030`, `n20-lod-sample-20261030`, and
`n21-lod-compiler-20261030` alongside this checkout.
