# MSC root-updating indirect compute

## Task Analysis

Continue the missing MSC indirect ABI, including submission-materialized typed
origins. Root constant updates are the first admission step; GPU-selected root
VAs need a separate resource lifetime/address contract and stay rejected.

- Hypothesis: the shared GPU resolver can use a reflected parameter-offset map
  and copy one MSC TLAB template per indirect command, rather than treating MSC
  root constants as AIR qwords. Typed origins require this template to come from
  submission materialization, not from a recording-time descriptor snapshot.
- Evidence: the current resolver binds only AIR roots/static samplers, while
  MSC requires the argument-buffer/heap bindings. Typed replay already creates
  immutable per-submission buffers but has no indirect resolver-data override.
- Expected effect: root-constant-updating MSC compute becomes executable, using
  per-command TLABs with unchanged tables/static samplers/private origin records.
- Risk: template/payload alignment, hidden root offsets, per-submission buffer
  ownership, read/write residency, and accidentally admitting unresolved root VAs.
- Validation: both full builds and host suites, real ordinary MSC and typed
  indirect compute readbacks, existing AIR reset regression, and self-review of
  unchanged unsupported root-VA/graphics/MinMax guards. Complete matrices remain
  later; this does not promote capabilities or close the full indirect contract.

## Task Result

The shared GPU resolver now supports MSC root constant updates using reflected
parameter offsets, copying the TLAB template to independent 16-byte-aligned
per-command regions. It binds named MSC TLAB/resource/sampler heap slots and
uses reflected threadgroup sizes. A buffer barrier separates resolver writes
from indirect dispatch reads. AIR qword/static sampler binding remains separate.

Ordinary MSC uses a recording-time root template with the existing descriptor
residency path. Typed origins instead allocate the destination TLAB array and
resolver payload within each submission's materialized binding buffer. Replay
replaces only the cloned resolver buffer command: allocator-owned payloads and
nodes are never patched. The copied template retains the private origin record
address, per-submission tables and inherited roots/static samplers. Its allocation
is retained with read/write usage through completion. Live descriptor resolution
still excludes static slots and releases the heap lock before allocation/fan-out.

Evidence directory: `/Users/zhangbo/.cache/dxmt-msc-indirect.THHLQE`.

- Old `aa91bb4` DLL rejected ordinary MSC root-updating dispatch at recording.
- Normal/no-private reconfiguration and full builds succeeded, followed by
  successful final full builds after optional probe compilation.
- Both host suites passed 5/5.
- Final focused GPU results: 8 ordinary MSC readbacks, 8 AIR regressions,
  14 typed indirect full-buffer readbacks and 2 typed direct regressions = 32.
  Ordinary/AIR cover an odd-offset constant, untouched adjacent constants/root
  UAV, zero CPU maximum, zero GPU count and two independently updated commands.
  Typed covers both selected slots, volatile/static ranges, two commands, and a
  volatile SRV changed after Close. That late-change case reads 116 rather than
  the recording-time view's 28, proving submission-template provenance.
- Two valid MSC root-CBV-updating signatures were rejected at Close, one per
  build. They were not submitted. Root-VA, graphics binding updates and MinMax
  indirect remain guarded.
- Only task application/runtime clones were changed. No game/prefix DLL writes,
  game/performance/tessellation acceptance or feature-level promotion.

Standards self-review: owned submission buffers, stable marker/node identities,
explicit C++/MSL payload size, named binding constants, checked allocation and
reflected root subrange bounds. Spec self-review: fixed TLAB stride alignment
and template-size separation, verified independent commands and unchanged root
VA guard, and checked copied private CBV/table pointers remain in the retained
submission allocation. No remaining actionable finding in this admitted scope.
These are main-agent two-axis reviews, not independent review evidence; review
subagents are unavailable. MSC integration guidance determined the inline
constant, reflected offset and residency design.

Still open: GPU-selected root VA lifetime/address resolution, broader graphics
and indirect binding contracts, repeat/concurrent-list submission and broad
format/shape/heap/static-sampler qualification. Focused passing results do not
close those requirements or the original FL12_0 objective.
