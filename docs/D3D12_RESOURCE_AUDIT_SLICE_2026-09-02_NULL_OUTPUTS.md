# D3D12 Resource Audit Slice: Null Outputs

Date: 2026-09-02

## Scope

This slice audits resource output-pointer handling and allocation-info outputs
when validation fails.

## Invariants

- `ID3D12Resource::GetDesc` must not dereference a null output pointer.
- `GetResourceAllocationInfo1` clears every supplied per-resource output before
  validation so invalid queries cannot leak stale allocation details.
- The aggregate allocation result remains the existing invalid sentinel
  (`SizeInBytes == UINT64_MAX`) for an invalid query.

## Changes

- Made buffer and texture `GetDesc(nullptr)` return null safely.
- Cleared `D3D12_RESOURCE_ALLOCATION_INFO1` outputs at the start of
  `GetResourceAllocationInfo1`.
- Added x64 runner coverage for both resource implementations and an invalid
  allocation query with a pre-filled per-resource output.

## Validation

- `git diff --check`
- `ninja -C build src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- `ninja -C build-no-private src/d3d12/d3d12.dll tests/dx12/dx12_resource_tests.exe`
- Isolated Wine x64 resource runner in both private and no-private environments.

## Follow-up

Continue with subresource/box range and resource alignment contracts.
