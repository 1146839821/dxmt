# Task Analysis

Baseline: feat/d3d12-1 at 2a30307, clean worktree.

Hypothesis: runtime sampler metadata can select ordinary sampling versus AIR
reduction without changing root signatures or recompiling each descriptor value.
Evidence: the shared 32-byte descriptor already transports flags and LOD clamps;
static and volatile sampler ownership now follows the required observation time.
Expected effect: real dynamic reduction on eligible DXBC consumers.
Risk: admitting unsupported operations, MSC consumers, resource MinLOD clamps,
or stale sampler generations; ordinary sampling must remain unchanged.

Implementation must connect all of:

- Root binding descriptor decode and runtime reduction discriminator.
- SampleLevel control flow with separate ordinary/reduction operations and PHI
  result merge, not eager execution of both operations.
- Per-shader consumer eligibility propagated to actual host recording and live
  submission validation, including mixed AIR/MSC use of the same slot.
- Opt-in non-anisotropic dynamic sampler creation with point native sampler and
  full original AIR filter/LOD metadata; MSC must reject its consumption.
- Resource MinLOD and feedback guards independent of residency opt-out.
- Direct and indirect observation/cache identity; no family fallback or FL/SM
  promotion, and no weakening of unsupported operation checks.

Focused validation first: same PSO ordinary/minimum/maximum descriptor changes,
static versus volatile timing, mixed filter/LOD state, unsupported consumer
rejection, AIR/MSC ordinary regressions, both build variants. Full matrices later.

# Task Result

Initial inspection (historical): dynamic reduction was rejected. Source inspection confirmed the
dynamic root binding currently discards LOD clamps, while static bindings decode
them; SampleLevel currently selects reduction only at compile time through an
optional state. Simply changing AddSampler would therefore be incorrect.

Compiler work in progress: filter/LOD decode is shared, and SampleLevel can now
emit descriptor-predicate control flow with ordinary/reduction blocks and a PHI
result. Ordinary sampling keeps its original unclamped-by-emulation LOD operand;
only the reduction branch applies the emulated clamps. Dynamic root bindings now
supply the predicate for samplers whose decoded uses are all supported float
2D/array/3D SampleLevel without feedback. Any other sampler operation excludes
eligibility; shaders containing compared texture handles are conservatively
excluded. A reflection flag transports this consumer qualification.

Both winemetal builds pass compilation. Fresh matching isolated runtimes pass
the ordinary dynamic DXBC volatile-observation GPU probe (255) in both variants,
with shader cache disabled. This validates the ordinary path after integration,
not the reduction branch. Host consumer qualification, resource clamp guards,
descriptor admission and dynamic reduction readbacks remain open. Keep this task
uncommitted until integrated; no FL/SM promotion or complete matrix claim.

Host work in progress: AIR shader reflection qualification is read once during
PSO creation and intersected across graphics stages. Pending sampler slots now
carry both MSC participation and AIR reduction qualification; repeated consumers
OR the MSC restriction and AND reduction eligibility. Admission remains closed
until resource MinLOD guard integration and reduction GPU readbacks complete.
The current qualification is deliberately shader-wide, not a full per-range
SM5.1 resource/register-space acceptance proof.

Both D3D12 builds and updated fixture compile successfully. Four fresh ordinary
volatile-observation executions (AIR/MSC x both variants) pass with GPU output
255 and explicit assertions for PSO qualification and pending-slot constraint
transport. This does not validate mixed-consumer intersection or unsupported
shader rejection yet, and reduction admission is still closed. No commit yet.

## Current implementation delta

The earlier admission-closed statements above describe intermediate checkpoints.
An independent DXMT_ENABLE_AIR_MINMAX_DYNAMIC=1 opt-in now admits finite-LOD,
non-anisotropic reduction descriptors, emits point native sampler state with
original AIR filter/LOD metadata, and leaves MSC descriptor entries empty.
Unsupported AIR and MSC consumers are rejected; root-updating indirect and
direct-indexed reduction remain excluded pending consumer/binding closure.

Static resource MinLOD state is captured at recording time, never reread for
this guard. The queue passes per-execution reduction observation to the volatile
resource resolver without mutating closed encoders. Static reduction samplers
also enable the existing recording/live resource clamp rejection. Eligible AIR
resource observation remains active even with residency opt-out.

Both DLL builds succeeded. Fresh dynamic minimum/maximum GPU readbacks pass
16/240 in both isolated variants with matching winemetal.so loader paths and
shader cache disabled. No same-PSO ordinary/minimum/maximum switch, mixed consumer,
unsupported consumer or resource-clamp guard acceptance claim yet. This task
remains uncommitted until focused lifecycle/rejection regressions and self-review.

## Final focused result

The opt-in dynamic explicit-LOD integration step is implemented. Final matching
builds pass 24 focused executions (12 per variant): same-PSO live descriptor
switch 16/240/128/16; static and volatile reduction ownership with GPU output 16;
AIR SampleGrad and MSC consumer rejection; volatile SRV ResourceMinLODClamp
rejection; existing static minimum/maximum/mixed clamp 16/240/16; ordinary
AIR/MSC observation 255; default-disabled dynamic rejection and descriptor-copy
regression. Rejection cases exercise the real resolver but do not submit their
unsupported shader work. Meson suites pass 3/3 per build; diff check passes.

### Standards self-review

No blocking issue identified. Static/live filter flag packing shares one helper;
host PSO reflection qualification is computed once, not per draw. Snapshot
retention/fan-out remains outside heap locks and closed encoders do not collect
live generations. Existing naming and Rc/COM ownership patterns are retained.
Independent reviewers were unavailable due to usage limits; this is main-agent
review, not independent review evidence.

### Spec self-review

No shader-family fallback or FL/SM promotion. Static ranges do not enter pending
lists or reread descriptors at submission; volatile samplers observe live state.
Unsupported consumers and resource clamps fail closed. AIR compute creation
compiles current bitcode and creates a fresh native library, not an MSC conversion
cache artifact, so the ordinary/reduction runtime branch does not reuse old MSC
lowering. Tests keep shader cache disabled; cache-on regression is outstanding.

Qualification is shader-wide, conservatively intersected across stages. Other
sample operations, depth/comparison resources, feedback, DXIL reduction,
root-updating indirect/direct-indexed reduction, full per-range SM5.1 acceptance,
static-resource clamp switching, mixed-consumer stress, complete matrix coverage
and game/performance acceptance remain open. Normal/no-private 2D float probes
do not establish 2D-array/3D D3D12 runtime acceptance. The full Min/Max workstream
and original FL12_0/FL12_1 goal remain incomplete.
