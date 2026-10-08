# MinMax pair observation

## Task Analysis

- Hypothesis: independent texture/sampler static and volatile observations are
  required before the prepared MinMax variant can be safely selected.
- Evidence: pair binding accepts snapshots but has no recording/submission
  policy. RS1.1 flags describe each range independently; RS1.0 is already
  converted to volatile by the deserializer.
- Expected effect: capture static components once and resolve only unique live
  slots at submission, retaining coherent sampler objects and API descriptors.
- Risk: mixed pairs must not reread the static half; failure must not publish
  partially prepared native pairs. This helper alone does not admit Dispatch.
- Validation: real heap overwrite probes for all four static/live combinations,
  invalid inputs and repeat materialization in both builds; host regressions.

The caller supplies resolved heap indices and preserved range flags, or an
original static sampler description. Root/table resolution and encoder replay
are separate remaining integration work. Heap locks cover snapshot acquisition
only; native sampler creation occurs after both locks have been released.

## Task Result

Implemented pair observation and materialization helpers. Only
DESCRIPTORS_VOLATILE requests a live read; DATA_VOLATILE alone preserves the
recording-time descriptor. Static root samplers use the supplied original API
description and no sampler heap. Unique live texture/sampler indices resolve
once per heap, then native pair creation runs outside either heap lock.
Candidate outputs publish only after every pair succeeds.

Both builds pass full compilation, four Meson host suites and the real Wine
heap probe: all four static/live combinations, a duplicate live pair, repeated
materialization, invalid-index recording/submission and late unsupported-pair
failure without partial publication, plus a static root sampler without heaps
at submission. Existing native pair/state/subset/lifetime regressions pass.
Receipts: `/Users/zhangbo/.cache/dxmt-minmax-observation.Dn15SZ/{normal,no-private}/pair.final.log`.
Loader paths identify staged D3D12 and matching isolated winemetal runtimes;
staged/source D3D12 hashes match for each build. No installed DLL deployment.

Main-agent self-review checked flag semantics, strong snapshot ownership,
unique live lookup, lock scope and atomic publication, and added the late-pair
failure regression. This is not independent review evidence. MSC integration
guidance informed retaining owners rather than retaining only integer handles.
No command-list admission, private TLAB assembly, encoder replay, numeric GPU
dispatch or game/performance acceptance is claimed. Those integration steps
remain the next task; no capability promotion.
