# Single-sample private variant SampleMask acceptance

Baseline: 3382ef01, feat/d3d12-1. This step adds bounded GPU evidence for
existing production mask propagation and absent-PS coverage; it does not
change production capability admission, native ABI or the raster gate total.

## Hypothesis / evidence / expected effect / risk / validation

Hypothesis: private Typed/MinMax PSO rebuilding preserves the application mask
in native, GS and HS/DS paths. Source retention alone does not prove the rebuilt
pipeline is used. Expected effect: mask 0 leaves the clear value untouched;
mask 1 produces the independent origin/reduction oracle. Risk: testing an
ordinary pipeline, accidentally masking the restoration control, or treating
a compiler-preparation failure as a GPU result. Require nonempty private
artifacts, independent numeric readback, paired masks, stage-specific shaders,
both builds and observed runtime/compiler identities with API validation.

## Fixture changes

The existing depth and MinMax graphics fixtures accept a final
`--sample-mask=N`; omission preserves UINT_MAX. Parsing rejects empty, negative,
overflowing and trailing nonnumeric values. Both fixtures resolve a relative
compiler argument to an absolute Windows path before using the private API.
The initial Typed diagnostic returned E_INVALIDARG because that API deliberately
rejects a relative compiler directory; the production contract is unchanged.

Typed depth requires a nonempty private variant for non-default positive cases.
Mask 0 expects first-pixel clear depth 1. Mask 1 checks the existing independent
FirstElement/live-replacement stage contributions. The ordinary second-pixel
restoration PSO explicitly uses UINT_MAX and must still write depth 0.75.
Direct/indexed, RS1.0/RS1.1 and deny-root controls are retained.

MinMax checks every color component against clear 64 when sample 0 is masked
out, and the existing reduction/live-replacement/ordinary-restoration oracle
when it is covered. Static and dynamic sampler configurations, private binding
capture and direct/indirect submissions remain active. Its obsolete static
compiler-absence assertion is corrected: with deployed DXC, automatic selection
must produce a nonempty private variant; absent deployment still requires
E_NOTIMPL. The old assertion also failed with default UINT_MAX, before the
static mode's GPU execution. No failure is waived and no static mode is skipped.

Optional Meson shader targets make the selected stage inputs reproducible.
The normal and no-private probes use identical freshly built DXIL inputs;
optional targets remain outside the default build.

## Evidence

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- private-mask-typed-first.json preserves the relative-directory E_INVALIDARG.
- private-mask-typed-path-green.json records the corrected native mask-0 run.
- private-mask-minmax-combined-control.json preserves the default-all stale
  static-deployment assertion failure; it is not a renderer failure or PASS.
- private-mask-matrix-final.jsonl: all 24 cases PASS, both builds, Typed depth
  and MinMax color, native/GS/HS-DS, masks 0 and 1, with API validation and
  target PE/Unix/compiler path/hash provenance. Typed accounts for 448
  submission readbacks, each checking masked depth and ordinary restoration.
  MinMax records 112 successful direct/indirect readback groups.
- private-mask-minmax-combined-final.json: additional no-private mask-1 VS+PS
  MinMax numeric control PASS after the deployment assertion repair.
- private-mask-full-{normal,no-private}-final.log: both complete builds PASS.
- private-mask-host-{normal,no-private}-final.log: both host suites PASS 17/17,
  including the existing 92 gate unit tests. git diff --check passes.
- private_mask_matrix.py retains the exact sequential diagnostic commands.

Main-agent Standards/Spec self-review checks CLI compatibility, private-path
assertions, independent oracle values, unmasked Typed restoration, automatic
deployment and failure handling. Diagnosis/compiler/integration/validation
skills informed the fixture and provenance checks. No independent review,
shader validation, native Windows oracle, WOW64 GPU run or game deployment is
claimed. Runtime deployment is cache-only; no games or Wine/Steam processes
were restarted and nothing is pushed.

## Remaining work

This is single-sample evidence, not 4x private-variant acceptance. Combined
VS+PS MinMax mask-0/both-build breadth, Typed+MinMax together, stencil,
sample-frequency/side effects, formats/counts/topologies/lifetimes and full
raster/FL qualification remain open. The mandatory raster ledger remains 183
bounded cases and PARTIAL. SM6.6/FL12_0/LogicOp MSAA opt-ins remain off.

Next user priority is R11G11B10 Typed UAV support. Current source already maps
the format and exposes typed store, but does not advertise typed load. Do not
equate TextureBufferRead plus TextureBufferWrite with read_write support or
promote the entire additional-format contract from a single-format result.
Begin with independent packed-format load/store readback across buffer/texture
routes and the shared AIR/MSC implementation boundaries.
