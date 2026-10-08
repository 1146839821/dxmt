# Task Analysis

## Current Branch

feat/d3d12-1.

## Baseline

Clean 2370e5a; origin/feat/d3d12 e147c710.

## Local Commits Since origin/feat/d3d12

131 at task start.

## Current State

Compute and native pixel typed-origin variants exist. Typed vertex draws remain
rejected. This is a prerequisite production contract, not vertex GPU acceptance.

## Existing Implementation

The transactional resolver matches ALL or requested stage visibility but only
accepts compute ALL and PIXEL. Only pixel root-access denial is checked.

## Relevant Files

d3d12_typed_origin_root.cpp and dx12_typed_origin_root.cpp.

## Existing Tests

Root reflection, DWORD budget, RS1.0 conversion and pixel visibility checks.

## D3D12 Contract

Vertex and pixel tables may reuse register identities with disjoint visibility.
Each stage resolves only its visible table; DENY applies to its own stage.
Descriptor range flags and failure output preservation must remain unchanged.

## DXBC / AIRCONV Impact

None. No backend selection or shader changes.

## DXIL / MSC Impact

Accept vertex visibility in private-root location resolution only.

## Shared Runtime Impact

No draw recording, submission, lifetime or residency changes.

## Missing Pieces

Vertex root contract first; vertex shader preparation/conversion, stage record
intervals and separate VS/PS TLABs still require implementation and GPU proof.

## Hypothesis

Existing stage-local matching supports vertex without changing private root ABI.

## Evidence

The resolver already filters ALL or requested visibility; its entry guard rejects
VERTEX and its deny guard checks only PIXEL. Graphics still rejects typed VS.

## Expected Effect

Correct stage-isolated locations available to subsequent graphics integration.
No previously rejected typed vertex draw becomes executable in this task.

## Risks

Cross-stage aliasing, ignored vertex deny flags, compute compatibility, changed
range flags and partial publication on ambiguity or missing binding.

## Minimal Implementation Plan

Accept VERTEX and check its deny flag. Extend the root fixture to both stages,
disjoint same-register tables, ambiguous ALL overlap and transactional failures.

## Validation Plan

Reconfigure both builds; focused and full builds before cache-only Wine staging.
Run root fixture on both variants, host tests, self-review and local commit only.

## Capability Impact

None. Typed VS remains unsupported; FL12 and additional typed-format capabilities
remain disabled. These root tests are not rendering or GPU semantic acceptance.

# Task Result

## Branch

feat/d3d12-1.

## Baseline

2370e5a.

## Local Commit

The commit containing this record; resolve with git log for this file.

## Changed Files

Resolver, root fixture and this record only.

## Implementation

VERTEX resolution accepts ALL or VERTEX tables and rejects vertex root-access
denial. Existing candidate-only publication and descriptor flags are unchanged.

## DXBC / AIRCONV Impact

None; no shader or routing changes.

## DXIL / MSC Impact

Stage-local vertex root resolution is available. Shader preparation/conversion
and graphics variant integration are unchanged and still reject typed VS.

## Shared Runtime Impact

No recording, submission, lifetime, descriptor observation or residency changes.

## Tests Added

27 visibility/deny combinations spanning compute, vertex and pixel; unsupported
GEOMETRY output preservation; disjoint same-register VS/PS offsets and flags;
missing second binding and ambiguous ALL overlap preserve prior output.

## Tests Run

Both reconfigurations, focused root/MinMax-root builds and full default builds
passed. Host tests 5/5 in each build. Both root fixtures and both MinMax-root
fixtures passed in task-cache Wine overlays. Pixel GPU fixture passed with
explicit DXC on normal and no-private, 16 typed draws each.

## Runtime Results

Evidence: /Users/zhangbo/.cache/dxmt-vertex-root.ebUpBs. Pixel static/live arithmetic,
guard bytes and ordinary restoration pass; typed VS rejection remains intact.
Staged D3D12 SHA1 matches build output: normal
fc02e6b710111eaf838a23a518f9307552472b01; no-private
5d03d866f925d34cad7c7892bba1ae458c7d020b. No game/prefix DLL deployment or restart.

## Standards

Readonly review found no documented violations or actionable heuristic findings.
Transactional output and bounded scope verified.

## Spec

Readonly review found no missing/incorrect bounded root-contract implementation
or scope creep. This is not acceptance of the larger VS/PS graphics milestone.

## Known Limitations

Vertex shader preparation/conversion, stage-local record intervals and dual
TLABs remain unimplemented. Vertex GPU correctness and full FL12 qualification
remain open. No new game, performance or tessellation acceptance evidence.

## Capability Status

No promotions; additional typed formats remain disabled.

## Feature Level Impact

None; FL12_0/FL12_1 remain disabled.

## Git Status

Precommit: only these three task files modified; diff check passes.

## Push Status

Not pushed.

## Next Recommended Task

Implement typed vertex preparation/conversion and separate VS/PS TLAB record
intervals, then independently verify VS-only and combined VS/PS GPU readbacks.
