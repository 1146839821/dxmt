# Min/Max Reduction Filtering

## Task Analysis

Branch: `feat/d3d12-1`; local starting point: `6c03bed`.
Read-only baseline: `origin/feat/d3d12`.
Existing DirectX submodule modifications are not in scope.

### Contract and evidence

D3D reduction selects the component-wise extrema of the ordinary filter's
contributing texels; texels with zero weight do not contribute.
[Microsoft D3D12 filter contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_filter)

Both existing PopulateWMTSamplerInfo overloads discard the reduction bits.
The GPU repro uses a 2x2 texture with red values 16/64/192/240 at its center:
minimum must be 16; maximum must be 240; ordinary linear average is 128.
DXIL maximum on the starting implementation returned 128 (exit 1).
DXBC minimum also returned 128 instead of 16 (exit 1), independently confirming
the shared host reduction-bit loss.

The native property is public as of macOS 26, but availability is not device
support. Apple lists sampler min/max reduction as Apple10, while this M4
is Apple9. The property is ignored when any min/mag/mip filter is nearest.
[Apple feature table](https://developer.apple.com/metal/limits/)
[Apple reductionMode](https://developer.apple.com/documentation/metal/mtlsamplerdescriptor/reductionmode)
The installed SDK's MTLSampler.h confirms the availability and ignored cases.

### Hypothesis / Evidence / Expected effect / Risk / Validation

- Hypothesis: discarding reduction bits silently implements weighted average.
- Evidence: decoded filters contain min/max; both host conversion overloads
  only populate ordinary filters; actual DXIL readback is 128, not 240.
- Expected effect: until a faithful implementation is validated, static root
  signatures reject min/max with E_NOTIMPL and dynamic descriptors are cleared
  with an explicit failure diagnostic, not left as ordinary/stale samplers.
- Risk: games relying on previously incorrect output will now fail explicitly;
  dynamic CreateSampler is void and cannot return the internal HRESULT.
- Validation: production filter-conversion matrix, static/dynamic min/max
  rejection through both shader fixture modes, ordinary GPU positive controls,
  normal/no-private builds, FL gates, diff check and two-axis self-review.

### Backend and shared-runtime scope

DXBC stays AIRCONV; DXIL stays MSC. No compiler fallback, shader ABI change,
Metal4 port, private selector or sampler native-enable path is added.
The shared D3D12 host sampler conversion reports unsupported reduction.
Metal/Wine sampler ABI and D3D11 sampler behavior stay unchanged.
Future Apple10 enablement needs independent GPU evidence, including mixed
filter modes, anisotropy, border/address modes, mip and zero-weight cases.
Apple9 requires a separately designed shader emulation path for both backends.

### Existing tests and plan

Extend the existing texture-sampler fixture rather than clone its resource,
root-signature, binding and readback setup. DXBC is compiled with D3DCompile
SM5 and uses a typed UAV because Wine's structured-buffer store frontend fails.
DXIL uses the existing DXC-generated SM6 fixture and MSC at runtime.

The semantic modes remain red/blocked until true GPU min/max is implemented.
An explicit rejection-contract mode is separate: passing that mode does not
validate GPU min/max. FL12's min/max requirement must not become PASS merely
because rejection is correct. No capability or Feature Level is promoted.

## Task Result

### Branch

`feat/d3d12-1`

### Baseline

`origin/feat/d3d12`; local starting commit `6c03bed`.

### Local Commit

The commit containing this report (see `git log -- docs/D3D12_MINMAX_FILTERING.md`).

### Changed Files

- `src/d3d12/d3d12_sampler.{hpp,cpp}`: explicit conversion result.
- `src/d3d12/d3d12_device.{hpp,cpp}`: declarations and public diagnostic.
- `src/d3d12/d3d12_root_signature.cpp`: propagate static sampler rejection.
- `src/d3d12/d3d12_descriptor_heap.cpp`: invalidate failed dynamic descriptors.
- `tests/dx12/dx12_sampler_filter.cpp`: actual production conversion matrix.
- `tests/dx12/dx12_texture_sampler.cpp`: backend fixtures and descriptor readback.
- `tests/dx12/{meson.build,dx12_fl12_gate.py,test_dx12_fl12_gate.py}`: build and gate coverage.
- This report.

### Implementation

Reject unsupported min/max reduction instead of silently creating a weighted
average sampler. Standard/comparison conversion is unchanged. Static root
signature creation returns E_NOTIMPL; the public void CreateSampler diagnoses
failure and invalidates the slot. Null native sampler allocation is guarded.

### DXBC / AIRCONV Impact

DXBC remains on AIRCONV; the SM5 fixture uses a typed UAV. No shader lowering changes.

### DXIL / MSC Impact

DXIL remains on MSC. MSC descriptor storage is cleared together with AIR storage.
No shader compiler fallback or binding ABI change.

### Shared Runtime Impact

Only D3D12 sampler conversion and its consumers change; D3D11 and Winemetal ABI
are untouched. Both shader backends receive the same explicit unsupported contract.

### Tests Added

72 static/dynamic conversion cases cover all nine ordinary filter bases across
four reduction types, including filter bits, comparison, anisotropy and output
clearing. Per runtime variant, thirteen contract cases include this matrix,
four ordinary GPU controls and eight static/dynamic min/max rejection cases.
Dynamic tests GPU-copy AIR's 32-byte and MSC's 24-byte descriptors, verify
internal rejection clears both, then re-seed through public CreateSampler,
verify nonzero handles, and verify public rejection clears every descriptor word.
Gate regressions prohibit equating rejection PASS with semantic support.

### Tests Run

Normal and no-private release builds; the 12 Python gate unit tests through
Meson in both builds; fresh Wine runtime gate reports; `git diff --check`.
No game benchmark or game-launch acceptance is claimed.

### Runtime Results

Baseline semantic tests were red: DXBC minimum 128 != 16; DXIL maximum 128 != 240.
Ordinary controls and explicit rejection are separate from semantic acceptance.
Fresh gate JSON lives in each build directory, not committed binary/log output.
Final normal and no-private reports each have build/runtime provenance PASS,
all thirteen minmax-contract cases PASS, and all existing API/container/stage
probes PASS. FL12_0_GATE and FL12_1_GATE remain FAIL (expected gate exit 1).
An intermediate no-private run overlapped a rebuild and was marked UNVERIFIED;
it was superseded by the stable-build rerun, not accepted as validation.
The normal runtime was restored with the user's sync_dlls.sh after testing.

### Self-review

Independent Standards and Spec reviews found no outstanding issue in the
fail-closed change after strengthening descriptor invalidation and filter-bit
oracles. Reviews explicitly preserve the missing full GPU semantics as a blocker.

### Known Limitations

This is not a complete implementation of specification section 15. Apple9 has
no native min/max sampler reduction; full support needs separately scoped shader
emulation in both AIRCONV and MSC. Even Apple10 native support does not establish
equivalence for mixed nearest/linear filters. Public CreateSampler is void.
The prior backend-isolation compiler-failure invocation oracle remains PARTIAL.

### Capability Status

Rejection contract: PASS after runtime verification. Full min/max semantics:
BLOCKED_BY_ARCHITECTURE on this native Apple9 path, pending shader emulation.

### Feature Level Impact

FL11_1: unchanged. FL12_0: FAIL gate, not promoted. FL12_1: FAIL gate, not promoted.
Shader Model advertisement unchanged.

### Git Status

Only this task's files are staged; existing dirty `include/native/directx` is preserved.

### Push Status

NOT PUSHED.

### Next Recommended Task

Typed UAV Mandatory Formats: audit and GPU-readback the entire mandatory set
through both backends; retain the open Min/Max emulation blocker.
