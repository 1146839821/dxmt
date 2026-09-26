# D3D12 Resource Audit Slice: Validation Matrix

Status: Complete
Date: 2026-09-03
Scope: x64 private and no-private runtime validation for the Phase B resource audit.

## Invariants

- The private and no-private builds use the same D3D12 resource contract.
- The matrix covers the available resource, capability, API, and shader fixtures.
- No advertised feature level or tiled-resource tier was changed.

## Validation

Built the following targets in both `build` and `build-no-private`:

- `dx12_resource_tests.exe`
- `dx12_interface_support.exe`
- `dx12_feature_support.exe`
- `dx12_graphics_sm6.exe`
- `dx12_compute_sm6.exe`
- `dx12_stream_output.exe`

The following runners returned status 0 in both configurations:

- `dx12_resource_tests.exe`: allocation, placed-resource, tiled-resource, and `CopyTiles` coverage.
- `dx12_interface_support.exe`: interface support contract.
- `dx12_feature_support.exe`: feature support contract.
- `dx12_graphics_sm6.exe`: base DXIL graphics readback, `0xff0000ff`.
- `dx12_compute_sm6.exe`: base SM6 no-resource dispatch.
- `dx12_stream_output.exe`: DXBC stream-output readback, 72 bytes.

The graphics and compute runners require shader binaries. The base fixtures were
generated locally with `tools/dxc/bin/x64/dxc.exe`; Meson did not expose the
optional DXC custom targets in this checkout. The generated files remain under
the ignored build directories.

## Follow-ups

- Optional shader variants remain outside this matrix when no matching fixture is
  available.
- The remaining audit work is the resource-helper and `CopyTiles` contract review.
