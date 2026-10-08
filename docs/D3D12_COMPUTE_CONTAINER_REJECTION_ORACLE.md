# Compute container rejection invocation oracle

## Task Analysis

Branch `feat/d3d12-1`; reference `origin/feat/d3d12`; task baseline `15d31de`.
Hypothesis: public rejection alone cannot exclude compiler work before rejection.
Evidence: existing container tests lack compiler instrumentation; the test-linked
compute factory already wraps AIRCONV initialization/compile and MSC conversion.
Expected effect: five mandatory modes require exact rejection HRESULT, cleared sentinel
output, empty trace and zero calls to both backends.
Risk: a hybrid fixture could be merely malformed. Reuse real executable chunks
and assert Ambiguous classification for duplicate/hybrid before factory invocation.
Minimal plan: bounded compute-only fixtures and host aggregation coverage.
Validation: both configured builds/tests, fresh Wine invocation receipts with
Metal API/GPU validation, provenance verification and normal DLL restoration.
No production shader ABI/runtime change, fallback, capability or FL promotion.
Graphics malformed/ambiguous invocation rejection is outside this bounded task.

Runtime-refined HRESULT contract: truncated/invalid-offset containers preserve
the parser's E_FAIL; structurally valid missing/duplicate/hybrid executable
families return E_INVALIDARG. The initial blanket E_INVALIDARG assumption was
incorrect. No production behavior is changed or generic FAILED-only check used.

## Task Result

Branch/reference unchanged. Local commit: this document's commit. Changed:
test-linked compute harness, gate registration, host tests, gate guide and audit
follow-up. Production compiler/runtime code is untouched for both backends.

Five modes added; duplicate/hybrid reconstruction copies actual executable
chunks and requires Ambiguous classification. Rejection requires exact HRESULT,
null output after non-null sentinel initialization, zero AIRCONV init/compile,
zero MSC calls and empty trace. Each mode is individually mandatory in host tests.

2026-10-02 validation: both configured builds successful after fixing the test's
enum reference; host tests 54/54 PASS and Meson tests 3/3 per build. Normal full
gate initially exercised 444 modes: the 439 old modes and three new modes
passed; truncated/offset failed due to incorrect expected HRESULT. That failed
receipt is retained at `build/fl12-container-normal.json`, not relabeled PASS.
Source inspection confirmed `CDXBCParser::ReadDXBC` E_FAIL propagation through
classification into the compute factory. After correcting exact expectations,
fresh normal/no-private compute-only oracles each passed 16/16, including all
five new modes. No final 444/444 full-matrix rerun is claimed.

Final receipts: `build/container-compute-normal.json` and
`build-no-private/container-compute-no-private.json`. Both have PASS provenance
before/after, API/GPU validation markers for every mode and exact zero-call
records for the five rejections. No failed-assertion or Shader/GPU Validation
Error markers found. Normal DLLs restored; fresh provenance passed.

### Standards

Independent review against 15d31de: no hard violations or concrete bugs; one
nonblocking raw-layout constant naming suggestion retained for narrow test reuse.

### Spec

Initial review found pending runtime evidence; both fresh compute receipts and
restoration checks now supply it. The HRESULT correction preserves the parser
contract rather than accepting any failure. Independent follow-up source review
confirmed parser -> classification -> compute initialization -> factory HRESULT
propagation and unchanged strict sentinel/zero-call assertions; no source defect.
Runtime receipt verification was performed by the main agent, not that reviewer.
Code-review skill separated the axes; Metal validation
skill guided enablement and stderr checks. No GPU execution semantics acceptance.

### Limits / Capability / Git / Next

Backend isolation PARTIAL: graphics malformed/ambiguous zero-call traces remain
open. Feature level/capabilities unchanged; no FL12 promotion. Only task files
included, NOT PUSHED. Next: the corresponding bounded graphics rejection oracle,
then resume mandatory min/max/typed-UAV semantic validation.
