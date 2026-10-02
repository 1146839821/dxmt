# DXIL MinMax finite descriptor arrays

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
