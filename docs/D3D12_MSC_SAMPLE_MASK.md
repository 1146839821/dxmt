# MSC fragment SampleMask propagation

Baseline: 90b61019. Scope: carry D3D12 graphics PSO SampleMask into MSC fragment
compilation and its conversion identity. Full MSAA qualification remains open.

Hypothesis: SampleMask never reaches MSC, although AIRCONV already receives it.
Evidence: the production D3D12 raw-sample fixture, mask 5 and XOR, changes samples
1 and 3 from 12340101/12340303 to 123401f1/123403f3. The red run has matched
PE/Unix/compiler provenance. Expected effect: masked-out samples retain seed
values. Risk: stale conversion-cache entries, private variant configuration
loss, mixed bridge versions, or confusing attachment coverage with shader
invocation/side-effect correctness. Validation: raw four-sample comparisons,
same-bytecode contrast masks, ordinary draw controls and old-native rejection.

## Implementation and ABI boundary

The device capability snapshot supplies a default-all compiler_sample_mask.
The graphics PSO retains its mask and supplies it only to fragment compilation,
including CapabilitiesForStage used by Typed/MinMax private variants. Both
in-memory and persistent conversion keys include the mask. Vertex/compute and
other stages continue using their ordinary compiler configuration.

The existing compile structs and Unix entry 146 are unchanged. New entry 205
receives a pointer to those existing compile params plus uint32 mask and result;
its fixed envelope has explicit 64/32-bit layouts and size checks. WOW64 uses
the shared legacy pointer/output conversion helper. Native optional-symbol bit
29 reports IRCompilerSetSampleMask together with the new entry's implementation.
Non-default masks require that bit; older native fails explicitly before the
new call. Default-all compilation continues through the legacy entry.

Native sets the mask immediately before fragment compile. The vendored MSC
header documents that IRCompilerSetSampleMask resets after every compilation;
the sizing/materialization calls therefore each apply their own mask. No compiler
object sharing or new descriptor/residency binding is introduced.

Absent-PS MSC depth-only pipelines cannot obtain coverage from this fragment
setting. Non-default masks now explicitly return E_NOTIMPL instead of being
ignored. A depth-only coverage-shader implementation remains a real gap; this
guard is not feature completion. Default-all depth-only behavior is unchanged.

Later checkpoint: D3D12_MSC_DEPTH_COVERAGE.md supersedes that blanket rejection
for ordinary MSC vertex pipelines via an internal coverage fragment. Masked
GS/HS/DS remains rejected; broader masked private/depth/stencil qualification
is not closed by the ordinary depth subset.

## Fixture

dx12_logic_op_msaa accepts --sample-mask=N, --ordinary, --contrast-mask and
--depth-only, preserving its original invocation. Seed always writes all four
samples. Only the tested second PSO gets the requested mask. For each raw sample,
the independent CPU oracle selects the Boolean result (ordinary source for the
ordinary mode) or the untouched seed according to the mask bit. No resolve or
averaging occurs. --contrast-mask first creates the same shader with a different
mask, catching a missing conversion-key dimension. The negative depth-only
mode is intended for PSO rejection, not a depth GPU oracle.

## Evidence and limitations

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- sample-mask-red.json: original XOR mask-5 numeric failure, matched modules.
- sample-mask-green.json: rebuilt XOR mask-5 and contrast-mask passes with
  Metal API validation; samples 1 and 3 remain unchanged, provenance matches.
- sample-mask-full-{normal,no-private}-final2.log: complete builds pass after
  the WOW64 error handling correction; initial failed logs remain diagnostic.
- sample-mask-fixture-{normal,no-private}-final.log: optional fixtures rebuilt.
- sample-mask-host-{normal,no-private}.log: both host suites pass 17/17,
  including 90 gate unit tests.
- sample-mask-matrix.json: 238 API-validation GPU cases and 952 raw sample
  comparisons pass on both builds (sixteen LogicOps and ordinary controls,
  masks 0/1/5/10/15/80000000/ffffffff); target PE/Unix provenance matches.
  This numeric matrix precedes the final contrast-PSO operation-order correction.
- sample-mask-final-controls.json: the corrected final fixture passes ten XOR
  cache-contrast cases with genuinely identical lowered bytecode on both builds.
  Two masked depth-only controls return E_NOTIMPL with matched modules. Earlier
  contrast PSOs used default CLEAR before setting the selected operation; their
  numeric results remain valid, but are not full same-bytecode cache evidence.
- sample-mask-old-native-controls.json: intentionally old no-private Unix image
  (hash matched to the saved pre-change file) rejects mask 5 while default-all
  ordinary GPU readback passes. Current-build Unix provenance correctly reports
  that deliberate image mismatch; these are compatibility controls, not formal
  qualification PASS. The fresh cache image was restored after the controls.
- sample-mask-pe32-syntax.log: i686 syntax compilation of the PE thunk and
  envelope size checks passes; actual WOW64 GPU execution is not claimed.
- sample-mask-raster-regression.json: all 162 no-private raster executions
  pass under API validation, with experimental MSAA explicitly zero and current
  PE/Unix provenance. Overall raster status remains PARTIAL.
- sample-mask-default-admission.json: final default no-private MSAA still
  returns E_NOTIMPL at logic PSO creation with current matched modules. Its
  nonzero exit is expected rejection, not a GPU qualification PASS.

Compiler/validation skills informed per-compilation configuration and API
validation; diagnosing-bugs supplied the production red-to-green oracle.
Main-agent Standards/Spec review checks unchanged old layouts/ordinals, stage
scope, private config, cache identity and default-off MSAA admission. There is
no independent review, shader validation or native Windows oracle. Runtime
deployment is cache-only; no game/prefix DLL changes or process restart.

Remaining: depth-only masked coverage implementation; numeric masked
Typed/MinMax private variants; explicit sample-frequency shaders, derivatives,
interpolation and UAV side effects; more counts/formats/MRT/write masks and full
format/raster qualification; actual WOW64 execution and persistent-cache disk
reuse. Cache-contrast GPU controls qualify in-process conversion identity;
the common persistent-key dimension is source-inspected. No FL/SM/LogicOp
advertisement is promoted.
