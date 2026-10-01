# Task Analysis

Clean baseline `4f184c4`, branch `feat/d3d12-1`. Existing 311 invocation modes
and 48 host tests retained. Integration branch `origin/feat/d3d12`.

Creation-time candidate probing converts each ray export then loads its Metal
library/function. Load failure skips that candidate; no selected stage yields
E_NOTIMPL. Factory output must stay null, with no partially initialized object
published. Lazy dispatcher loads are covered separately and return E_FAIL.

Hypothesis / evidence / expected effect: missing executable evidence, not a
confirmed bug. Reuse RunStateObject and test-only library/function import wraps;
six stages plus AH/CH hint paths, real controls and each load failure (24 modes).
Arm only after device/root setup. Exact compiler/load trace, sentinel output
clearing, same-description factory retry and converter-cache reuse required.
Retry creates a fresh object; do not claim same-object initialization retry.
Cache reuse skips selected compiler calls but must still load real libraries
and functions. No successful object fabricated and no AIR fallback.

Risk: observation must not perturb old or lazy modes; wrapping must preserve
the actual ABI. No production changes unless a deterministic failing contract
is reproduced. Multi-export partial progress and addition-parent behavior remain
separate. No new capability/FL promotion, GPU dispatch/game/FPS or native claim.

Validation: independent normal/no-private reconfigure/build, host/Meson tests,
full Wine invocation gates with runtime/executable provenance and API/shader
validation before device creation; restore normal. Independent Standards/Spec
review against baseline, Task Result, diff check, local commit without push.

# Task Result

## Scope and implementation

24 creation-load modes added to the mandatory state-object oracle (68 total),
retaining all previous 311 invocation modes. Test-only wrapping of the real WMT
imports uses export function ordinal observations separately from lazy function
name observations; no production ABI or source changes. Observation starts after
device/root initialization. Each mode runs in a fresh process, single export.

Load failures continue the existing candidate loop, ultimately E_NOTIMPL with
sentinel output cleared and no object published. Fault-disabled same-description
factory retry creates a new object; selected-stage conversion is cached, but
library and function loading still occurs. Compiler-fault-armed subsequent factory
creation also succeeds through that cache and real loads. Public alias/hint group
identifiers checked. No same-object or retained-Metal-object cache claim.

## Evidence and review

Both independently reconfigured full builds PASS. Host tests 49/49 PASS and
Meson 3/3 PASS in each build. Targeted normal controls/failures PASS in
`build/state-load-targeted.log`: RayGen control/library failure, hinted AH function
failure. Representative AH: query/materialize, library.1, export.function.1,
E_NOTIMPL/null publication; retry S_OK with only library.1/export.function.1 added;
later compiler-fault-armed creation likewise loads real objects without compiling.
No production contract failure reproduced; diagnosis skill's production-fix
phases are inapplicable to this coverage task. MSC compilation/integration skills
guided actual stage compilation and real loading, Metal-validation skill guided
environment configuration before device creation.

Normal full receipt `build/fl12-state-load-normal.json`: 335/335 invocation cases
PASS, including all 24 new creation-load modes. Build/runtime provenance PASS,
executable provenance checked; all 335 cases API/GPU-validation enabled, no
selected assertion/shader/GPU-validation errors. Both FL12 gates remain FAIL.
No-private full receipt `build-no-private/fl12-state-load-no-private.json`:
335/335 PASS, provenance PASS, API/GPU validation enabled for all cases, no
selected validation errors. Fresh case sets in both variants preserve every
previous 311 mode and add exactly the specified 24. Full runners exit 1 with
existing typed-UAV/missing-requirement FAIL gates, not invocation failures.
Normal DLLs restored by `sync_dlls.sh`, fresh normal provenance PASS. Final
`git diff --check` PASS; actual local commit and clean tree reported in handoff.

Independent read-only review against `4f184c4` and this untracked spec: Standards
zero hard findings, one optional test-matrix deduplication heuristic (deferred;
keep test expectations separate from runner). Spec zero implementation/scope
findings. Full dual runtime/provenance receipts and normal restoration recorded
above; do not substitute reviews for runtime evidence.

## Limits and next item

All HRESULTs are DXMT-local contract evidence, not native D3D12 equivalence.
No allocation-growth, object destruction-count, GPU dispatch/hit-group semantics,
native Windows, concurrency, game or FPS acceptance. Creation retry is fresh
object allocation, not failed object reuse. Addition-parent export loads and
multi-export partial progress remain unverified; overall isolation PARTIAL.
Capabilities/FL11_1 unchanged, FL12 gates remain FAIL due to existing typed-UAV
and other missing requirements. Next: bounded addition export-load/publication
and parent-immutability retry coverage. Local commit only, NOT PUSHED.
