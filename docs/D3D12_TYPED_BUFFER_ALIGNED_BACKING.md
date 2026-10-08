# Production typed-buffer aligned backing

## Task Analysis

Baseline `79f42ad`, clean `feat/d3d12-1`. Continue production capability gaps,
not another offline matrix. Runtime adapter inspection shows the existing
text transform is fixture-specific (fixed registers, output u1, straight-line
main); it is not eligible as a generic game shader lowering implementation.

- Hypothesis: the descriptor binding can prepare a separate aligned view plus
  texel origin/count without exposing it to unmodified MSC shaders.
- Evidence: current production path returns before native view creation for
  unaligned offsets; native diagnostic coordinate-corrected views worked.
- Expected effect: real production descriptor construction/ownership for the
  view representation needed by origin lowering.
- Risk: stride/alignment divisibility, backing bounds, native view limits,
  accidental substitution into the legacy MSC heap.
- Validation: build both variants, narrowly extend existing ownership fixture,
  run the staged normal DLL and submission resolver. No full matrix expansion.

Ordinary descriptor entries still require exact aligned views. The alignment
rejection boundary is unchanged; new allocation-range checks also reject
out-of-bounds native view requests before creation.
The alternate view is only eligible for a future origin-aware shader path.
This is an incremental implementation prerequisite, not the final lowering,
root/cache/binding integration or a completed FL12_0 capability.

## Task Result

Both normal/no-private D3D12 DLLs and ownership/submission-residency fixtures
built successfully after reconfiguration. Host Meson suites passed 3/3 in each
variant. Final normal runtime ownership and submission-residency checks passed
with staged build DLLs; ownership loader trace shows the temporary d3d12.dll
loaded as native. DLL SHA256:
`ec351d208df6363483f7633f433d47a1c062ab9d6702eff9d7e30862dd0dcec0`.
Ignored receipts: `build/typed-backing-ownership-runtime-final.log` and
`build/typed-backing-residency-runtime-final.log`.

The ownership fixture verifies origin-view availability/address/length and
texel origin for the existing R32_UINT aligned/unaligned cases, copy/reset and
allocation rename coherence. Submission regression preserves ordinary MSC
unaligned rejection. This is native view creation/CPU ownership/resolver
validation, not transformed shader execution or GPU readback acceptance.
No no-private runtime acceptance is claimed; its matching Unix runtime was
not deployed. Earlier probes launched before build completion timed out and
were excluded; final probes ran after completed builds and stable relinking.

MSC binding guidance kept alternate descriptors separate from the ordinary
24-byte heap ABI. LLVM guidance prevented treating the fixed-register textual
adapter as a generic DXIL compiler frontend. No shader fallback, capability
promotion, prefix DLL replacement, game benchmark or push.

Standards review: no hard findings; short descriptor-construction duplication
retained as a non-blocking heuristic. Documentation now distinguishes preserved
alignment rejection from new allocation-range checks. Independent Spec review
found no implementation defect and two coverage gaps. Snapshot-only ownership
was strengthened: clear both source/destination slots, inspect all origin
fields and query the retained native texture width. Nonzero aligned-down base,
other strides, tail boundaries and native rejection limits remain unverified
matrix coverage; no broad arithmetic acceptance claim is made.

Next implementation dependency: actual origin-aware DXIL lowering with shader
resource identity mapping and internal root/metadata binding. Preparing the
view does not make that dependency complete, nor justify a FL12 promotion.
