# DXIL MinMax compute conversion

## PSO variant task analysis

- Hypothesis: an immutable PSO-owned variant can retain the augmented root, pairs and reflected locations independently of the application pipeline.
- Evidence: the compute PSO already retains original shader bytes and its explicit root for typed-origin variants; MinMax conversion now has a separate cache contract.
- Expected effect: lazy validated preparation and native Metal PSO creation, ready for subsequent descriptor/state encoding.
- Risk: stale borrowed roots, premature publication on failure, alternate DXC cache identity, accidental application PSO replacement.
- Validation: focused actual-DLL probes for heap/static samplers and RS1.0/1.1, original bytecode/root release and repeated lookup. No dispatch acceptance is implied.

## Task analysis

- Hypothesis: the validated reduction shader and reflected compiler root can use the existing MSC compute conversion without changing the application PSO.
- Evidence: shader preparation owns validated bytecode and pair bindings; root preparation reserves space 2 and resolves application locations. MSC conversion already keys bytecode and root bytes.
- Expected effect: provide a separate conversion entry point with an explicit versioned MinMax cache contract, for subsequent PSO integration.
- Risk: confusing ordinary and prepared shader cache entries, or accepting mismatched pair counts.
- Validation: build both normal and no-private configurations; inspect contract hashing and fail-closed guards. This checkpoint does not prove dispatch or enable reduction samplers.

## Result

The separate conversion entry point resolves application bindings before MSC compilation and hashes a versioned reduction contract plus ordered pairs, in addition to existing bytecode/root/compiler keys. Application pipeline identity and descriptor recording/submission behavior are unchanged.

- Normal and no-private `src/d3d12/d3d12.dll` targets built successfully after Meson reconfiguration.
- Existing Meson suite: 4/4 in each configuration (`--no-rebuild`); not coverage of this new entry point.
- Full builds stopped at the sampler-filter probe's missing `air_sampler_abi.hpp` include path. Full-build success is not claimed.
- Self-review checked declaration/call consistency, ordered pair hashing, pair-count bounds and resolver failure propagation. No GPU or game validation was performed.

Next: PSO-owned immutable variant, actual DLL preparation/creation probe, then recording/submission bindings and GPU readback. FL/SM advertisement remains unchanged.

## PSO variant result

`GetMinMaxVariant` now retains an immutable native compute pipeline, copied compiler root, ordered pairs and application binding locations. Preparation uses PSO-owned original bytecode and retained application root. The selected DXC directory must match on repeated successful lookup; failures publish no variant. The application pipeline is never replaced and command-list selection is not enabled.

Normal and no-private production DLLs plus the MinMax and typed-origin probes built successfully. Isolated actual-DLL runs in `/Users/zhangbo/.cache/dxmt-minmax-pso.T5JbcD` passed:

- One/two pairs, RS1.0/1.1, heap/ordinary-static samplers: 16 native PSO creations total.
- Null/relative DXC inputs, alternate-directory cache rejection, retained bytecode/root after caller release, unchanged ordinary pipeline handle, reflected threadgroup identity and stable repeated lookup.
- Existing typed-float PSO regression: three passes per configuration; Meson unit suite: 4/4 per configuration.

Loader logs identify staged native D3D12/DXGI and the matching isolated winemetal runtime. Staged D3D12 hashes match build output. The initial sampler-range legacy expectation was corrected: samplers have DESCRIPTORS_VOLATILE, not DATA_VOLATILE. A separate CFG fixture failed ordinary MSC PSO creation with unsupported shader feature before variant lookup; no CFG regression pass is claimed.

Self-review checked ownership, failure publication, cache directory identity, resolver flags and unchanged application pipeline. This proves preparation and native PSO creation only, not D3D12 dispatch, GPU readback, game correctness or FL12_0. Next production gap: private descriptor/state recording and submission integration.
