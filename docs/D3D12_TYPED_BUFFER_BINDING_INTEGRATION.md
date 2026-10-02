# Production typed-buffer binding integration

## Task Analysis

Baseline `56ce7d3`, branch `feat/d3d12-1`, clean start. User priority:
implement capability gaps first, then expand full validation matrices.

- Hypothesis: a native typed view and later `Buffer::current()` can describe
  different allocation generations; logical origin metadata must use the same
  descriptor observation point as the retained view.
- Evidence: SetMSCTypedBufferView creates from current allocation, but
  ResolveDescriptors previously fetched current allocation again later.
- Expected effect: retain the allocation used for the native view and carry
  byte offset/count/stride coherently through copy, snapshot and overwrite.
- Risk: extra allocation references live until slot overwrite/heap destruction;
  snapshot metadata is not yet the private GPU origin/count ABI.
- Validation: normal/no-private D3D12 builds, existing host regressions and
  focused descriptor ownership/residency checks. No full matrix expansion.

## Implementation

Production `MSCTypedBufferBinding` owns allocation and view, with logical byte
offset, element count and stride. Slot creation captures all fields under the
existing heap mutex, before alignment rejection. Unsupported views still have
no native view/MSC descriptor. Copy transfers the entire binding; overwrite
clears it; snapshots copy the aggregate while locked. Resource-use fan-out
uses the allocation pinned at descriptor creation, not a later renamed buffer.
Static recording versus volatile submission observation points are unchanged.

The CPU binding aggregate is not a GPU wire struct. It does not round native
origins, create a hidden root, upload GPU origin/count records or change shader
conversion. Unaligned MSC views still fail closed. DXBC/MSC routing and all
capability declarations remain unchanged.

Focused ownership validation adds aligned/unaligned availability, descriptor
copy, overwrite/reset, retained snapshots and a deliberate internal allocation
rename/re-resolution/restore. This exercises real descriptor methods, not a
mock producer, but does not dispatch a shader or prove GPU completion lifetime.
The test now links existing runtime dependencies needed to destroy owning
snapshots; the initial missing dependency link error was fixed.

## Task Result

Normal and no-private D3D12 DLLs, ownership and typed-residency fixtures built.
Meson host suites passed 3/3 in each build; `git diff --check` passed.
Fresh normal runtime ownership and typed-buffer submission residency fixtures
both passed through the existing user Wine loader with staged build DLLs.
Ownership loader trace confirms the staged `d3d12.dll` loaded as native; SHA256
`b86674c6fde64283a3c0f0f77190fd4c66f34cee4030e51a3b713827b04e7346`.
Ignored receipts: `build/typed-binding-ownership-runtime.log` and
`build/typed-binding-residency-runtime.log`. The latter preserves expected
unaligned submission rejection while reporting contract success. An earlier
dedicated cached Wine-overlay attempt timed out after 60 seconds; it is not a
passing result. No no-private runtime/provenance acceptance is claimed: its
build and host suite passed, but its matching Unix runtime was not deployed.

Independent Standards review's aggregate-snapshot suggestion was applied.
Independent Spec review's availability and allocation-change test gaps were
fixed, then re-reviewed. Final Standards: zero findings; final bounded Spec:
zero findings. Broader GPU origin/binding requirements remain incomplete.
The MSC integration skill guided reflected binding separation and coherent
descriptor ownership; no hardcoded GPU root offset was introduced.

Next: connect production DXIL lowering and internal root/metadata binding to
this coherent descriptor payload. This is an implementation dependency closed,
not completion of the typed-UAV or FL12_0 contract. Full matrices remain later.
No game DLL deployment, capability promotion or push.
