# DXIL MinMax modern binding handles

## Task Analysis

- Hypothesis: constant finite SM6.6 binding handles can resolve against existing
  resource metadata; generated private handles must use modern binding and
  annotation operations, not legacy createHandle or shader-model downgrading.
- Evidence: selected DXC emits ResBind {lower,upper,space,class} plus constant
  index and ResourceProperties. The current path rejects all modern handles.
- Expected effect: admit modern finite constant sampling pairs through the same
  runtime descriptors and retain unrelated original modern resource handles.
- Risk: mismatched binding range/annotation, sampler consumers hidden behind
  annotations, incorrect private CBV size or resource properties.
- Validation: native IR validity, actual SM6.6 fixture regeneration with DXC,
  MSC conversion and focused GPU readback; both full builds and legacy regression.
  Heap/dynamic/unbounded handle flows remain separate required implementation.

The shared preparation inspector also restricts compute containers to SM6.0.
Extend only the MinMax operation's maximum minor version to 6; retain typed-
origin's maximum minor version 0 and validate both input and rebuilt output.

## Implementation and focused acceptance

Modern constant binding handles now match exact finite application ranges,
register spaces/classes and metadata; annotations must agree with float texture
shape/component properties or ordinary sampler properties. Raw sampler handles
must flow through qualified annotations and only admitted sampling consumers.
Private texture/sampler/CBV handles use createHandleFromBinding (217) and
annotateHandle (216), preserving the original shader model and unrelated handles.
The private CBV annotation size matches the complete per-pair state array.
Operation identifiers were checked against the
[DXC DXIL reference](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst).

Only MinMax's container inspector admits compute SM6.0..6.6; typed-origin remains
SM6.0. Both validated input and rebuilt output retain the existing allowed-part
checks. No general shader-model/feature-level promotion or fallback was added.

Both full builds and host suites (4/4 each) passed. Native modern-array output
is identical between builds. Native checks reject out-of-range index, nonuniform
index and inconsistent sampler properties without publishing bindings. Legacy
array transformation remains byte-identical to the previously validated IR.

Actual SM6.6 array SampleLevel and volume SampleGrad artifacts passed selected
DXC regeneration/validation and MSC Apple9 conversion with reflection. Each
isolated actual-DLL runtime read array MIN 16, array MAX 240, volume gradient 160;
each also read legacy-array MIN 16. Fresh PE/Unix load paths were confirmed.
The original SM6.6 containers, not downgraded or pre-lowered replacements, were
passed to the runtime probes. No installed game/prefix DLLs were replaced.

Main-agent self-review checked signature types, exact range matching, sampler
consumer coverage, pre-mutation qualification, private singleton metadata and
annotation sizes. LLVM/MSC skills informed IR verification and reflection/
binding checks. Heap, runtime dynamic/unbounded/nonuniform handle selection,
full MinMax coverage, FL12_0 and game/tessellation acceptance remain incomplete.
No independent review claim. Evidence:
`/Users/zhangbo/.cache/dxmt-minmax-modern.uK19aI`.
