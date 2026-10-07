# Tiled raw/structured feedback production gap

Baseline cc665176. Task Analysis: prioritize a real Tier 2 implementation gap,
not another complete matrix rerun. Packed array tails are not the next missing
feature: the existing packed audit records Tier 2's rejection of substandard
array mips. Do not widen that normative boundary. Single-slice packed-tail
translation still requires compatible D3D/Metal tail boundaries and byte sizes.

The next identified gap is raw/structured sparse access status. Current texture
feedback reaches AIR sparse residency and StoreFeedback canonicalizes the opaque
status for CheckAccessFullyMapped. InstLoadRaw and InstLoadStructured have no
feedback destination or residency input; ordinary pointer loads cannot manufacture
a Metal texture sparse-access status. The Tier 2 gate therefore correctly marks
this path DESIGN_REQUIRED / BLOCKED_BY_ARCHITECTURE.

Hypothesis: a shader-visible mapping sideband can represent the residency of the
actual byte footprint of a raw/structured access. This requires more than accepting
an opcode: identify every 64KiB tile touched by the instruction's components and
view origin, guard OOB/overflow, then return mapped status only when every touched
tile is mapped. TGSM and ordinary committed buffers must remain distinguishable.
Do not substitute a constant success flag or a CPU-time snapshot for GPU ordering.

Required implementation seams: resource mapping state ownership; descriptor/root
transport of sideband identity and view-relative offsets; AIR raw/structured
operation decoding/lowering; queue-ordered sideband updates matching sparse mapping;
completion-owned lifetime across repeated submission and cross-queue remapping.
DXIL needs an independent MSC-compatible solution; DXBC support alone cannot close
Tier 2. Keep feedback consumers rejected until all these seams are connected.

Expected effect: real mapped/NULL/mixed byte-footprint feedback without changing
shader-family routing or promoting TiledResourcesTier. Risk: stale mapping
generations, tile-straddling vector loads, allocation churn and unsafe dynamic
root provenance. First proof must include mapped and NULL accesses with identical
numeric zero data (to prevent value-based status inference), a tile boundary
straddle, nonzero view origin, and remap ordering. This is source-backed task
preparation, not implementation or GPU acceptance.

## Byte-footprint prerequisite

Added sparse_buffer_footprint.hpp: bounded component-mask geometry computes
unique resource-relative 64KiB tile indices after view-origin translation.
Per-component four-byte accesses are checked independently; masked-out bytes do
not create tiles. OOB, empty/invalid masks and arithmetic overflow reject without
publishing a partial result. No residency/status decision is made here.
Native unit tests pass in both configured builds, including all 15 nonempty masks,
view-origin boundary crossing, unaligned component crossing, mask selectivity,
invalid view bounds and overflow. Main self-review confirms the maximum output
capacity is four components times two tiles; end-address arithmetic is bounded
by the validated resource/view range. Queue-ordered GPU mapping sideband,
descriptor transport, actual AIR/DXIL lowering and GPU mapped/NULL oracles remain
unimplemented. This prerequisite is not production feedback admission or Tier 2
qualification; current shader feedback rejection remains unchanged.

## Native queue write prerequisite

The current sparse-mapping handle is an MTL4CommandQueue. SDK-confirmed
MTL4ComputeCommandEncoder fillBuffer writes permit GPU-ordered sideband updates
on that queue. The opt-in native sparse_sideband_queue prototype brackets writes
with ResourceState-to-Blit consumer ordering and Blit-to-All device visibility,
retains command allocator/resource/residency through a shared-event completion,
and alternates two independent bytes over eight phases. Both builds compile and
both native runs pass (`sparse-sideband-queue-{normal,np}.log`).

This proves the buffer-write primitive and completion visibility only. No sparse
mapping operation or shader feedback consumer is in this prototype; the producer
ResourceState dependency has no matching mapping workload yet. It must not be
credited as atomic map/status coupling, cross-queue remap correctness or production
feedback admission. Next connect actual mapping operations and sideband ownership
before adding the optional Wine boundary and AIR descriptor transport. The Metal
resource/synchronization skills guided explicit residency, stage barriers and
allocator lifetime; the native target is not built by default.

## Actual mapping prerequisite result

The native target now allocates a two-page placement sparse buffer plus a
compatible placement heap and includes both in its residency set. Each phase
maps one virtual page, unmaps the other, alternates the physical heap page,
then writes sideband bytes and a phase-distinct payload on the same queue.
ResourceState-to-Blit uses ResourceAlias visibility; dependent buffer readback
uses an explicit Blit-to-Blit device barrier. Both native builds pass eight
phases (`sparse-sideband-map-{normal,np}.log`), with numeric 0x40+phase data checks
in addition to sideband bytes. Resources and allocator survive the GPU signal.

This supersedes the earlier no-mapping probe only for native mapping/write
ordering. The probe serially waits each phase and copies only the mapped page;
NULL-page shader access, cross-queue overlap, raw/structured status, descriptor
transport, ABI versioning and production integration remain unfinished.
Main self-review: uses SDK tile units for bufferRange/heapOffset; checks support,
residency, exact independent per-phase data and status expectations; no CPU bitmap
write substitutes for GPU updates. No production admission, prefix deployment,
game test or capability promotion follows. These prerequisites are ready for a
local checkpoint commit, not completion of the sparse-feedback task.
