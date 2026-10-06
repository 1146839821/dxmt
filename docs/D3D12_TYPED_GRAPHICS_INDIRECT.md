# Task Analysis

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
mutually exclusive. Split native VS/PS indirect remains rejected pending its
dual-TLAB resolver ABI; ordinary split direct draws remain unchanged.

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
