# Logic-op qualification checkpoint (2026-10-08)

Starting HEAD: 89937ec7. Scope: register the complete Boolean operation set in
the current raster qualification ledger, not declare full raster/format closure.

## Hypothesis / Evidence / Expected effect / Risk / Validation

Hypothesis: the existing OR-only graphics oracle can hide incorrect operation
translation and cannot establish no-private FL12_0 semantics. Evidence: the fixture
hardcodes OR, checks only RGB, and uses complementary source/destination bits;
the gate leaves mandatory_raster_matrix entirely UNVERIFIED. Production validators
explicitly reject active logic ops in no-private builds.

Expected effect: independently observe all 16 Boolean operations on both compiler
backends and expose rejection as a mandatory failure. Risk: degenerate operands,
alpha omission, false backend labeling, fixture file dependency and partial
success incorrectly promoted to full conformance. Validation: exact four-channel
CPU Boolean expectations, both build variants, actual PE/Unix provenance, host
fail-closed gate tests and ordinary packed-input regression.

## Oracle and registration

The existing graphics fixture now accepts --logic-op-0 through --logic-op-15 for
DXIL, and --logic-op-sm5-0 through --logic-op-sm5-15 for DXBC. SM5 shaders are
compiled through D3DCompile vs_5_0/ps_5_0, reusing the existing fixture compilation
helper without requiring DXIL files. DXIL uses the existing MSC pipeline; the
shader-converter skill informed verification, with no compiler configuration or
runtime binding change.

Source RGBA bytes are (240,15,170,85); destination is (204,51,15,240). Every
channel contains all four binary input combinations, distinguishing the complete
set of truth tables. The CPU oracle implements the D3D12 Boolean definitions,
independent of the driver's Metal enum translation, and checks alpha as well.
Existing timestamp/occlusion requirements remain enforced; invalid timing is
not replaced or ignored to obtain a graphics pass.

Definitions: [Microsoft D3D12_LOGIC_OP](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_logic_op).

run_logic_op_gpu_matrix executes all 32 cases sequentially using the existing
isolated, default-off capability/provenance runner. The full FL gate invokes it.
Missing runtime or inconsistent hashes remain UNVERIFIED. All-success is PARTIAL
for the mandatory raster category, never PASS; any rejection/numeric failure
remains FAIL. No-private rejection is not reclassified as expected acceptance.
Complete RTV formats/component widths, write masks/MRT/MSAA, blending,
depth/stencil/cull/scissor/sample coverage and native Windows remain explicit gaps.

## Fresh evidence

Two complete matrix runs (preliminary and final) reproduce:

- Normal: 16 DXBC + 16 DXIL exact numeric GPU readbacks PASS per run.
- No-private: all 32 fail PSO creation with E_NOTIMPL (0x80004001), before GPU
  drawing; these are rejected attempts, not 32 successful GPU executions.
- Actual PE and Unix paths/hashes match in every final case. Experimental
  SM6.6/FL12_0 and AIR MinMax opt-ins stay off; only cache runtime modules updated.
- Both targeted fixture/shader builds pass. Both host suites pass 16/16,
  including 85 gate unit tests. Shared-helper packed SM5 ordinary controls pass
  on both builds.
- Main-agent code-review Standards/Spec self-review and git diff --check pass.
  Review removed the accidental DXIL-file dependency from DXBC cases. No
  independent review, full gate run or fresh Metal validation is claimed.

Final output/provenance is retained outside Git at
`/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/logic-op-qualification-final.json`;
preliminary output at `logic-op-qualification.json`, ordinary controls at
`logic-op-packed-regression.json` in the same recoverable evidence directory.

## Remaining implementation decision

The no-private active-LogicOp path is a concrete FL12_0 implementation gap, not
an oracle issue. A public-API implementation must preserve all Boolean operations,
destination representation and ordering on both backends; merely accepting the
PSO, advertising the feature, skipping cases or enabling private API is not a fix.
This checkpoint changes no production capability or rendering implementation.
