# ExecuteIndirect state reset

## Task Analysis

Close the shared command-list state contract before admitting root-updating
MSC dispatch. This prerequisite does not implement the MSC indirect binding ABI.

- Hypothesis: AIR indirect execution leaves pre-call CPU root/VB/IB staging
  intact, so subsequent direct commands inherit values which should be reset.
- Evidence: the compute and ordinary graphics ICB branches return after encoding
  argument copies without resetting application staging. The Microsoft indirect
  drawing specification requires affected constants to become zero and affected
  root views/VB/IB to become null after ExecuteIndirect.
- Expected effect: subsequent commands observe reset state; unmodified bindings
  remain inherited. The already-copied indirect arguments remain unchanged.
- Risk: clearing before copying would corrupt the indirect call; clearing an
  entire constant parameter would erase unmodified constants. Zero/count-buffer
  and predicated execution must still reset CPU state.
- Validation: normal/no-private builds and host suites, focused AIR compute GPU
  readback for modified constants, preserved constants/UAV, and zero command
  count. Review ordering and graphics VB/IB reset. MSC rejection stays intact.

Specification: https://microsoft.github.io/DirectX-Specs/d3d/IndirectDrawing.html

## Task Result

Command signatures own the update descriptions (excluding their terminal
operation). ExecuteIndirect validates root staging ranges, copies indirect
arguments before resetting exact constant subranges/root views and the affected
VB/IB bindings, and dirties subsequent root uploads. A CPU zero maximum count
resets state without creating a zero-length ICB or allocating zero root copies.
Dispatch signatures containing graphics VB/IB updates are rejected at creation.
MSC resource-updating signatures remain rejected; this is not MSC indirect
support or FL12_0 closure.

Evidence: `/Users/zhangbo/.cache/dxmt-indirect-reset.PjlDwr`.

- Existing HEAD DLL plus the new probe reproduced `106,62` instead of `106,7`:
  the indirect copy worked but the subsequent direct dispatch inherited the
  pre-call value 55 plus the preserved value 7.
- Both reconfigured full builds and final post-probe full builds succeeded.
- Host suites passed 5/5 in each build (ten suite executions, not ten new suites).
- Both builds passed three real AIR/DXBC compute GPU readbacks: normal count
  (`106,7`), zero CPU maximum (`62,7`), and zero GPU count (`62,7`). The shader
  uses an odd-offset updated constant alongside untouched constants in the same
  parameter and an inherited root UAV. Output is initialized by a preceding
  dispatch; no verdict relies on allocation contents.
- Task-owned application/runtime clones only; no game or prefix DLL changes.

Standards self-review: checked owned descriptions, one shared reset helper,
bounded staging writes, unchanged rejection guards and copy/reset ordering.
Spec self-review: reset is independent of executed command count; only modified
constant DWORDs are cleared and unmodified bindings remain inherited. Fixed the
zero-maximum root allocation failure during review. No remaining finding in this
scope. Main-agent reviews, not independent reviews (review subagents unavailable).
Graphics VB/IB and root-view null resets were source-reviewed, not separately
GPU-qualified by this compute probe. Invalid root parameter type/compatibility
validation and the MSC resolver/TLAB ABI remain follow-up work. No capability
promotion, complete matrix, game/tessellation or performance acceptance claimed.
