# Task Analysis

## Recording/replay continuation

Baseline f432b483, clean. Hypothesis: ordinary typed DRAW/DRAW_INDEXED can use
the existing MSC reflected root-update resolver now that split-TLAB payloads
exist. Evidence: ExecuteIndirect explicitly disabled typed PreDraw selection,
and replay only redirected MinMax resolver buffers. Expected effect: retain
typed draw markers, select reflected private layouts, defer template creation,
redirect resolver buffers to submission-owned clones and restore private PSOs.
Risk: allocator mutation, stale variant selection, lost split-stage bindings,
accidentally admitting companion/VB/IB paths not implemented. Validation:
normal/no-private builds, existing native regressions, targeted runtime follow-up.
Complete graphics indirect qualification remains the objective, not this slice.

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

## Recording/replay result

Ordinary MSC typed DRAW/DRAW_INDEXED now selects its private variant through
PreDraw. Non-updating commands inherit submission-bound stage TLABs. Root-updating
commands resolve application parameter offsets against the private reflected
layout and attach immutable render resolver payloads to the typed marker.
Submission replay redirects the resolver's vertex-buffer binding to the cloned
payload and retains its allocation through completion. The allocator restores
the typed private PSO after the resolver instead of the application PSO.

Companion GS/HS/DS indirect and typed VB/IB updates are still unsupported, as are
the previously rejected predication/MinMax combinations. Companion selection
is reported before status return so unsupported paths fail recording explicitly
instead of silently dropping the command. Full typed graphics indirect remains
incomplete. Both full builds and native regressions (12/12 each) passed; these
host tests do not prove public GPU draw correctness. Source self-review checked
marker association, resolver clone redirection, private PSO restoration, immutable
payload ownership and old caller default behavior. git diff --check passed.
Evidence logs retained as typed-wire-*.log in the reconciliation cache. Next
required work is a focused typed split-stage direct/indexed indirect readback
fixture, before expanding the full graphics matrix or declaring qualification.
