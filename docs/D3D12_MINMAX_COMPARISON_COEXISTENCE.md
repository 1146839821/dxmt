# Task Analysis

## Current Branch

`feat/d3d12-1`.

## Baseline

Clean b123416; read-only `origin/feat/d3d12` e147c710, merge-base85bb2dd.

## Local Commits Since origin/feat/d3d12

126 local commits at task start. Full FL12 objective remains active.

## Current State

`dxil_minmax_binding.cpp` rejects every unqualified `dx.op.sample*` operation and
every non-regular consumer of a sampler handle. Consequently a separate
SamplerComparisonState prevents an otherwise qualified reduction shader from
being regenerated.

## Existing Implementation

Independent regular sampling lowering, paired bindings and original application
root descriptors already exist; native MSC implements comparison sampling.

## Relevant Files

`src/winemetal/unix/dxil_minmax_binding.cpp`, `tests/dx12/dxil_minmax_ir.cpp`,
`tests/dx12/dx12_minmax_prepare.cpp`, shader fixtures and Meson targets.

## Existing Tests

Native IR, PE preparation and graphics fixtures verify regular samples,
but do not cover comparison coexistence.

## D3D12 Contract

STANDARD/COMPARISON/MINIMUM/MAXIMUM must coexist without backend fallback.
Hypothesis: independent, statically resolved comparison-sampler calls can remain
unchanged while regular sampler pairs are rewritten. DXC's
[DXIL specification](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst)
defines SampleCmp opcode64 and SampleCmpLevelZero65 with distinct signatures.
Expected effect: qualify those f32 calls and comparison-kind metadata before
mutation; keep their original handles, arguments, results and native MSC path.

## DXBC / AIRCONV Impact

No changes, executable/ABI reuse or fallback.

## DXIL / MSC Impact

Only independent DXIL preflight changes. No AIR executable/ABI reuse, descriptor
ABI changes. Native MSC still compiles the untouched compare
operation; regenerated DXIL validation remains mandatory. No capability change.

## Shared Runtime Impact

No host sampler, descriptor or residency change.

## Missing Pieces

Independent comparison consumers currently block reduction shader regeneration.
Real GPU coexistence qualification is absent.

## Risks

Wrong sampler kind or modern comparison annotation, malformed/variadic signature,
status extraction, and accidentally rewriting comparison operands or users.

## Minimal Implementation Plan

Prove comparison metadata kind1 and finite static register provenance, exact
opcode/signature/operand types and component-only users. Reject comparison calls
on regular sampler metadata and regular calls on comparison metadata. Maintain
unknown sampler consumer and feedback rejection. Add focused mixed-call fixture
and unchanged-call/reject-before-mutation tests, then regenerated-container/MSC
evidence. GPU coexistence and broader compare operations remain required before
full runtime qualification; do not conflate offline conversion with GPU output.

## Validation Plan

Both configurations: reconfigure, focused native/PE targets, full default builds,
host suites, LLVM verifier, selected DXC regeneration. Source review and local
commit, no push.

## Capability Impact

No FL11_1 change, no FL12_0/12_1 or SM promotion.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

Task b123416; read-only remote e147c710. Full objective remains active.

## Local Commit

This repository-style local implementation commit; no history rewrite or push.

## Changed Files

`src/winemetal/unix/dxil_minmax_binding.cpp`, native IR fixture,
`minmax_comparison.hlsl`, optional Meson CS/PS targets, this record and closure ledger.

## Implementation

Only exact f32 SampleCmp/SampleCmpLevelZero calls on statically resolved comparison
kind1 sampler metadata bypass pair collection. Full nonvariadic FunctionType,
opcode, float result/status shape, component-only users and original provenance
are checked before mutation. Original comparison callee, arguments, result type
and users remain unchanged. Regular sampler metadata still cannot drive comparison
calls, comparison metadata cannot drive regular calls, status/unknown consumers
and unsupported provenance remain rejected.

Modern annotateHandle must match sampler kind0 to property14 and kind1 to
property32782, with zero second property word. The comparison flag is bit15;
it is not a new resource kind. This matches the official
[DXC resource-properties representation](https://github.com/microsoft/DirectXShaderCompiler/blob/main/include/dxc/DXIL/DxilResourceProperties.h)
and the selected DXC's actual SM6.6 output. Other annotation bits are not relaxed.

## DXBC / AIRCONV Impact

None. No shader fallback, helper linkage or executable-family mixing.

## DXIL / MSC Impact

Qualified independent comparison calls survive regular reduction regeneration;
MSC compiles the original comparison operation. This is compiler admission and
conversion evidence, not comparison GPU qualification.

## Shared Runtime Impact

None: descriptor, sampler, residency, root observation and private pair ABI unchanged.

## Tests Added

Two mixed comparison/reduction HLSL fixtures (CS LevelZero, PS implicit compare).
Native `binding-comparison` mode checks unchanged comparison calls and one regular
pair, plus seven reject-before-mutation probes: wrong sampler metadata, regular
handle substituted into comparison, wrong opcode, status extraction, comparison
handle substituted into regular sampling, modern wrong comparison bit or legacy
nonuniform handle, and variadic function signature. Rejections compare complete
module text and sentinel binding records. Meson exposes optional CS/PS targets.

## Tests Run

Both builds reconfigured; focused native/PE and final full default builds complete.
Host suites5/5 each. Native no-argument regressions pass with LLVM15 verification.
Four selected-DXC containers (CS/PS at SM6.0 and SM6.6) run through both native
builds: eight positive transformations and56 rejection probes, all pass and all
output IR verifies. No SM6.6 capability promotion follows from these fixtures.

Eight final production PE preparation processes validate regenerated DXIL and
exercise the actual export-boundary safety tests, each reporting one regular pair.
Eight offline MSC4.0.1 Apple9 conversions produce metallibs and reflection.
Automatic CLI layout is not proof of production augmented-root GPU bindings.

## Runtime Results

Evidence root `/Users/zhangbo/.cache/dxmt-minmax-comparison.vC1AN7`.
Count only `final-*` files as final evidence: native IR,
`final-{normal,no-private}-{cs,ps,modern-cs,modern-ps}-prepare.log` and corresponding
MSC logs/metallibs/reflection; `final-full-*.log`, `final-tests-*.log`.
Baseline isolated native runtime169fd26f rejects the same CS container with
E_NOTIMPL (`baseline-rejected.log`), while final preparation succeeds.
Initial failed test compilation and initial modern-annotation rejection are
diagnostic evidence, excluded from final counts.

Final normal D3D12 SHA1 `2fa3e83a0acb3ca5da991d7e29f70a962327ec0d`, no-private
`d79f3555e5bba78d8866e89f7f9685972effa33f`; current build, adjacent staged DLL
and overlay PE copies match. Native winemetal build/overlay normal
`aaf00b9871f4e59899d440fb0b5af7b833dc9403`, no-private
`3cb8f7736b63e89bd823139228554a863da9b2f4` match. Only cache overlays updated.

Two final real Cube-array Sample graphics GPU regression processes return0,
each completing12 readback groups across static/live sampler/root and draw modes.
These24 groups verify the previous regular path, **not comparison GPU output**.
No installed Wine/prefix DLL deployment, game launch, Steam or wineserver management.

Reproduce native comparison qualification:

```sh
build/tests/dx12/dxil_minmax_ir \
  /Users/zhangbo/.cache/dxmt-minmax-comparison.vC1AN7/comparison-modern-cs.dxil \
  binding-comparison > /tmp/dxmt-comparison.ll
/usr/local/opt/llvm@15/bin/opt -passes=verify -disable-output /tmp/dxmt-comparison.ll
```

## Known Limitations

No comparison GPU readback or production-root mixed comparison draw/dispatch
qualification. Broader compare shapes/formats/filters/clamps, compare feedback,
f16, gather, modern dynamic/nonuniform handles and advanced compare operations
remain unqualified/rejected as applicable. No native-Windows oracle, Metal
validation/trace, game benchmark or tessellation acceptance in this increment.

## Standards

Independent review found an analysis-heading format breach, now fixed; an optional
test duplication heuristic was withdrawn because adjacent test style is intentionally
retained without unrelated refactoring. No remaining Standards finding.

## Spec

Review requested exact nonvariadic signatures and evidence beyond LLVM verification.
Added signature rejection probe, strict modern comparison-bit qualification and
final PE/DXIL/MSC evidence above. Follow-up source review found no remaining source
defect. Reviewers did not execute tests; main owns reported validation. LLVM skill
guided unchanged IR and mutation checks; MSC skill guided regeneration/conversion.

## Capability Status

PARTIAL: independent comparison coexistence compiler path implemented/validated;
actual GPU semantics remain UNVERIFIED. Full MinMax and FL gates remain incomplete.

## Feature Level Impact

FL11_1 unchanged; FL12_0 and FL12_1 unpromoted.

## Git Status

Scoped changes reviewed and committed locally after diff whitespace validation.

## Push Status

NOT PUSHED.

## Next Recommended Task

Real production-root comparison plus reduction GPU dispatch/draw with a depth
resource and independent comparison/reduction oracles, before a broader matrix.
