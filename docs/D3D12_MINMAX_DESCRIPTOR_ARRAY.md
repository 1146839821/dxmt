# DXIL MinMax finite descriptor arrays

## Runtime binding Task Analysis

- Hypothesis: existing root location lookup and table materialization resolve
  actual array register identities without a separate array replay path.
- Evidence: compiler now reports t1/s1, but the existing GPU probe binds only
  one descriptor in each range and cannot exercise these locations.
- Expected effect: exercise the production path with two-element ranges and
  distinct first/second texture and sampler slots, correcting implementation
  if actual GPU evidence contradicts the existing location mapping.
- Risk: identical slots would hide erroneous base-slot selection; keep slot 0
  texture forced zero and sampler reduction opposite to the intended slot 1.
- Validation: MIN/MAX readbacks in both builds using isolated current DLLs,
  selected original DXIL, and lower-dimensional regression. No full matrix.

## Task Analysis

- Hypothesis: constant legacy handle indices into finite descriptor arrays can
  resolve to exact application register identities without dynamic selection.
- Evidence: the resolver requires count == 1 and register == base; it discards
  otherwise statically known indices. Private metadata currently copies count.
- Expected effect: admit finite arrays with constant in-range indices, preserve
  application ranges, and emit one private descriptor per resolved pair.
- Risk: register arithmetic overflow, conflating slots sharing one range, or
  accidentally copying array count into overlapping private descriptor ranges.
- Validation: native positive/negative qualification, both full builds, selected
  DXC validation and MSC conversion. Focused runtime array binding follows;
  dynamic index, unbounded arrays and complete FL12_0 remain unproven.

## Compiler implementation result

Legacy constant handle indices now resolve within finite application ranges;
pair identities use the actual texture/sampler registers. Application metadata
is retained while every cloned private descriptor range has count one. Dynamic
and nonuniform handles, out-of-range indices and unbounded arrays still reject.

The selected DXC emitted index 1 for both two-element arrays in the real HLSL
fixture. Both native tools emitted identical transformed IR; the regenerated
container passed DXC validation and MSC Apple9 conversion with reflection.
Both full builds and host suites (4/4 each) passed. Native qualification checks
cover resolved texture slot 1, original count 2/private count 1, and out-of-range
slot 2 rejection alongside existing provenance/status guards.

Main-agent self-review checked widened range arithmetic, pair deduplication by
slot rather than range identity, private singleton metadata and rejection before
publication. This compiler checkpoint does not claim runtime descriptor-array
GPU acceptance; that integration readback remains the next required check.
No game/prefix deployment, feature promotion, or independent-review claim.
Evidence: `/Users/zhangbo/.cache/dxmt-minmax-descriptor-array.71AUZY`.

## Runtime integration result

The existing production root-location/table-materialization path correctly
resolves the second finite-array slot. No separate production replay path or
capability change was needed. The focused probe exposes two-element SRV and
sampler ranges, puts the output UAV after both SRVs, and binds the original
selected-DXC shader through the runtime MinMax conversion path.

Slot 0 uses forced-zero texture components and the opposite reduction sampler;
slot 1 uses the four distinguishable texels and intended reduction sampler.
Both normal and no-private actual-DLL runs read MIN 16 and MAX 240. These oracles
distinguish wrong texture base-slot selection (0) and wrong sampler base-slot
selection (the opposite extremum). Each build also passed Texture3D gradient
(160) and Texture2DArray gradient (96) regressions. Fresh task-local PE/Unix
load paths were confirmed in Wine/dyld logs. Installed game/prefix DLLs remain
unchanged. Both reconfigured full builds and host suites (4/4 each) passed.

Main-agent self-review checked range sizes, separate heap increment sizes, UAV
CPU/GPU offset consistency, decoy differentiation and existing-mode preservation.
The MSC integration skill informed table-offset and native-object lifetime
checks. This completes focused finite constant-index runtime acceptance, not
dynamic/unbounded/nonuniform indexing, the complete MinMax matrix or FL12_0.
Evidence: `/Users/zhangbo/.cache/dxmt-minmax-array-runtime.jC1zXv`.
