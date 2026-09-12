# D3D12 Resource Audit Slice: Display Heap Compatibility

Status: Complete
Date: 2026-09-03
Scope: `D3D12_HEAP_FLAG_ALLOW_DISPLAY` at committed-resource creation.

## Contract

Display heaps are valid only for committed resources on
`D3D12_HEAP_TYPE_DEFAULT`. Their resource description must be a non-MSAA
Texture2D with a scan-out-capable format, alignment 0, one or two array
slices, one mip level, unknown layout, and no depth-stencil or cross-adapter
flag.

Explicit heaps continue to reject `ALLOW_DISPLAY`; the flag is only valid for
the committed-resource path.

## Changed Files

- `src/d3d12/d3d12_resource_helper.cpp`
  - Enforces display-heap heap-type and resource-description compatibility in
    the existing shared validators.
- `src/d3d12/d3d12_device.cpp`
  - Requires `D3D12_FORMAT_SUPPORT1_DISPLAY` for the display format.
- `tests/dx12/dx12_resource_tests.cpp`
  - Rejects display heaps on upload heaps, buffers, oversized arrays, and
    multi-mip textures, and non-display formats.
- `docs/D3D12_RESOURCE_AUDIT_CHECKLIST.md`
  - Records this completed contract slice.

## Validation

- Private and no-private x64 resource runners.
- `git diff --check`.

## Follow-Up

Continue auditing the remaining resource-helper and `CopyTiles` paths.
