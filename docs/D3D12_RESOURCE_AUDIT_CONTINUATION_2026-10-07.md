# Resource closure continuation — 2026-10-07

2026-10-08 D3D12 MSAA integration: the missing scalar R32 integer MSAA capability
was blocking committed texture creation. Correcting it leaves integer resolve
unsupported. D3D12 render-to-texture2DMS-to-raw-root-UAV readback now validates
four distinct samples through all sixteen LogicOps on both normal and explicit
experimental no-private paths under API validation (128 comparisons). The new
MSAA lowering flag defaults off and is cleared by formal qualification. This
qualifies the bounded allocation/view/residency/barrier/readback chain, not all
sample counts, side-effect frequency, lifetime/root layouts or full raster.
See D3D12_LOGIC_OP_MSAA_INTEGRATION_2026-10-08.md; FL/SM declarations unchanged.

2026-10-08 MSAA mechanism evidence: native public MSC fetch preserves four
different R32_UINT sample values through XOR, with ordinary and explicit
sample-ID shaders, on M4 under API validation. Raw compute sample reads avoid
resolve-based evidence loss. This bypasses DXMT D3D12 root/residency/binding paths
and therefore does not qualify them or shader side-effect frequency. Production
single-sample admission remains unchanged. See
D3D12_LOGIC_OP_MSAA_FEASIBILITY_2026-10-08.md for the next integration requirements.

2026-10-08 combined-MinMax pixel evidence: sixteen LogicOps pass static reduction
samplers, dynamic MIN/MAX and same-PSO MIN/MAX/LINEAR/MIN replay on both builds
under API validation (160 cases, 256 submissions). Volatile sampler replacement
occurs only after fence completion; the immutable list restores texture/attachment
initial states and uses increasing fence values. This validates the tested live
descriptor reread together with framebuffer-feature handling, not same-address VA
remap or full resource lifetime/stage/root-layout coverage. Source changes are
test/gate only. See D3D12_MSC_LOGIC_OP_LOWERING.md; formal capabilities unchanged.

2026-10-08 private-variant update: pixel-only framebuffer feature-space settings
and transformed bytecode are retained for draw-selected Typed/MinMax variants.
No synthetic framebuffer descriptor/residency entry is added. Typed+XOR checks
actual R32_UINT output, typed-UAV side effects and guards, submission-time live
SRV replacement, and same-encoder ordinary root restoration on both builds with
automatic/explicit compiler selection under API validation (64 submissions).
MinMax composition has implementation changes but no combined GPU acceptance
yet. Same-address VA remap, remaining layout/lifetime matrices and simultaneous
Typed+MinMax stay open. See D3D12_MSC_LOGIC_OP_LOWERING.md; no capability promotion.

2026-10-08 root-space collision update: application descriptor ranges, root
constants/descriptors and static samplers participate in feature-space exclusion.
Retries lower the original shader with a decreasing ceiling and preserve existing
TLAB indices, descriptor ownership and residency behavior. Two occupied root
constant spaces plus a consumed/rebound CBV pass all sixteen operations on both
builds under API validation. The expanded no-private gate passes eighty executions
but remains PARTIAL. Old native rejects the explicit ceiling safely. This closes
the tested collision layout, not same-address VA remap, all table/static-sampler
layouts, private Typed/MinMax composition or complete resource/raster coverage.
See D3D12_MSC_LOGIC_OP_LOWERING.md for evidence. No capability is promoted.

2026-10-08 LogicOp root-CBV evidence: real shader reads and a second draw with a
different aligned CBV address/source pass sixteen operations on both builds with
API validation. This qualifies that root-buffer/TLAB combination; it does not
prove same-address VA remap, table/static-sampler layout, or Typed/MinMax private
variants. See D3D12_MSC_LOGIC_OP_LOWERING.md for provenance and remaining gaps.

2026-10-08 programmable-logic update: MSC now consumes attachment inputs through
its public compiler feature space, not a host descriptor or new residency entry.
The PSO checks application root-space collisions and uses local compiler state;
application TLAB/descriptor ownership is unchanged. RGBA8_UINT overlapping-draw
readback passes, but nonempty-root layout, private Typed/MinMax composition,
collision retry and complete raster qualification remain open. See
D3D12_MSC_LOGIC_OP_LOWERING.md; no Tier/FL/resource capability is promoted.

Branch: feat/d3d12-1. Starting HEAD: 96b935bc2c0ad94c79bad0af1d5e7062780222ff.
Scope: refresh current implementation/evidence classification for closure
infrastructure consolidation, not Tier 2 or feature-level promotion.

## Analysis and evidence boundary

The checklist and Round 2 audit were read completely before this update.
Their dated M1/MSC results, packed-tail normative restrictions and native GTX
1650 observations remain unchanged historical evidence. The flat-VA AIR blocker
described in that historical snapshot is no longer an accurate description of
the current implementation. Hypothesis: a backend/scope-specific continuation
ledger prevents both stale architecture blockers and premature closure claims.
Expected effect: accurate remaining work for qualification. Risk: focused passes
could be generalized to all stages, formats, lifetime or native semantics.
Validation: inspect current ABI/lowering/transport and rerun bounded compute
oracles with matching cache binaries; do not manufacture broader validation.

## Current resource ledger

| Area | Implementation/evidence | Closure status |
| --- | --- | --- |
| AIR raw/structured feedback | StoreBufferFeedback consults a GPU mapping byte per 64KiB tile, aggregates component footprints and writes an independent opaque status. Reserved allocation publishes a 32-byte immutable header with actual D3D width, not padded extent. | IMPLEMENTED / PARTIAL qualification; not a remaining blanket AIR design blocker. |
| AIR table SRV/UAV transport | AIR descriptor word 3 carries the header VA; word 2 remains the UAV counter. Current focused table SRV compute remap/boundary/zero-data oracle passes on both builds. | DXMT_LOCAL_PASS for that fixture, broader table/view/format/lifetime matrix open. |
| AIR root SRV/UAV transport | Slot 8 supplies submission-owned count plus 24-byte VA-range/header rows. Registry snapshot retains buffers/header/bitmap and native fan-out follows outside the lock. Root-consuming shader query avoids table-only transport. Current indirect compute root UAV oracle passes on both builds. | IMPLEMENTED / PARTIAL; alias logical width, same-address remap, overlapping submissions and performance remain open. |
| AIR graphics/emulated indirect | Existing committed VS/PS/GS/HS/DS transport, count/predication, multi-command roots/constants and GPU VB table updates have bounded readback checkpoints. | PARTIAL; no full-stage/full-topology/format qualification. IB updates still rejected; concurrent private-output reuse unqualified. |
| DXIL/MSC raw/structured feedback | AIR header/table is not an MSC descriptor/TLAB extension. No corresponding MSC status-sideband implementation is established here. | BLOCKED_BY_ARCHITECTURE / implementation gap; AIR passes cannot close both backends. |
| Mapping order and ownership | Reserved buffers own header/bitmap allocation identities; sparse mapping/copy and queue event ordering are implemented. Completion-owned snapshots retain selected buffers. | Source-inspected plus serial remap LOCAL_PASS; cross-queue/in-flight/early-release matrices remain unqualified. |
| Texture feedback breadth | Historical representative Load/sample/filter evidence is retained, not refreshed by buffer tests. | PARTIAL / current complete gather/compare/typed-UAV and native comparison missing. |
| Packed tails | Existing compatibility guards and Tier 2 array rejection remain normative boundaries. M1 tail mismatch remains an observation, not an assertion about all M4 descriptors. | Conditional implementation; current physical-layout/mapping/CopyTiles qualification incomplete. |
| ResourceMinLODClamp | Fractional metadata fix and historical MSC/M1 failure remain recorded separately from per-sample clamp. | Current ordinary/reserved, fractional/MostDetailedMip, AIR/MSC qualification UNVERIFIED. |
| Native Windows oracle | Prior GTX 1650 observations are adapter-specific; no new native execution/debug-layer data acquired here. | UNVERIFIED for remaining comparisons. |

## Authoritative source seams inspected

- src/airconv/air_sparse_buffer_abi.hpp: header size/offset assertions and
  separate RootBufferFeedbackEntry structure.
- src/airconv/nt/dxbc_converter_base.cpp: StoreBufferFeedback validates each
  accessed component, actual resource extent and tile index; volatile GPU bitmap
  loads feed the opaque predicate separately from data.
- src/airconv/dxbc_binding_rootsig.cpp: root containing-range lookup and
  descriptor-table header extraction. Root remaining byte count currently uses
  allocation range, so logical-width/alias qualification is not silently closed.
- src/d3d12/d3d12_buffer.cpp: immutable auxiliary identity publication and
  GPU-ordered mapping-sideband update/copy paths.
- src/d3d12/d3d12_command_queue.cpp: registered-buffer snapshot, table creation,
  completion ownership and resource fan-out. Linear table lookup and snapshot
  costs are not measured by correctness tests.
- src/d3d12/d3d12_device.cpp: TiledResourcesTier remains NOT_SUPPORTED.
  Temporary SM6.6/FL12_0 opt-ins remain separate from individual feature options.

## Fresh bounded validation

Both runs explicitly set DXMT_EXPERIMENTAL_SM6_6=0 and
DXMT_EXPERIMENTAL_FL12_0=0. WINEPREFIX was the existing Wine prefix; only cache
runtime/overlay DLLs were synchronized. No game/prefix deployment or process
restart. Initial PE mismatch invalidated the preliminary four runs as current
build evidence; matching binaries were copied and all cases rerun.

Current D3D12 SHA256:

- normal: 7476dd5f4b902f510d00d146935d7ca703d828b4c6dcf71a3692474639b302cb
- no-private: cbeb915b67c15a317ae4016b465e665aa218bc0e87564c1ba6114fc0904a0b45

D3D12 and DXGI build/overlay/runtime hashes now match. Winemetal PE and installed
Unix runtime matched by byte comparison. Extra WINEDEBUG=+loaddll executions
confirm PE module paths under safety-normal or safety-no-private, not system32.
Actual loaded Unix-image tracing is not established by PE loader output alone;
the runner/provenance consolidation must retain this distinction.

Commands are dx12_buffer_feedback.exe (direct table SRV) and
dx12_buffer_feedback.exe --root --uav --indirect (compute root UAV ICB), each on
normal and no-private. Four matching runs exit 0 and print BUFFER_FEEDBACK PASS.
They check raw/structured zero payloads, independent mapped status, cross-tile
access and four serial alternating mappings. Two loader-trace table reruns also
pass. Logs: cache dxmt-reconciliation.ZLDvwE/closure-audit-matched-*.log and
closure-audit-provenance-*.log. Runtime snapshots report MSC API 4.0.1,
macOS 27.0.1, AppleFamily 1009 and maxShaderModel 96 (SM6.0).
These are existing-build GPU reruns, not new builds, complete matrices or native
Windows comparisons. This documentation-only change has no production ABI or
capability effect.

## Review and next work

Main-agent standards/spec self-review, informed by code-review, checked dated
evidence, backend isolation, binary provenance and partial vs complete scope.
No independent sub-agent review performed. Review identified and corrected the
cache PE mismatch before accepting fresh runs. No outstanding actionable issue
in this ledger; full resource closure remains PARTIAL, not achieved.

Next: isolate experimental qualification environments and establish actual-module
provenance in the gate; resolve timestamp oracle; measure root-feedback costs;
then execute/register complete Typed/MinMax/Tiled/format/raster matrices.
No qualification requirement, native comparison or fresh ROTTR acceptance is
waived. No push.
