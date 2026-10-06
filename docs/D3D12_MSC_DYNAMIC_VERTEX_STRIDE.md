# Task Analysis

## Stage-in ABI / private shader continuation

Baseline 80e7a838, clean. Verify the actual generated ordinary stage-in ABI
before binding companion records. MSC CLI 4.0.1 emits a separate metallib;
metal-objdump reflection identifies buffer 6, and disassembly identifies the
address/length/stride record plus dynamic stride multiplication. Add safe unique
function retrieval and propagate input layout/compile flags through typed VS
conversion so subsequent private PSO integration can use the same stage-in mode.
Risk: hardcoded names, old-runtime null-name calls, nonmatching private artifacts.
Validation: source review, compiler artifact reflection/disassembly, full builds
and native regression; ordinary dynamic-fetch GPU acceptance remains required.

## Linked stage-in runtime continuation

Baseline 3cb11e82, clean. Implement the missing ordinary render PSO linked
vertex-function entry before changing shader compilation/binding. Keep the old
pipeline-info layout and Unix call 34 unchanged. Append call 198 with a fixed
wrapper containing the old parameters and a function handle; both PE widths
share this fixed-width ABI. Unsupported older runtimes return no PSO rather
than ignoring linkage. Risk: thunk index/layout mismatch and unbalanced ObjC
ownership. Validation: normal/no-private builds and native regression; linking
and dynamic-fetch GPU tests remain required before enabling this in D3D12.

Baseline 1e507117, feat/d3d12-1, clean. Intended next gap: MSC indirect VB
updates, preserving address, size, stride, instance step and submission lifetime.
Hypothesis: the existing resolver could extend IB updates to VB records.
Risk: accepting an address while ignoring stride produces incorrect input fetch.
Validation for this investigation: trace current ordinary PSO descriptor, draw
binding, converter stage-in generation and existing dynamic-record consumers.

# Source findings

InitializeMSCVertexInput in src/d3d12/d3d12_pipeline_graphics.cpp calculates
strides from input element offsets/format sizes and stores those values in
WMTVertexBufferLayout. They are input-layout extent, not an application's current
D3D12_VERTEX_BUFFER_VIEW.StrideInBytes. The Unix render pipeline factory in
src/winemetal/unix/winemetal_unix.c copies layout.stride into MTLVertexDescriptor
at PSO creation. No dynamic stride command is used in that path.

EncodeVertexBuffers in src/d3d12/d3d12_command_list.cpp binds only buffer handle,
offset and slot for ordinary MSC rendering. It never supplies StrideInBytes to
ordinary Metal vertex fetch. Therefore address-only indirect VB updates cannot
close the contract; direct ordinary MSC vertex fetch has the same source gap.
This is source evidence, not a fresh failing GPU readback.

There is reusable dynamic-record infrastructure: PopulateMSCVertexBufferTable
writes address, length and stride for companion object/mesh consumers. The
converter in src/winemetal/unix/metalirconverter.c can already request separate
stage-in generation and synthesize the corresponding metallib from the input
layout. Ordinary graphics initialization currently loads that library only when
msc_emulation_flags is nonzero. Enabling a conversion flag alone does not link
the function or bind its required record ABI.

# Consequence for the implementation order

The next production task must provide ordinary MSC dynamic vertex fetch first,
then let the indirect resolver write per-command address/size/stride records.
Reuse separate stage-in synthesis and existing companion record construction
where their actual ABI permits, preserving per-instance classification/rate.
Verify that the synthesized ordinary stage-in function consumes the proposed
record layout; do not assume its ABI equals the object/mesh companion ABI.
Host linked-function support, private typed/MinMax variants, shader artifact
cache identity and indirect buffer binding must all agree.

PSO variants keyed only by recording-time strides cannot implement arbitrary
GPU-produced per-command strides. Accepting only the layout extent is not the
requested final solution. No such reduced implementation was added.

Focused oracle: padded/interleaved vertices with a stride larger than attribute
extent, stride changes on the same PSO, nonzero buffer offset, per-instance step,
and GPU-selected indirect records. Use independent pixel/typed readbacks and
ordinary-state restoration; do not rerun the full FL matrix at each ABI step.

# Task Result / self-review

The initial address-only plan was invalidated by the source call chain above.
MSC integration skill's vertex pipeline reference distinguishes fixed Metal
vertex fetch from separately synthesized linked stage-in functions; it does not
prove compatibility of a custom dynamic-record ABI. This changes the next
implementation from resolver admission to dynamic-fetch runtime integration.
No source/capability change or GPU acceptance in this investigation, no push.
VB updates remain rejected. Broader typed indirect and FL12_0 goals remain open.

## Linked-function runtime result

Added MTLDevice_newRenderPipelineStateWithStageIn and its WMT C++ wrapper. A
new fixed-width transport wraps the existing pipeline parameters with one
function handle. Call 198 is appended to both Unix dispatch tables; call 34 and
WMTRenderPipelineInfo are unchanged. The shared native factory installs a
balanced MTLLinkedFunctions object on vertexLinkedFunctions when requested.
The existing factory supplies a zero handle and retains its old behavior.

The PE wrapper checks the existing MSC capability query for native renderer bit
27 before issuing call 198. This bit describes renderer entry availability,
not a libmetalirconverter symbol or qualified D3D capability. MSC-aware older
Unix runtimes without the bit fail before the new dispatch. Missing stage-in
handles and failed dispatch return no PSO; no unlinked fallback is created.

Both reconfigured full builds passed, as did existing native regressions 12/12
per variant. Source self-review checked table append positions, unchanged old
layout/call, output initialization, capability gate and ObjC ownership balance.
git diff --check passed. These tests do not create a linked stage-in PSO; no GPU
dynamic stride correctness or PE32 runtime compatibility is claimed. Logs are
linked-stagein-final-build*.log under the reconciliation cache.

Remaining implementation: verify the synthesized ordinary stage-in record ABI,
connect converter flag/artifact/cache identity and D3D12 PSO selection, bind
dynamic vertex records and extend the indirect resolver, then focused readback.
The original dynamic stride requirement remains incomplete.

## Verified ordinary stage-in ABI and private compile preparation

MSC CLI 4.0.1 compiled graphics_sm6.vs.cso with --vertex-stage-in and a generated
input-layout template. metal-objdump reflection of the VS identifies
vertex_buffers_ab at buffer 6 and a visible function reference. Disassembly of
stride-separate.stageIn.metallib shows vertexBufferAndLength as pointer/i32/i32,
loads field 2 as stride and multiplies it by vertex_id before fetching POSITION
and COLOR. This establishes the 16-byte record ABI for this artifact, matching
the existing address/length/stride builder; it is not GPU correctness evidence.

The MSC skills require querying the generated function name instead of assuming
it. Added MTLLibrary_newUniqueFunction and WMT wrapper: the native library must
contain exactly one function, otherwise it returns null. A distinct renderer
capability bit 28 gates the new null-name query behavior against older MSC-aware
runtimes. Named lookup remains unchanged; retain/release of the selected name is
balanced. No new Unix dispatch number is needed.

Typed-origin vertex conversion now forwards optional input layout and compile
flags to its shared converter, with defaults preserving existing callers.
This avoids losing synthesized stage-in mode when preparing private typed VS.
Ordinary/typed/MinMax PSO selection and dynamic record binding still need wiring.

Reconfigured full builds and final incremental builds pass for normal and
no-private. Existing native tests pass 12/12 each. Source self-review and
git diff --check pass. Host units do not call unique retrieval or prove linked
PSO creation; no new GPU acceptance is claimed. Compiler artifacts and
stride-separate-reflection.log / stride-separate-air.log are retained under
/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE, with stagein-abi-final-*.log
build evidence. No FL capability promotion, prefix/game changes or push.

## Dynamic fetch wiring continuation (baseline 3a0c219f)

Hypothesis: linking the synthesized ordinary stage-in and binding the existing
16-byte vertex records at buffer 6 removes fixed input-layout extent as the
source of ordinary MSC stride. Evidence: the compiler artifact ABI above.
Expected effect: ordinary, typed and MinMax variants share dynamic fetch mode;
indirect root/index updates retain inherited vertex records. Risk: linked PSO
creation, CPU/MSL payload alignment and stage-specific resource retention.
Validation: reconfigure and build both variants, run native regressions, then
focused padded-stride GPU readback. Vertex-buffer command-signature updates
remain rejected; no capability promotion or full-game qualification is implied.

### Dynamic fetch wiring result

Ordinary MSC PSOs with input layouts now synthesize and link stage-in, without
fixed Metal vertex attributes/strides. Typed-origin and MinMax private variants
forward the same layout and synthesis flag. Used IA slots select 16-byte dynamic
address/length/stride records at vertex buffer 6; companion object/mesh binding
is unchanged. Root/index indirect updates carry an inherited record pointer in
the mirrored 176-byte CPU/MSL payload. VB command-signature updates remain gated.

Normal and no-private full builds passed after reconfigure. Native regressions
passed 12/12 each. The new --padded-stride fixture uses stride 64 for 24-byte
attributes, upload padding filled with 0xcd, and VB GPU address offset 16. Both
GPU runs passed the existing pixel, occlusion and timestamp checks; color readback
was 0xff0000ff. The first normal attempt stopped at an unimplemented entry due
to stale cached runtime PE DLLs despite the app-local copy; matching cache-only
PE/native staging fixed the retry. No prefix/game DLLs were deployed.

The existing typed-origin indirect IB fixture passed 32 typed draws and ordinary
restoration per variant. It uses no IA input and therefore does not qualify the
new indirect record pointer path. Same-PSO stride changes, per-instance input,
typed/MinMax IA GPU tests and GPU-selected VB updates remain follow-up work.
No FL promotion, full-game acceptance, FPS claim or push. Logs are
dynamic-fetch-*.log under the reconciliation cache.

Additional packed-stride normal run returned failure: the expected red pixel
was present, but timestamp_end was zero (timestamp_begin 977974628654833).
This run is not counted as passed; attribution needs separate query diagnosis
and baseline comparison. The padded-stride acceptance above remains limited
to its recorded successful runs, not a clean whole graphics regression matrix.
The corresponding packed-stride no-private run passed all fixture checks.

### Submission self-review

Standards axis: no documented breach identified; the shared stage-in layout was
renamed from minmax_stage_in_layout_ to msc_stage_in_layout_ to remove misleading
ownership. Spec axis identified unused stale IA bindings as a recording failure
risk; ordinary direct/indirect builders now filter the active PSO slot mask before
lookup, validation and retention, leaving companion behavior unchanged. Added a
CPU payload offset assertion for the inherited record pointer at byte 168.

Final post-review full builds and native regressions passed for both variants
(12/12 each); both final padded-stride GPU runs passed, red pixel 0xff0000ff.
Two partial validation findings remain: same-PSO stride changes/instance step,
and private/indirect padded IA readback. These are tracked qualification gaps,
not claimed completed requirements. No new hard standards findings remain;
dynamic fetch work and the broader FL goal remain open.

## Indirect VB continuation (baseline 55ae8294)

Hypothesis: a distinct 31-entry MSC record region per indirect command lets the
GPU resolver update address, length and stride without mutating inherited input.
Evidence: linked stage-in consumes pointer/length/stride, whereas AIRCONV records
use pointer/stride/length. Expected effect: ordinary/private MSC DRAW and indexed
DRAW accept VB signatures with dynamic fetch, retaining all registered VA buffers.
Risk: output region overflow, stale inherited bindings, per-command isolation and
post-indirect reset. Validate both builds and focused GPU VB payload readback;
companion and slot 31 support remain separate gaps.

### Indirect VB implementation and validation

Admission now permits VB updates for ordinary MSC dynamic-fetch graphics PSOs
on slots 0..30. Each command gets an independent 496-byte output region, copied
from inherited records before GPU-side updates. Generated MSL writes MSC
pointer/length/stride order, preserving AIRCONV pointer/stride/length. The
existing payload output pointer/stride fields carry these regions; transport
layout does not change. Updated slots are excluded from inherited lookup and
PreDraw skips redundant vertex binding. Registered VA residency retention is
enabled for VB updates, and ResetIndirectState clears the affected slots.
Private typed/MinMax allocation no longer rejects VB updates; GPU qualification
of those private paths remains pending. Empty root signatures get a non-null
16-byte TLAB sentinel and at least one offset allocation element, with zero
template copy bytes; this keeps resolver backend selection valid for VB-only
signatures without changing shader parameter layout.

Both reconfigured full builds and probe builds passed, with host tests 12/12
each. The --indirect-vb GPU probe supplies VB only in the indirect arguments,
stride 64 and address offset 16. Both variants passed pixel, occlusion and
timestamp checks, readback 0xff0000ff. The final fixture adds a second command
with stride 24 and zero vertex count: it must not overwrite the first draw's
records. Both isolation runs passed. Typed-origin IB regressions passed 32
draws plus ordinary restoration per variant. Logs: indirect-vb-*.log in the
reconciliation cache. No prefix/game deployment, capability promotion or push.

Remaining qualification: VB updates on private typed/MinMax shaders, indexed
VB+IB/root combinations, inherited untouched slots, count buffer/predication,
per-instance fetch and post-indirect VB reset readback. Existing packed normal
timestamp failure from the preceding task has not been attributed or resolved.

### Indirect VB self-review

Standards: zero documented breaches, two heuristic findings addressed. The
MSC record/count now have a shared CPU definition with size/length/stride
offset assertions; allocation uses sizeof(record), MSL copy count is emitted
from the same constant. Renamed roots_only to supported_msc_updates.
Spec: no confirmed implementation defect or scope creep; two partial validation
findings remain, untouched inherited-slot preservation and observable post-VB
reset/rebind. Other qualification gaps listed above remain open.

Final post-review full builds passed both variants, native regressions 12/12
each, and the two-command VB isolation GPU fixture passed both with 0xff0000ff.
The MSC integration skill guided distinct record field order, binding point
and indirect resource residency; source review checked per-command output,
updated-slot exclusion and unchanged compute/AIRCONV admission/order. No broad
D3D capability or full-game claim is justified by this increment.

## IA slot 31 continuation (baseline 32f244ea)

Hypothesis: ordinary separate stage-in can fetch slot 31 from a 32-entry record
table, independent of Metal's direct buffer-index limit. Evidence: stage-in
input layout already validates slots below 32, native synthesis forwards the
slot unchanged, and only the dynamic table/admission count rejects slot 31.
Expected effect: ordinary direct/indirect MSC fetch covers all 32 D3D12 IA slots.
Risk: bit-31 mask handling, per-command table stride and accidentally changing
companion behavior. Validation: old PSO rejection, matched shader slot-31 direct
and indirect GPU readback on both variants, plus host/build regression.
Preserve companion count and behavior; element-count limits are separate.

### IA slot 31 result

Before the production change, the new direct slot-31 probe failed at PSO creation
with E_NOTIMPL (0x80004001). Ordinary MSC count now uses the D3D12 32-slot constant;
per-command records consequently use 512 bytes. Shared count drives admission,
PSO slot masks, inherited tables and MSL copies. Companion table count/mask remain
31/0x7fffffff and are explicitly selected for the unchanged object/mesh path.
No shift by 32 is used to construct masks.

Both reconfigured full builds passed and native tests passed 12/12 each. The
--slot31 and --indirect-vb-slot31 GPU probes passed both variants, pixel
0xff0000ff plus occlusion/timestamp checks. Layout POSITION and COLOR both use
slot 31, stride 64 and address offset 16. The indirect probe supplies VB only via
GPU arguments and retains the second non-drawing stride-24 isolation command.
This proves the tested runtime synthesis/link/fetch chain for the highest IA
slot, not 32 input attributes or private/companion GPU qualification.

Slot-0 indirect regression passed normal. No-private produced the expected red
pixel but timestamp_end was zero, so that run is a failure, not a pass. This
matches the symptom of the preceding un-attributed query issue but does not
establish the same cause. Retained logs slot31-*.log in reconciliation cache.
No prefix/game writes, capability promotion or push.

### Slot 31 final self-review

Standards: zero documented breaches; one nonblocking configuration-clump
heuristic remains. The private table builder's stages/mask/count are coordinated
at both current ordinary call sites and its companion defaults remain explicit;
bundling these parameters is optional future refactoring, not a current ABI bug.
Removed the unused ordinary all-slots mask constant.

Spec: zero actionable findings after reviewing the four completed GPU logs.
The initial review's pending-GPU finding was withdrawn because that snapshot
preceded actual verification. Independent source review checked unsigned bit31,
512-byte ordinary output, inherited updated-slot exclusion, and unchanged
companion count/mask. Final full builds and host regressions passed both (12/12).
Final direct probes passed both and indirect no-private passed. Final indirect
normal initially failed timestamp_end=0 despite red readback, then a separate
serial retry passed every fixture check. Both logs are retained: this is not a
claim of clean repeated-run query reliability or attribution of the failure.

MSC compilation/integration skills guided the distinction between layout slots,
native Metal buffer indices and separate stage-in records. No native converter
or companion API change was needed. Full private/instanced/indexed qualification
and the broader FL goal remain incomplete.
