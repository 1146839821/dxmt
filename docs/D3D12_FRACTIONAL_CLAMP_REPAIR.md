# Fractional clamp repair

Baseline aad41922. Hypothesis: the current numerical failure is caused by
automatic private-path selection, state transport or lowering, distinguishable
by a PSO selection assertion before the existing exact GPU oracle. Evidence:
fresh clamp-fix-baseline.json reproduces ordinary 1/1/1/1/3 instead of 1/2/3/4/3
and wrong reserved outputs, with compiler/PE/Unix provenance. Expected effect:
repair the existing semantics, not relabel rejection as support. Risk: changing
sampling selection or lowering can regress ordinary filters, static/volatile
observations and sparse data. Validation: red-before-green numeric oracle,
selected-path assertion, both builds, same-executable A/B and ordinary/reduction
regressions. No capability promotion, prefix/game deployment or push.

## Root cause and semantic repair

The deployed PSO had already selected a qualified MinMax variant. The test now
optionally asserts that immutable selection with --require-private-clamp; the
red baseline passes the selection assertion but fails exact GPU values. Thus
compiler absence is not the root cause of this deployed-path failure.

The ordinary branch retained the application SampleLevel call and its original
handles. It tested resource clamp only for empty views; it did not apply clamp
to the explicit LOD, relying on native MSC metadata that this runtime ignores.
SampleLevel now uses existing zero-clamp private texture and unbiased/unclamped
ordinary sampler, applying bias once, sampler max then min, then resource clamp.
Existing filters, coordinates, offsets, descriptor capture/retention, private
register layout and empty-set defaults remain intact. SampleGrad and implicit
operations retain their directional native calls; they are not scalarized.

The remaining reserved result also exposed an oracle defect, not a renderer
failure: with mips 0 and 1, resource clamp 1.5 must return OOB, even though its
floor is mip 1. [D3D functional specification §5.8.5](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
explicitly defines this last-mip exception. Correct vectors are ordinary
1/2/3/4/3 and reserved 1/2/3/0/3. The old renderer still fails the corrected
fixture, so the oracle correction does not manufacture repair evidence.
Sampler bias/clamp ordering is checked against §7.18.6 and SampleLevel §22.4.18.

The Tiled runner accepts --tiled-compiler-dir (standalone: --compiler-dir) only
for the clamp case, with existing observed compiler/PE/Unix path/hash checks.
That case requires actual private-path selection. Other cases and rejection
controls do not receive deployment. Without deployment the native metadata path
remains unsupported; no distribution/installer change is included here.

## Evidence boundary and self-review

Source and build graph are reconfigured before both full builds. Matching cache
PE DLLs and Unix images are refreshed only after completed builds. Initial
fixture include-dependency failure was corrected using the repository's existing
internal-fixture dependency pattern. Both full builds and explicit fixtures pass.
The existing libunwind linker warning remains; it is not treated as validation.
Native dxil_minmax_ir succeeds on both builds, including LLVM module verification.

Under /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE, the frozen
clamp-fix-frozen-ab-{old,new}.json uses the same final EXE SHA256
611450b0088d9c37f0f6bb6fe2997e519669c70172260870f0ff9bc7311ced9a,
identical PE hashes, compiler and shader. Only native implementation differs:
old exit1 / new exit0, both observed provenance PASS. Earlier final-ab-* results
had unequal PE version hashes and are excluded from exact native-only A/B claims.
The original native image/PEs are retained in clamp-pre-fix.Ok3DUA; the matched
current cache native image is restored after comparison. The no-private exact
numeric/private-path fixture also passes.

Standards/Spec review is main-agent only, informed by code-review. Diagnosis
distinguished selection, actual lowering and oracle errors. MSC integration
guidance preserved reflected layout and owned binding transport; LLVM guidance
preserved verified IR and derivative control flow. No independent review,
complete fractional clamp across all operations/formats/stages, native Windows,
full Tier2/FL12 gate, game benchmark or capability promotion is claimed.
## Final regressions and outstanding finding

clamp-fix-regression-tiled-{normal,no-private}.json each records 20 PASS actual
executions with deployed clamp, while category remains BLOCKED_BY_ARCHITECTURE
for the explicit independent gaps. clamp-fix-regression-minmax-* each passes
52 numerical cases (category PARTIAL). clamp-fix-tiled-api-no-private.json passes
all 20 with API validation; the clamp process confirms validation activation.
Both 16-test host suites pass, including 82 gate tests, and diff checks pass.
These are registered bounded categories, not a full FL12 or all-state acceptance.

The broader MinMax deployed-dispatch fixture exposes a PRE-EXISTING red
in-flight regression: a held first execution expects MIN=16 but returns MAX=240
after the host changes the live sampler. clamp-inflight-frozen-ab.json repeats
the same EXE/PE/compiler with old/new/old/new native; all four fail, with varying
first failing modes (2--4). This excludes the current ordinary-SampleLevel change
as the source of that already-reproducible failure, not every possible regression.
Earlier no-private invocation missed its shader fixture and is excluded; the
shader target is now built for a fresh repetition.
The fresh clamp-inflight-no-private-repeat.json also fails at mode 2 with
actual=240/expected=16 and observed PE/Unix provenance. Full in-flight/lifetime
acceptance is therefore NOT claimed. Next prioritize auditing the timestamp
worker's asynchronous observation boundary against the volatile descriptor
contract and the fixture's private-submission expectations before selecting a
fix; a conformance defect cannot be inferred solely from this stronger test.

The overall FL12 objective remains open; native-unprepared and gradient/implicit
clamp semantics, broader qualification and remaining architectural gaps are not
waived by this explicit-LOD repair.
