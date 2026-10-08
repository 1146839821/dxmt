# MinMax numeric qualification

Baseline cf285a31. Hypothesis: existing numerical reduction fixtures can expose
current implementation gaps when registered independently from rejection
contracts. Evidence: eight basic static/dynamic MIN/MAX runs pass, but the FL12
runner registers only rejection and ordinary controls. Expected effect: fresh,
backend-specific GPU evidence with explicitly incomplete coverage, not promotion.
Risk: inherited AIR gates could mask MSC admission, incorrect shader/shape pairs
or rejection success could masquerade as numeric acceptance. Validation: fixed
shader/mode/expected-value manifest, exact markers and nonzero-exit checks,
isolated AIR flags, deployed MSC compiler identity, both builds, negative gate
tests and existing rejection controls. Unsupported/full-matrix gaps stay visible.

No production renderer or capability change, game/prefix deployment, game restart
or push. The full FL12 objective remains open; numeric subsets cannot close it.

## Implementation

The gate now registers a separate minmax_gpu_matrix with 52 fixed compute cases:
16 shared cases per backend (static/dynamic 2D SampleLevel/SampleGrad, 1D, cube,
cube-array), four additional MSC array/3D cases, and 16 AIR gradient/descriptor
observation/live sampler switching/resource and instruction-clamp cases. Inputs
are CPU-seeded and the fixture checks its exact integer readback; runner markers
include the integer's line terminator to prevent prefix matching (16 vs 160).
Dynamic-switch additionally requires the 16/240/128/16 replay sequence marker;
static/volatile observation requires its own ownership marker.

Qualification children force both AIR MinMax gates to zero by default. Only
numeric DXBC cases explicitly opt into both gates, recorded in each controlled
environment. MSC cases never enable AIR gates; --minmax-compiler-dir deploys
the DXC/validator pair with existing target-process path/hash enforcement and
automatic module-relative production discovery. The option does not deploy DXC
to rejection controls or change --typed-compiler-dir behavior. Missing compiler
deployment leaves all MSC numeric cases UNVERIFIED; failed/missing/changed-runtime
cases cannot be excluded from aggregation.

Numerical execution is aggregated separately as execution_status. Even if all
52 executions PASS, category status is PARTIAL with a nonempty coverage_gaps
list. The mandatory ledger adds min_max_numeric_gpu_matrix; its rejection-only
min_max_reduction_filtering row remains nonpassing. Complete anisotropy, format/
address/filter combinations, implicit/bias/cube derivatives, comparison and
arrays, graphics, indirect/lifetime, sparse feedback and native Windows oracle
are explicitly outstanding. These are not smaller substitute acceptance goals.

## Evidence and review scope

The first eight basic diagnostics passed but inherited AIR opt-ins; do not use
them to prove independent MSC admission. The registered matrix explicitly
isolates backend controls and records observed compiler/runtime modules.
Evidence resides under /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE.

Both fixture builds pass. qualification-minmax-numeric-{normal,no-private}.json
each records 52 PASS executions and category PARTIAL (104 numerical executions).
The final exact-line markers were also checked against every captured output;
all match. Independent MSC cases observe both compiler modules with AIR gates
zero. qualification-minmax-numeric-api-no-private.json passes all 52 cases with
Metal API Validation Enabled in every process (52 additional correctness runs,
not timing data). This is CPU-side API validation, not shader validation.
qualification-minmax-isolated-controls-{normal,no-private}.json each passes 13
rejection/ordinary controls with both AIR gates zero despite parent opt-ins.
Both 16-test host suites pass, including 77 Python tests; git diff --check passes.
No complete FL12 runner execution is claimed by these category-specific runs.

New tests require all 52 numerical cases, exact markers, independent backend
controls, nonpromotion after successful execution, missing compiler evidence,
case failures, runtime hash consistency, explicit environment isolation and
mandatory gate registration. Review found no remaining actionable issue in this
runner scope. No production changes were needed for the observed numeric subset.

Main-agent Standards/Spec self-review uses baseline cf285a31 and the closure
consolidation requirements. The exact-marker improvement and backend-control
tests address evidence risks. No independent review, complete MinMax acceptance,
performance evidence, renderer fix or FL promotion is claimed.
