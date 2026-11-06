# D3D12 Resource Audit Slice: Texture Formats

Date: 2026-09-02

## Scope

This slice audits the texture format boundary shared by resource creation and
resource allocation-info queries.

## Invariants

- `DXGI_FORMAT_UNKNOWN` is reserved for buffers and is invalid for textures.
- A texture format must be recognized by `MTLQueryDXGIFormat` before DXMT
  creates a Metal texture or reports allocation information.
- DXMT's current Metal 3 format mapping does not implement the YUV and other
  formats that `MTLQueryDXGIFormat` rejects.
- Invalid or unsupported texture formats return `E_INVALIDARG` from resource
  creation and produce `UINT64_MAX` allocation size from allocation-info queries.

## Changes

- Added an explicit unknown-format rejection to the shared texture descriptor
  validation.
- Normalized unsupported Metal format mappings to `E_INVALIDARG` in the texture
  setup path.
- Added x64 runner coverage for unknown and unsupported texture formats in both
  resource creation and allocation-info queries.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Format capability combinations for render-target, depth-stencil, and UAV usage
are covered by `D3D12_RESOURCE_AUDIT_SLICE_2026-09-03_FORMAT_CAPABILITIES.md`.
Do not change the advertised typed-UAV capability as part of either slice.
