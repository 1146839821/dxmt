# Typed origin default-path gap

## Task Analysis

- Hypothesis: default selection, rather than scalar-only format support, is the
  next production gap after modern finite handle integration.
- Evidence: PreDispatch only selects the variant with an external DXC directory
  override; every selected shader is then lowered, including unrelated shaders.
- Expected effect: identify the implementation boundary before enabling it.
- Risk: deleting the switch would reject unrelated shaders and introduce an
  undeployed compiler dependency. Focused readbacks cannot qualify all formats.
- Validation: compare the same current task-owned binaries with and without the
  override, using format conversion and complete backing-buffer oracles.

## Task Result

Current commit `9f13d5c`, normal build, actual DXIL/MSC D3D12 dispatch:
R32_FLOAT, R16_FLOAT and R32G32B32A32_FLOAT each pass buffer first-element 1
and 4. These six cases check loaded component values and the complete backing
buffer after stores, not merely process completion. The same R16_FLOAT first4
case without `DXMT_TYPED_ORIGIN_DXC_DIRECTORY` fails before GPU dispatch:
`EncodeMSCResourceUses` reports the MSC typed-buffer view unavailable and Close
returns 0x80004005. This is direct evidence of a default routing gap, not a
numeric half-float failure under the origin path.

Evidence: `/Users/zhangbo/.cache/dxmt-typed-admission.6dvoo2`.
Task-owned runtime/application clones were used; no game/prefix DLL changes.
No new production code or capability changes; no fresh no-private, DXBC,
modern-format, complete-matrix or game acceptance in this checkpoint.

Next implementation: qualify variant selection by shader/binding requirements,
retain a single coherent static/live snapshot, supply an explicitly deployed
compiler/validator location, and avoid imposing typed lowering on ordinary
compute shaders. Graphics stages and root-updating indirect commands must be
accounted for before claiming complete default support. Do not simply remove
the environment check or enable TypedUAVLoadAdditionalFormats from these cases.

Self-review: source routing, selected-case GPU oracles and failure phase checked;
six focused passes remain separate from full feature acceptance. Documentation
only, no build rerun needed; diff whitespace checked. Local commit only.
