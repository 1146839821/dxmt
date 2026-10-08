# DXIL MinMax SampleGrad implementation

## Task Analysis

- Hypothesis: the existing SampleLevel footprint can serve SampleGrad once a
  view-relative isotropic LOD is emitted and the original sampler bias is
  transported separately from the unbiased private native samplers.
- Evidence: AIR's CreateIsotropicGradientLOD already implements normalized
  Gram-matrix major-axis selection and the parallel/zero-gradient caveat.
  DXIL binding currently qualifies SampleLevel only; its ordinary and point
  samplers deliberately have zero bias. The 32-byte state has a reserved word.
- Expected effect: reuse the AIR LOD algorithm and existing conditional
  reduction taps rather than introduce another sampling footprint.
- Risk: SampleLevel must continue ignoring sampler bias. Instruction clamps
  must be applied after sampler clamps, and both ordinary and reduction
  branches must share the original view dimensions. A compiler-only helper
  does not establish production dispatch admission.
- Validation: first compile and verify the generated DXIL IR; then connect
  bias/state and both branches, regenerate fully validated DXIL, compile with
  MSC and run distinguishable multi-mip GPU readbacks in both builds.

Opcode and SampleGrad signature reference:
https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst

This task is incomplete until binding integration and numeric runtime
acceptance pass. No capability or FL promotion is authorized by preparation.

## Gradient emitter checkpoint

Implemented CreateReductionGradientLOD2D, including declaration/signature
qualification before mutation, original-view dimension queries, normalized
major-axis calculation and the AIR degenerate-gradient rules. The native IR
probe verifies malformed-opcode rejection without mutation, valid-module
generation, expected dimension/unary operations and absence of fast-math flags.
Both reconfigured builds compile and run that probe successfully; both host
suites pass 4/4. Main-agent self-review checked the algorithm against AIR and
the DXIL operand positions. These are structural compiler checks, not numeric
GPU acceptance or regenerated-container validation.

Production still rejects SampleGrad. Next: connect runtime bias and clamp order
to both binding branches, then validate actual multi-mip dispatch results.

## Binding integration Task Analysis

- Hypothesis: normalize qualified SampleGrad calls into the existing
  SampleLevel branches, keeping gradient LOD in the common dominator.
- Evidence: both branches already apply sampler clamps followed by resource
  constraints and empty-set defaults. SampleGrad's operand 16 supplies its
  view-relative instruction clamp; the state record's final word is unused.
- Expected effect: one footprint implementation, runtime bias for gradients
  only, and identical merged constraints in ordinary/reduction branches.
- Risk: PHI dominance, malformed intrinsic signatures, status consumers and
  a stale private shader cache. Keep the state size fixed and bump its version.
- Validation: extend actual-container binding probes, reject unsupported
  consumers, compile both variants, then validate regenerated DXIL and GPU
  multi-mip oracles. No full-matrix rerun or capability promotion.

Validation diagnosis: zero-clamp regeneration validates and passes ten native
GPU states. Nonzero-clamp regeneration rejects a stale TiledResources shader
flag (declared 4112, actual 16). DXC collects this flag from nonzero native LOD
clamps or CheckAccessFullyMapped; after normalization the clamp is ordinary
arithmetic. Update the private entry metadata only when no mapping-status
check remains, preserving every other flag and copy-on-write ownership.

D3D12 acceptance analysis: reuse the existing four-mip texture sampler probe
for DXIL major-axis/bias/MinLOD/MaxLOD cases. Its DXIL output descriptor is
structured, so compile the shared gradient fixture with a structured output
variant; do not bind a raw shader to that structured view. Reject DXIL modes
whose gradient vectors the supplied fixture does not represent. Expected
outputs are nonzero/distinct, so skipped dispatches cannot pass as cleared
readback. Use fresh isolated DLL/runtime clones only.

## Task Result

Qualified legacy float Texture2D SampleGrad now uses the production preparation
and MinMax binding transformation. The original-view gradient LOD dominates
both branches; only gradients add the transported sampler bias. Both branches
apply sampler limits then merged resource/instruction constraints. Point taps,
empty-set defaults and application pair identities reuse the existing path.
The private state remains 32 bytes, version 2; its final word carries bias.
SampleLevel continues ignoring that word. Comparison/aniso, broader shapes,
modern/nonuniform handles and sampled status consumers remain rejected.

Two fully regenerated raw-output gradient containers (clamp 0 and 3) pass DXC
validation and MSC 4.0.1 compilation. Each native build passes twenty gradient
state dispatches, including ordinary/reduction bias, point/linear mip extrema,
sampler limits, resource-over-MaxLOD and empty component defaults: forty
executions across builds. The two-pair SampleLevel regression passes fifteen
states in each native build with a deliberately nonzero bias of 10, confirming
it is ignored. These thirty executions are separate from gradient acceptance.
Both gradient IR outputs match byte-for-byte across builds; nine pair/provenance,
metadata, status and failure-publication probes run for each gradient fixture.

Actual D3D12 compute submission passes five focused dynamic-sampler cases in
each isolated build: major-axis LOD=96, bias=224, MinLOD>MaxLOD=96, MaxLOD=32,
and instruction clamp 3 above sampler MaxLOD .25=160. AIR reduction gates are
off. Nonzero expected readbacks exclude skipped-dispatch false positives.
This is ten production GPU executions, not a full MinMax matrix. Fresh static
SampleGrad, dynamic instruction operands, wider formats/shapes and indirect
dispatch acceptance remain to be covered; no claim of full MinMax or FL12_0.

Both complete builds and both 4/4 host suites pass. Sampler state tests pass
72 ordinary contracts, 24 private configurations and seven reject/preservation
checks; pair materialization and the actual PE-to-Unix preparation/export probes
pass in both isolated runtimes. Physical winemetal paths are recorded by dyld.
Main-agent self-review checked shared dominance, bias isolation, metadata
copy-on-write, rejection publication and fixture/view compatibility; diff
whitespace checks pass. No independent reviewer or game benchmark is claimed.

Receipts: `/Users/zhangbo/.cache/dxmt-minmax-grad.eH4A3Z`, with final production
results in `normal/dispatch-*.final.log` and `no-private/dispatch-*.final.log`.
Final source/staged D3D12 SHA-256 matches:

- normal: `324fd2c92a10a0a01a72a6aa82d23b8023b9f2822d4b4b77f4811fad84217fd0`
- no-private: `4983abf8cd51e2b173a6cdbc5e1c1d0fa2a9b3e82956639fe8046f425d171ec8`

Installed prefix/game DLLs were not replaced; all runtime changes are in new
task-owned staging copies. Original loader-failure logs are retained, not
counted as passes. Capability declarations are unchanged.
