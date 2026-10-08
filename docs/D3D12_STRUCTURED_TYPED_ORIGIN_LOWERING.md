# Structured DXIL typed-buffer lowering

## Task Analysis

Baseline `3a9d715`, clean branch `feat/d3d12-1`. Continue actual implementation
gaps before enlarging matrices. The existing textual adapter is not a general
runtime lowering implementation. A fresh extraction of real SM6.0 DXIL showed
that the configured native LLVM 15 BitcodeReader can read its bitcode.

- Hypothesis: structured IR rewriting removes fixed main/register/control-flow
  restrictions while preserving typed conversion in MSC.
- Evidence: current offline adapter matches SSA text and excludes branches;
  production already owns exact/alternate native views and logical metadata.
- Expected effect: a native lowering component compiled with winemetal, with
  resource-to-record mapping and load/store/32-bit atomic guards for all typed
  bindings, including output UAVs.
- Risk: DXIL validation and container regeneration differ from LLVM validity;
  handle provenance, array resources, root reservation and metadata publication.
- Validation: native build and a real-container focused lowering run; DXC
  validation/MSC/GPU integration still must follow before runtime enablement.

## Implementation contract

Source in `src/winemetal/unix/`, separate from the AIRCONV frontend. Calls must
operate on a private module and publish only on success. Typed resources are
recognized by resource kind and class/range ID, not class spelling or metadata
list position. Register spaces and registers are arbitrary. Existing CFG is
split using LLVM utilities; loads/atomics merge zero for invalid coordinates,
stores skip invalid writes. Unsigned logical bounds and addition wrap are
checked independently. LLVM module verification runs after rewriting.

Current supported provenance is legacy static createHandle with single-element
typed resource ranges, up to 64 records. Dynamic/nonuniform/modern handle flows,
arrays, load status/aggregate use and operations needing separate lowering
(including GetDimensions) reject. Textures/raw/structured resources are not
rewritten. Hidden CBV b0/space1 collision with shader resources rejects;
application root collision checks belong to the still-pending root integration.
The pass returns resource bindings in private 16-byte CBV record order.

The native command-line harness parses a real container and verifies the input
module before invoking this production source. It outputs IR, not a validated
regenerated container. Neither its existence nor LLVM verification proves DXIL
or GPU correctness. No converter invocation, PSO hookup, root augmentation,
cache identity change or capability promotion is claimed by this increment.

## Task Result

Reconfigured normal/no-private builds; built native harness and winemetal.so in
both. Existing Meson suites passed 3/3 each. No runtime DLL deployment occurred.
The helper is compiled into the native runtime but has no PE thunk or converter
call site yet; it is not an enabled shader path.

Real-container focused results: FLOAT shader lowered with records for u0 AND
output u1; DXC assembler plus full validator accepted the regenerated container.
The second HLSL fixture exercises u7/space3, u9/space4, existing b3/space7,
branches, a loop and InterlockedAdd. Its structured rewrite passed LLVM verify
and full DXC validation. Final results are in ignored
`build/structured-origin-float-final-dxc.log` and
`build/structured-origin-cfg-final-dxc.log`. Normal/no-private emitted CFG IR
matched byte-for-byte; these are host tool variants, not two GPU runtime runs.

Three LLVM-valid synthetic negative inputs (opaque CBV return type, i128 handle
class parameter, aliased handle producer) each rejected with no emitted IR in
both variants; an unmodified control succeeded. These rebuilt test containers
have placeholder hashes and are not DXIL-validator acceptance cases. The
negative checks protect the review fixes, not an arbitrary-input security claim.

Initial LLVM-printed anonymous block numbering failed DXC assembly. Naming
anonymous blocks/values via LLVM resolved it; no text token rewriting is used.
CFG changes use LLVM splitting utilities and final verification, following the
LLVM skills. Both independent review axes found input-contract weaknesses;
complete handle signature, existing CBV type/load signature, initial verification
and unsupported CallBase rejection were added. Final reviews found no remaining
hard defects; numeric DXIL constants remain a nonblocking naming heuristic.

Next: expose the transformation across the native/PE boundary, regenerate and
validate containers in the real conversion path, augment/map the compiler root,
include lowering/metadata identity before cache lookup and bind the matching
descriptor-origin records. Dynamic/modern handles, arrays and GetDimensions
remain implementation gaps, not merely missing tests. GPU acceptance and full
mandatory format matrices follow this integration. No push or FL promotion.
