# D3D12 capability obligation gate: runtime evidence

## Scope and design

The existing gate checks selected consistency relationships among reported
feature levels, shader models, tiers and interfaces. It is not a complete
conformance suite: API behavior, all format obligations, shader execution,
resource and synchronization semantics need separate tests. This slice adds
the 11_0 baseline of shader model 5.1 and resource binding tier 1, and the
11_1 output-merger logic-op obligation. Synthetic controls ensure each new
condition is rejected when absent while logic ops remain optional at 11_0.

Microsoft's [hardware feature-level table](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-feature-levels)
lists binding tiers and logic operations; its accompanying note describes
shader model 5.1 support on hardware used through the D3D12 API. These are
reported-capability checks, not proof of implementation.

## Reproducibility and limits

`tests/dx12/run_capability_obligations.py` requires an explicitly prepared
isolated stage and prefix. It compares stage artifacts with the specified
build, accounting only for the Wine builtin marker on native D3D12/DXGI
overrides, checks actual loaded PE paths/export bytes and the Unix WineMetal
path, records hashes, and enables Metal API and shader validation. Its
converter path and file hash are observations, not proof that those file bytes
are the mapped converter identity. The runner does not modify installed Wine.

On the local Apple M1 Max, macOS 27.0.1 and Wine 11.0 x86_64, the selected
gate, artifact checks and validation setup passed. The observed maximum was
FL 11_1 and SM 6_0; binding tier 2 and logic-op support were reported, while
tiled resources, raytracing, mesh shaders and enhanced barriers were reported
unsupported. Both the 11_0/11_1 and existing 12_2 synthetic controls passed.
See `dxmt-test/n95-capability-gate/run3` for artifact identities and logs.
Other hardware, Windows reference,
the no-private-API configuration and GPU execution conformance remain
unverified. No public capability was promoted by this test.
