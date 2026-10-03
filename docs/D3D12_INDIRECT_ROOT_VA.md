# GPU-selected indirect root addresses

## Task Analysis

- Hypothesis: GPU-written argument buffers cannot be safely resolved by reading
  CPU argument memory at recording/submission. Root addresses are already native
  Metal GPU addresses; retaining the authoritative registered buffer set at
  submission and declaring indirect access permits GPU selection without a
  separate VA translation table.
- Evidence: device VA registration replaces same-address entries under its
  residency mutex, but LookupBufferByVA returns a borrowed allocation; MSC
  indirect currently admits only constants. Encoder replay already provides
  submission-scoped retained state and native useResource fan-out.
- Expected effect: MSC CBV/SRV/UAV indirect root updates use reflected offsets,
  while AIR and MSC GPU-selected buffers remain resident/alive through completion.
- Risk: conservative all-buffer retention/access cost, address-map races and
  remaps, resource registration assumptions, invalid VA inputs, and lifetime
  after application resource release. This is not full sparse/aliasing closure.
- Validation: both builds/host suites, GPU-produced root arguments with real
  readback, post-Close resource registration and gated release-before-completion,
  existing constants and typed-origin regressions, plus self-review. No feature
  promotion or full matrix/game performance claim.

Review follow-up before admission: UnregisterResidencyAndVA currently erases an
allocation-matching entry before checking its resource owner. A replacement
owner of the same allocation can therefore lose its new VA/residency registration
when the old owner unregisters. Require both identities to match and add a
runtime registry/snapshot regression; this is not native GPU address-reuse proof.

## Task Result

MSC compute command signatures now admit constant and CBV/SRV/UAV root updates.
The GPU resolver writes 64-bit root addresses into reflected TLAB offsets; AIR
continues to write its own qwords. Reflection validates each updated parameter's
kind, size and offset. Typed submission templates/destination arrays remain
per-submission and immutable allocator command nodes are not patched.

Only compute encoders containing GPU-selected root VA updates snapshot the
device's registered buffer allocations immediately before replay. Strong
allocation references are acquired under the registry mutex, with useResource
read/write fan-out after unlocking. Submission objects keep those references
until completion. The snapshot never reads CPU argument memory and sees buffers
created after Close. Ordinary compute/constant-only indirect paths do not incur
this scan. Old owner unregisters now require both allocation and resource owner
identity before deleting a current registration.

Evidence: `/Users/zhangbo/.cache/dxmt-indirect-va.uou4RS`.

- Old `daf543f` DLL rejected the new MSC signature at recording.
- Both reconfigured full builds, optional probe builds, final full builds and
  host suites (5/5 each) passed. The first attempt to invoke the newly added
  optional target before regenerating Meson's target catalog failed; explicit
  reconfiguration followed by successful target builds resolved that tooling
  issue. It was not runtime/GPU evidence.
- Four AIR/MSC GPU root-VA probes passed across both builds. An AIR producer
  writes two 40-byte indirect commands to a default-heap UAV, then a barrier
  transitions it to INDIRECT_ARGUMENT. All three root addresses and a constant
  differ per command, yielding 107 and 209. CBV/SRV buffers are created after
  Close; a queue fence prevents execution until their application references
  have been released after translation. This proves internal retention in this
  fixture, not a relaxation of application lifetime obligations.
- Ten typed-origin root-CBV full-buffer readbacks passed: slots 0/1 with
  volatile/static tables and a volatile SRV updated after Close. The CBV allocation
  and argument VA are also populated after Close. No static descriptor is mutated.
- Sixteen AIR/MSC constant-update regressions plus four existing typed direct/
  constant-indirect regressions passed: **34 focused GPU executions total**.
- Two internal registry runtime probes passed stale owner/anonymous unregister,
  replacement-owner lookup, and snapshot allocation lifetime after public
  resource release. These manipulate same-allocation ownership and are not GPU
  readback or native same-address different-allocation remap qualification.
- No game/prefix DLL changes, process kills or feature-level promotion.

Standards self-review: checked allocation ownership under lock, destructor/drop
ordering outside the lock, submission retention, allocation-failure handling,
bounded reflected writes and unchanged unsupported graphics/MinMax admission.
Spec self-review: verified GPU arguments rather than CPU inspection, late buffer
registration, gated retention, independent per-command roots and unchanged
static/volatile table behavior. Fixed stale owner unregistration and retired the
old root-VA rejection probe, replacing it with numeric acceptance. No remaining
actionable finding in this admitted compute scope. Main-agent reviews only;
independent review subagents are unavailable. MSC integration guidance determined
the reflected 64-bit root-address writes and indirect residency requirements.

Conservative cost: each affected encoder scans and retains all registered
buffers with read/write usage; no performance claim is made. Generation-cached
snapshots/access refinement can follow profiling. Graphics indirect roots,
same-address native allocation reuse, sparse/aliasing/remapping synchronization,
invalid GPU VA behavior, repeat/concurrent-list execution, complete matrices and
real game/tessellation/performance acceptance remain open. FL12_0 is not closed.
