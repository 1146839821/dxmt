# DXIL MinMax compute conversion

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
