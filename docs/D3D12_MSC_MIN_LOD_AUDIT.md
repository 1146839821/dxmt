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

## Experimental AIR rewrite

An isolated prototype extracted the converter's AIR, used LLVM 15 to insert a
descriptor metadata load and clamp the explicit level for each of the five
known 2D sample calls, and rebuilt a metallib with the existing DXMT writer.
The input fixture uses explicit level 0; its fifth SRV has a view-relative
negative clamp. In a native Metal harness with the matching descriptor table,
ordinary texture and API/shader validation enabled, the unchanged metallib
returned `1, 1, 1, 1, 3` and failed the oracle; the rewritten metallib returned
`1, 2, 3, 4, 3` and passed. Both runs used the same harness and inputs.

This demonstrates a possible repair mechanism for one fixed AIR shape, not a
production implementation. The prototype does not handle general LOD values,
empty mip intersections, status results, texture dimensions, descriptor
indexing or alternate AIR versions. It also has not been integrated or tested
through DXMT, shader caching, reserved textures, or descriptor lifetime. The
`n22-lod-air-prototype-20261030` artifacts contain the prototype and both logs.

## Empty view intersection

The `--sample-empty` mode sets the fourth SRV's resource clamp above the last
mip visible in its view. The expected R32 result is zero. On the same M1 Max
runtime, both SampleLevel and SampleGrad return nonzero results for ordinary
and reserved textures. SampleGrad still passes the nonempty matrix in a
separate control. The experimental AIR rewrite likewise returns the last mip
instead of zero when its minimum exceeds the last visible mip. The fix must
therefore handle the empty intersection as well as the explicit LOD calculation;
simply taking the maximum of the LOD and clamp is insufficient. Missing
component defaults and view swizzles require separate coverage. Results are in
`dxmt-test/n23-lod-empty-20261030`.

## Experimental empty-intersection result

On the same M1 Max, a native Metal test performed an out-of-bounds texture
read with API and shader validation enabled. R32 and RGBA32 views returned
their respective missing-channel defaults, and two swizzled views produced the
expected mapped values. Apple's [Metal Shading Language specification](https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf)
describes out-of-bounds texture-read component defaults in the texture
functions section.

An isolated extension of the fixed 2D AIR prototype checks the view's mip count
and selects an out-of-bounds read for an empty intersection. It passed both
the nonempty and empty R32 native harness cases with Metal validation enabled.
This is a candidate mechanism, not a production patch: it performs the
out-of-bounds read even for nonempty views and does not establish correctness
for other texture types, comparison sampling, status values, null descriptors,
all swizzles or shader stages. Metal and D3D component mapping and descriptor
lifetime also require end-to-end verification. Artifacts and logs are in
`dxmt-test/n24-lod-oob-20261030`.

## Sampler maximum LOD ordering

The D3D specification's section 5.8.6.2 applies the sampler's LOD range
before the resource minimum, even when the resource minimum exceeds the
sampler maximum. A native Metal experiment with sampler MaxLOD 0.25 showed
that the prototype's `max(explicitLOD, resourceMinLOD)` is subsequently
clamped by the Metal sampler: resource minima 0.5, 1 and 1.5 all returned the
0.25 mip blend. The expected values are the resource-clamped mip blends.

The `--sampler-max` test mode reproduces this failure through DXMT for both
SampleLevel and SampleGrad, with an unrestricted-sampler SampleGrad control
passing. Thus sampler ordering is a blocker for both the draft AIR rewrite and
the existing gradient path. Correct handling needs access to the sampler range
and an execution strategy that respects D3D's ordering; simply modifying the
explicit LOD is insufficient. Do not use fields documented as reserved in
MSC's descriptor ABI without a compatibility design. The native and DXMT
results are in `dxmt-test/n27-lod-sampler-20261030`.

## Experimental sampler-range sidecar

A native proof used a separate two-float buffer for the original sampler
minimum and maximum, plus a Metal sampler with a relaxed LOD range. Modified
AIR first clamps the explicit LOD to the stored sampler range and then applies
the SRV minimum. With one static sampler, dynamic texture indexing, explicit
LOD 1.25, sampler maximum 0.25, several resource clamps and an empty view, the
five exact outputs matched the D3D ordering. Changing the stored sampler
maximum to 0.75 made the unchanged oracle fail. Metal API and shader
validation were enabled. This prototype used an extra root-buffer slot for the
sidecar; it did not change the MSC descriptor entry format.

The experiment does not establish a production ABI. Dynamic sampler arrays,
multiple samplers, descriptor copies and post-Close mutations require a stable
mapping from the selected sampler to its range and relaxed sampler. Static and
dynamic variants, original sampler behavior for other shader operations,
resource retention, submission synchronization, root-buffer size and shader
cache versioning must be handled together. The fixed root slot used by the
prototype is not safe to reserve in DXMT without a binding-layout design.
Gradient, comparison and implicit-LOD sampling remain unimplemented. Artifacts
and positive/negative logs are in `dxmt-test/n28-lod-abi-20261030`.

A follow-up prototype uses the dynamically selected sampler-table index to
select a corresponding range from a separate table. Two samplers with different
mip filters and maximum LODs are selected nonuniformly in one dispatch. The
five expected outputs, including an empty view, matched; swapping the two range
entries made the oracle fail. The MSC sampler descriptors remained unchanged.
This only tests a single sampler table and a hand-added root-buffer slot; it
does not solve descriptor-table rebasing, multiple tables, updates after Close,
or management of original and relaxed sampler variants. See
`dxmt-test/n29-lod-dynamic-sampler-20261030` for artifacts.

The experimental AIR pass additionally uses a sidecar entry containing a
relaxed sampler handle and the original range, leaving the MSC sampler table
available to unmodified operations. In a native mixed SampleLevel/SampleGrad
test, replacing the original sampler and its sidecar range after encoding but
before command-buffer submission produced the expected six values. Omitting
the sidecar update made the explicit-level oracle fail while the original
gradient sampler observed the updated state. This establishes the need to
update both representations together; it does not establish DXMT queue,
descriptor-copy or in-flight lifetime correctness. Artifacts and logs are in
`dxmt-test/n32-lod-volatile-sidecar-20261030`.

The local reproduction artifacts, converted AIR and binary identities are in
`dxmt-test/n19-metal-minlod-20261030`, `n20-lod-sample-20261030`, and
`n21-lod-compiler-20261030` alongside this checkout.
