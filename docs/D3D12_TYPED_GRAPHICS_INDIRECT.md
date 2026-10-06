# Task Analysis

## Dual-TLAB ABI continuation

Baseline 3f035eef, clean. CPU render payload and generated Metal resolver both
receive two appended addresses, with static size/offset assertions. Resolver
copies vertex and optional fragment templates separately and applies application
root updates to both, preserving the distinct private record CBVs. Submission
materialization allocates both per-command arrays with overflow checks and
clones fresh addresses without touching allocator-owned data. Single-TLAB and
compute paths preserve their original behavior. Full builds/native regression
plus source review are required; GPU source compilation/execution is separate.
No ExecuteIndirect admission change at this ABI checkpoint.

Current branch feat/d3d12-1; baseline feb27542; clean worktree. Typed graphics
indirect remains rejected by PreDraw. Compute typed indirect and graphics
MinMax already clone resolver payloads into submission-owned buffers.

Hypothesis: typed graphics can reuse that ownership pattern, but native
combined VS/PS requires two stage-local TLABs. Evidence: the typed materializer
currently accepts only IndirectComputeCommandData, while the render resolver
uses one TLAB for both stages. Expected effect: implement the missing render
payload ownership foundation without enabling a path with incorrect stage data.
Risk: partial payload publication or allocator mutation across replays.
Validation: both builds, native regressions, inspect size/overflow checks and
payload cloning. This checkpoint is not GPU indirect acceptance.

The MSC integration skill requires per-draw TLAB storage, reflection-derived
offsets and retention through GPU completion. Preserve static descriptor
recording snapshots and unique volatile submission resolution. No capability
promotion, compiler fallback, game changes or full matrix reruns.

# Task Result

Typed-origin materialization accepts a paired allocator-owned immutable render
payload and resolver binding, validates the reflected size/stride/count,
allocates submission-owned resolver/TLAB storage and clones the payload.
The allocator payload is never patched. Compute and render payloads are
mutually exclusive. At the initial checkpoint split native VS/PS materialization
was rejected pending the dual-TLAB resolver ABI; this is superseded below.
Ordinary split direct draws remain unchanged.

PreDraw rejection and ExecuteIndirect routing remain unchanged in this
foundation checkpoint. Remaining implementation: dual-stage resolver ABI,
typed render replay binding, private PSO restoration and command-list wiring,
then focused direct/indexed late-descriptor and replay GPU oracles. The whole
typed graphics indirect requirement remains incomplete.

## Validation and self-review

Reconfigured normal/no-private full builds passed. Existing native tests passed
12/12 in each; these do not invoke the new render materialization path and are
not its runtime acceptance. Source review checked paired payload/binding,
graphics/compute exclusion, reflected template bounds and count multiplication,
submission-owned copy and read/write residency. Original allocator data and
static/volatile observation logic are unchanged. git diff --check passed.
Logs retained under /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE as
typed-render-payload-*.log. No FL/capability promotion or game acceptance.

## Dual-TLAB ABI result

CPU/Metal render payloads append fragment TLAB/template at offsets 152/160;
CPU size is asserted as 168. Both are initialized to zero by the allocator.
The resolver copies separate templates and writes constants/CBV/SRV/UAV root
updates into each present stage TLAB. Fragment ICB binding selects its private
TLAB when present and otherwise preserves the original shared binding.
Compute has no added payload fields and keeps its single TLAB.

Typed materialization now accepts split-stage render payloads, allocates two
per-command arrays with overflow checks and writes fresh submission addresses
into a cloned payload. Only application roots are updated by the resolver;
the private CBV address in each original template remains distinct. For single
TLAB variants, both optional fragment addresses are explicitly zeroed.

Normal/no-private full builds passed after reconfiguration. Existing native
regressions passed 12/12 in each. Self-review checked CPU/MSL field ordering,
zero defaults, template preservation, mirrored root updates and unchanged
compute/legacy behavior; git diff --check passed. The generated Metal resolver
has not been compiled/executed by these host tests. Logs: typed-dual-*.log under
the existing reconciliation evidence directory. Replay wiring, public indirect
admission and GPU validation still remain; this is not complete typed indirect.
