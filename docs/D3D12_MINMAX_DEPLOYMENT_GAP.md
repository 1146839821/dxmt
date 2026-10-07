# MinMax deployed compiler production gap

Audit baseline: 23354edf. Current working tree was clean before inspection.
This is a source-backed implementation task, not a capability promotion.

Task Analysis:

- Hypothesis: deployed compiler selection must be retained by the PSO and reused
  at command recording; changing only PSO admission leaves draws/dispatches
  rejected or accidentally on the ordinary sampler surrogate.
- Evidence: compute Initialize rejects static reduction roots when
  DXMT_MINMAX_DXC_DIRECTORY is empty; graphics Initialize does the same.
  PreDraw separately rejects requires_minmax_variant when its environment
  directory is empty. Thus existing sampling/readback primitives do not provide
  default production selection even with a deployed compiler.
- Expected effect: share validated deployment discovery with typed-origin, then
  retain the selected MinMax directory for both compute and graphics. Explicit
  override wins; missing deployment remains explicit unsupported; broken
  deployment must fail rather than silently use ordinary point surrogates.
- Risk: typed-origin/MinMax coexistence, late environment changes, DXBC routing,
  absent deployment compatibility and changing immutable variant identity.
- Validation: same binaries against baseline/current DLLs, environment unset
  with valid/missing/broken deployment, static MIN/MAX and mixed roots, compute
  and graphics numeric readbacks in both builds. Preserve backend isolation,
  rejection boundaries and static/live descriptor timing.

Current source seams:

1. `d3d12_typed_origin.cpp`: SelectTypedOriginCompilerInternal discovers
   dxmt-dxc beside the module, but returns a directory only for typed-buffer
   declarations. Calling this unchanged for a sampling-only shader cannot
   select a MinMax compiler. Extract deployment discovery without copying its
   typed-resource reflection policy or skipping compiler validation.
2. `d3d12_pipeline_compute.cpp`: static reduction-root admission reads only the
   environment; GetMinMaxVariant stores a private directory and validates cache
   identity. Recording needs an immutable selection accessible from the PSO.
3. `d3d12_pipeline_graphics.cpp`: static reduction admission similarly requires
   the environment; private minmax_dxc_directory_ already owns variant identity.
4. `d3d12_command_list.cpp`: PreDraw/PreDispatch must use retained selection when
   no override exists, and must preserve typed-origin/MinMax incompatibility
   guards. Do not relax requires_minmax_variant without selecting its binding
   and private shader path.

Task Result / self-review: this audit identifies a multi-consumer production
selection gap, not missing numerical sampling primitives. No renderer code or
capability was changed and no new runtime pass is claimed. Existing timestamp
diagnosis remains unresolved; it is not the prerequisite for implementing this
selection path. Full FL12_0/FL12_1 objective remains active.
