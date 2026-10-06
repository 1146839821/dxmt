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
