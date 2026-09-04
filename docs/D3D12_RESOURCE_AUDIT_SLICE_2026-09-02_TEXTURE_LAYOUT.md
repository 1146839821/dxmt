# D3D12 Resource Audit Slice: Texture Layouts

Date: 2026-09-02

The reserved-texture portion of this baseline was refined by
`D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_RESERVED_TEXTURE_LAYOUT.md`.

## Scope

This slice audits texture layout values accepted by the D3D12 resource creation
and allocation-info paths.

## Invariants

- `D3D12_TEXTURE_LAYOUT_UNKNOWN` remains the only layout accepted by ordinary
  committed, placed, and allocation-info texture paths.
- Reserved textures use a dedicated boundary validator and accept only
  `D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE`; their physical Metal texture
  construction normalizes that layout to the internal `UNKNOWN` representation.
- Row-major textures require cross-adapter support and a shared cross-adapter
  heap on D3D12. DXMT reports `CrossAdapterRowMajorTextureSupported` as false
  and does not implement shared cross-adapter heaps.
- The undefined 64KB swizzle reserved-texture path is a bounded logical
  shadow-mapping model. DXMT still reports tiled resources as unsupported.
- Standard 64KB swizzle requires `StandardSwizzle64KBSupported`, which DXMT
  reports as false.
- Unsupported texture layouts are rejected with `E_INVALIDARG` before Metal
  texture creation and from resource allocation-info queries.

## Changes

- Added `ValidateTextureResourceLayout` to the shared D3D12 resource helpers.
- Applied the ordinary layout check to committed, placed, and allocation-info
  texture paths.
- Added a reserved-texture layout check at the reserved-resource boundary.
- Added x64 runner coverage for row-major, undefined-swizzle, and
  standard-swizzle texture descriptions, allocation-info rejection, and the
  reserved-texture layout boundary.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue with texture format capability, null, range, and alignment contracts.
Do not advertise row-major, standard-swizzle, tiled-resource, or sparse
residency support to make a resource test pass.
