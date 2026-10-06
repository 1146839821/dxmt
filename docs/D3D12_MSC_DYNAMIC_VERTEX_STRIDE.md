# Task Analysis

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
