# D3D12 Resource Audit Slice: Texture Layouts

Date: 2026-09-02

## Scope

This slice audits texture layout values accepted by the D3D12 resource creation
and allocation-info paths.

## Invariants

- `D3D12_TEXTURE_LAYOUT_UNKNOWN` is the only texture layout accepted by this
  Metal 3 implementation.
- Row-major textures require cross-adapter support and a shared cross-adapter
  heap on D3D12. DXMT reports `CrossAdapterRowMajorTextureSupported` as false
  and does not implement shared cross-adapter heaps.
- Undefined 64KB swizzle requires tiled-resource support and reserved-resource
  mapping. DXMT reports tiled resources as unsupported.
- Standard 64KB swizzle requires `StandardSwizzle64KBSupported`, which DXMT
  reports as false.
- Unsupported texture layouts are rejected with `E_INVALIDARG` before Metal
  texture creation and from resource allocation-info queries.

## Changes

- Added `ValidateTextureResourceLayout` to the shared D3D12 resource helpers.
- Applied the check to committed, placed, reserved, and allocation-info texture
  paths through the existing validation and texture-info seams.
- Added x64 runner coverage for row-major, undefined-swizzle, and
  standard-swizzle texture descriptions and allocation-info rejection.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue with texture format capability, null, range, and alignment contracts.
Do not advertise row-major, standard-swizzle, tiled-resource, or sparse
residency support to make a resource test pass.
