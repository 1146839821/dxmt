# Resource Audit Slice: Heap Mapping Compatibility

Status: Complete
Date: 2026-09-02
Branch: `feat/d3d12`
Checklist: `D3D12_RESOURCE_AUDIT_CHECKLIST.md`

## Problem

Reserved texture mapping rejected every heap carrying
`D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES`. That is correct for an ordinary
texture, but incorrect for a resource carrying
`D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET` or
`D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL`: those resources must instead reject
`D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES`.

## Implemented Contract

`MTLD3D12Texture::UpdateTileMappings` now selects the incompatible heap flag
from the resource flags:

- RT/DS texture: reject `D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES`.
- Ordinary texture: reject `D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES`.

The existing `CreatePlacedResource` validation in
`src/d3d12/d3d12_device.cpp` remains the single creation-time heap/resource
type check. Its existing `ALLOW_ONLY_BUFFERS` texture guard in
`CreatePlacedTexture` was preserved; no duplicate validation was added there.

## Changed Files

- `src/d3d12/d3d12_texture.cpp`
  - Corrected the reserved texture mapping heap compatibility branch.
- `tests/dx12/dx12_resource_tests.cpp`
  - Added an RT-only heap and reserved RT texture.
  - Added a placed RT texture success case and invalid ordinary/RT placement
    cases.
  - Added an RT reserved-texture `CopyTiles` upload/readback oracle.
  - Removed a redundant NULL remap that invalidated the deferred-resolution
    baseline before execution.

## Validation

- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed.
- `ninja -C build-X86 src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
  passed as a compile check; 32-bit runtime validation is out of scope.
- x64 resource runner passed with the current `build` DLL.
- no-private resource runner passed with the current `build-no-private` DLL.
- `git diff --check` passed.

Expected warnings in the runner include unsupported feature-level requests,
intentional invalid resource cases, and intentionally unmapped `CopyTiles`
cases. They are not failures when the final line is:

```text
D3D12 allocation and placed resource tests passed
```

## Do Not Reimplement

- Do not restore the unconditional `DENY_NON_RT_DS_TEXTURES` test in reserved
  texture mapping.
- Do not move the existing placed-resource type validation into the texture
  implementation without a new caller or a demonstrated regression.
- Do not remove the deferred-resolution test setup or add a pre-execution NULL
  remap that changes the expected mapping.
- Do not advertise tiled-resource Tier 2 or add a real sparse-residency claim.

## Follow-Up

The next candidate slice is committed-resource heap-flag compatibility. First
audit `CreateCommittedResource`, `ValidateHeapProperties`, and both committed
buffer/texture creators, then add negative tests only for behavior not already
covered by the shared device validation.
