# DXIL MinMax compute dispatch binding

## Task Analysis

- Hypothesis: the existing prepared variant can execute through normal D3D12
  Dispatch once submission owns a coherent private TLAB and all referenced
  resource/sampler tables.
- Evidence: typed-origin has immutable marker replay; MinMax has reflected
  compiler roots, native variants and retained pair preparation. Its three
  private parameters and sampler tables are not connected to dispatch yet.
- Expected effect: opt-in actual DXIL/MSC compute execution with static capture,
  unique volatile slot resolution, private state/point/ordinary descriptors,
  residency and completion-owned lifetime.
- Risk: copying only participating SRVs would lose output UAVs; rereading a
  pair separately from its application table would split descriptor generations.
  Ordinary/typed-origin dispatches must restore their PSO/TLAB afterward.
- Validation: both builds and host suites, isolated actual D3D12 numeric
  dispatches, ordinary/static sampler regressions and static/live overwrites.

No feature-level promotion or full MinMax acceptance is implied. DXIL remains
MSC-only, DXBC remains AIRCONV-only, and preparation failures have no fallback.

## Runtime contract

`DXMT_MINMAX_DXC_DIRECTORY` selects an absolute Windows DXC directory and
explicitly requests the qualified private compute path. It also admits bounded
dynamic reduction descriptors without either AIR MinMax switch. Ordinary MSC
consumers still reject reduction descriptors; the MSC opt-in does not grant an
AIR consumer admission. Both switches unset retain the prior rejection.

The command list owns the original PSO/root, heaps, staging template, preserved
range flags and static snapshots. At each execution, unique volatile slots are
resolved once per heap. Pair state and application tables use those same
snapshots. Native pair creation, shared buffer allocation and encoder residency
calls occur outside heap locks. The buffer contains the reflected TLAB, aligned
32-byte pair states, N point-view entries, 2N point/ordinary sampler entries,
static sampler entries and all participating application resource/sampler
tables, including output UAVs. Private texture clamp and sampler bias are zero.

Completion-owned bindings retain this buffer, snapshots, native resources,
samplers and the static sampler root. Marker replay clones the immutable stream,
uses reflected threadgroups, inserts private PSO/TLAB bindings and never patches
allocator nodes. Recording dirties the ordinary PSO/root arguments afterward.
Indirect dispatch and combined typed-origin/MinMax lowering explicitly reject;
non-null typed buffers without exact ordinary MSC views reject at static
recording or live materialization. Origin-only views cannot slip through.

## Task Result

Both full builds and their four Meson host suites pass. Isolated staged DLLs
match source build hashes, and loader logs identify the matching winemetal
runtime. No installed DLLs or game prefix deployment changed.

`dx12_minmax_dispatch` passes one/two-pair shaders in normal/no-private:

- Six roots: four RS1.1 independent static/live combinations, RS1.0 volatile
  conversion, and ordinary static root samplers with no sampler heap.
- Three closed-list executions per root vary sampler min/max/ordinary state
  and live texture empty-set clamps; static snapshots retain recorded state.
- Two additional executions per root share a closed list, held behind a GPU
  fence until both submission bindings exist. A GPU intermediate copy preserves
  the first result; live sampler replacement yields separate 16/240 results.
- One same-encoder private-to-ordinary pair of dispatches per probe uses
  different output tables, sentinel initialization, an explicit application
  SetPSO stream assertion, and caller heap/output release before execution.
- Combined-private and indirect recording negatives fail without submission.

This is **120** repeated/in-flight numeric compute executions plus **8**
private/ordinary restoration executions across the four probes, not full
MinMax acceptance or 128 independent feature cases. Static slot replacement
checks are ownership robustness observations, not permission to violate a
static descriptor promise in application code.

The mixed Texture.SampleLevel + RWBuffer shader produces an origin-only typed
view. Both builds reject its static recording and live materialization without
invalid GPU execution or partial binding publication. Ordinary MSC dynamic and
static readbacks (255), opt-in AIR dynamic minimum (16), default-off rejection,
and existing typed-origin direct/indirect contracts (14 per build) pass.

Receipts: `/Users/zhangbo/.cache/dxmt-minmax-dispatch.zzz2tO/{normal,no-private}/`
with `pair-{1,2}.final.log`, `typed-rejection.final.log`,
`regression-*.log`, and `typed-origin-regression.log`.

Main-agent self-review and an independent read-only integration audit identified
the missing exact typed-view guard; it was repaired and regression-tested.
Subsequent audit found no new confirmed implementation defect. The MSC binding
guide informed reflection-based pointer writes and strong ownership instead of
integer-handle lifetime assumptions. Tests establish caller-release behavior,
not isolated submission-only ownership of every object after all other owners
are removed.

Remaining production work includes static reduction root samplers, broader
DXIL operations/shapes/formats, meaningful feedback, indirect/direct-indexed
integration, combined origin contracts and default production qualification.
The opt-in intentionally rejects unsupported shaders rather than falling back.
No capability/FL promotion, Metal validation run, full GPU matrix, game launch,
tessellation acceptance or performance benchmark is claimed.
