# Task Analysis

## Current Branch

`feat/d3d12-1`, clean; HEAD `8475045`.

## Baseline

Adapt the missing macro gate from pinned reconciliation commit `fee20420`;
do not import unrelated command-list tracing or residency counters.

## Local Commits Since origin/feat/d3d12

Pinned `bad6756a` comparison: 140 ahead / 17 behind before this task commit.

## Current State

DEBUG/TRACE macros format and evaluate arguments even when Logger rejects them.

## Existing Implementation

Immutable per-DLL minimum log level; output filtering remains in emitMsg.

## Relevant Files

`src/util/log/log.hpp`, native util regression/registration, this record.

## Existing Tests

Audit cache PE probe reproduced disabled argument evaluation; no registered
all-level positive/negative macro regression currently exists.

## D3D12 Contract

Logging must not provide business side effects; no GPU contract changes.

## DXBC / AIRCONV Impact

None to parsing, lowering, ABI or permanent routing.

## DXIL / MSC Impact

None to compilation, origin/reduction or capability handling.

## Shared Runtime Impact

Shared DEBUG/TRACE statement macros gate argument evaluation; WARN/ERR unchanged.

## Missing Pieces

Disabled argument and stream insertion checks, enabled levels, dangling-else
safety and production call-site side-effect inspection.

## Hypothesis / Evidence / Expected Effect

Gate before format to eliminate disabled formatting; checks must distinguish
argument evaluation, formatting, and logger invocation. No FPS gain inferred.

## Risks

Suppressing required side effects; enabled output regression; macro statement
semantics; test-double scope cannot certify real output I/O.

## Minimal Implementation Plan

Register macro regression and run red; gate only DEBUG/TRACE; full normal and
no-private build/test, self-review and local commit without push.

## Validation Plan

Reconfigure both modes, test all six minimum levels; retain original PE probe.
Inspect every production macro argument including getter definitions. No game
process changes or prefix binaries. Full FL goal remains open.

## Capability Impact

None. User additionally requested upstream per-commit adaptation after this
task and historical temporary-file cleanup; source/end-point clarification is
resolved: 3Shain/dxmt main, pinned HEAD
`e40c9d7ae1cf476ee9cc54b9c2cab6ed09b2e595`; 14 commits strictly after
`fb4515681daefb789a4d0f403c4bdbca88f3b3de`. No cleanup or upstream merge is
performed here. Historical temporary artifacts will first be listed, then
recoverably archived while retaining current validation evidence.

# Task Result

## Branch

`feat/d3d12-1`.

## Baseline

`8475045`; adapt only `fee20420` DEBUG/TRACE macro portion.

## Local Commit

Reported after committing. Message matches `fee20420` macro repair title;
this does not claim full patch equivalence to its command-list tracing changes.

## Changed Files

`src/util/log/log.hpp`, `src/util/meson.build`,
`src/util/tests/debug_logging.cpp`, this task record.

## Implementation

DEBUG and TRACE check immutable Logger minimum level before evaluating arguments
or formatting. Single-statement do/while macros preserve unbraced if/else.
WARN, ERR, ERR_ONCE, actual Logger output filtering and level initialization
remain unchanged. Enabled debug/trace messages use the same format invocation.

## DXBC / AIRCONV Impact

None; no shader parsing/lowering/ABI/cache version changes.

## DXIL / MSC Impact

None; no compilation, binding, capabilities or fallback changes.

## Shared Runtime Impact

Shared macro gate applies to D3D11/D3D12/DXGI/NVNGX consumers. Production macro
arguments and NGX_DEBUG wrapper were inspected; no necessary business side
effect was found in suppressed arguments. Texture dimensions/array length,
current allocations, addresses and descriptors are reads, not creation/reset.

## Tests Added

Native actual-header macro regression uses isolated renamed Logger with all
six immutable thresholds. It separately counts argument evaluation, stream
insertion, logger invocation, enabled message content and dangling-else paths.
It does not link the production output sink or emulate its filtering internally.

## Tests Run

Reconfigured normal/no-private before builds. Before repair, all levels other
than Trace failed: e.g. Info evaluated and formatted both arguments and called
both logger methods. Trace positive control passed. After repair, full builds
completed 0 and host suites each passed 12/12, including all six levels.
Original audit PE probe rebuilt against current header and ran through cache
Wine, exiting 0. No game/prefix DLL modifications or process management.

## Runtime Results

Evidence `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE`:
`log-config-*`, `log-red-build.log`, `log-full-*`, `log-host-*`,
`log-original-green.log`; process statuses captured separately by runner.
Red test output was captured by Meson's test runner before repair. Native unit
and PE macro probe are not GPU validation or actual log sink I/O certification.

## Standards Review

Independent readonly review: 0 hard violations or actionable smells. Symmetric
macros are clear; no speculative abstraction. Main self-review checked scope,
threshold direction, immutable level reads and single-statement expansion.

## Spec Review

Independent readonly review: 0 implementation defects. Parameters, formatting
and calls gated; enabled content/levels and statement behavior covered. Reviewer
also inspected DEBUG/TRACE/NGX_DEBUG argument getters without finding required
side effects. Main performed builds/tests; reviewers did not.

## Known Limitations

Direct `Logger::debug(str::format(...))` calls are still eager, as are counters,
walks and computations outside macros. The two command-list trace gates in
`fee20420` were not imported; production residency counters remain a separate
profiling/adaptation requirement. No FPS, CPU timing or complete logging-overhead
elimination claim; no performance benchmark or fresh game/GPU acceptance.

## Capability Status

Scoped macro regression validated locally in both modes; full goal remains
PARTIAL. No broad capability advertisement changes.

## Feature Level Impact

FL11_1 unchanged; FL12_0/FL12_1 not promoted.

## Git Status

Only four task files intended for commit; final commit/status reported afterward.

## Push Status

NOT PUSHED. Exact upstream SHA fetch used FETCH_HEAD only; branch refs unchanged.

## Next Recommended Task

Honor the new user priority: adapt all 14 commits in
`fb451568..e40c9d7a` from 3Shain/dxmt main, without merge, preserving each
upstream original message. Inspect equivalent local changes and dependencies,
retain D3D12 extensions, self-review/validate each adaptation. Also inventory
historical test temporaries and recoverably archive selected obsolete artifacts;
retain current binaries/runtime overlays and acceptance evidence. Neither
upstream integration nor cleanup is claimed completed by this logging task.
