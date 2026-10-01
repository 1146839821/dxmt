# MSC typed-buffer view offsets

## Task Analysis

Branch: `feat/d3d12-1`. Baseline: `9f84f84`. Preserve unrelated DirectX
submodule changes; self-review and commit locally, without pushing.

- Hypothesis: MSC's typed texture accesses start at the native view origin;
  the whole-buffer view cannot represent D3D FirstElement through metadata.
- Evidence: fresh `--dxil --buffer-only` reproduces 18 PASS / 36 FAIL;
  descriptor GPU VA/metadata contains the requested offset, but AIR accesses
  use index + zero. M4's queried texture-buffer alignment is 16 bytes for
  R8/R16/R32/RGBA32 integer formats.
- Expected effect: exact-range native views fix aligned offsets without changing
  AIRCONV's full-buffer texture and element-offset ABI. Unaligned MSC views
  must fail explicitly instead of issuing invalid Metal offsets or reading poison.
- Risk: descriptor copy/overwrite and static/volatile residency require retaining
  the view independently; CPU-only descriptors must also carry the view. Alignment
  varies by device/format and must be queried. Missing padding support remains
  a partial capability, including offsets not exercised by the original matrix.
- Validation: retain the red GPU matrix; add aligned, unaligned, descriptor-copy,
  overwrite/lifetime and prefix/suffix checks; normal/no-private build and gates.
  Do not raise capability bits or feature levels.

## Binding contract

The installed MSC companion header defines 24-byte descriptors with a texture
view handle and alignment-padding metadata. It is not an arbitrary full-buffer
FirstElement field. Views returned by `newTexture` are owned references; snapshot
copies retain them under the descriptor lock and encoder references retain them
through GPU completion. No generated-AIR rewrite or backend fallback is permitted.

The native alignment requirement is queried per device/format, rather than
assuming this test device's 16-byte value. See [Apple's texture-buffer alignment
contract](https://developer.apple.com/documentation/metal/mtldevice/minimumtexturebufferalignment(for:)).

## Task Result

- Branch: `feat/d3d12-1`.
- Baseline: local `9f84f84`; comparison branch `origin/feat/d3d12` is read-only.
- Local Commit: the commit containing this report (`git log -1 -- docs/D3D12_MSC_TYPED_BUFFER_VIEWS.md`).
- Changed Files: D3D12 descriptor heap/snapshot, pending-use encoder record and command-list residency;
  WineMetal alignment query/header/thunks and native dispatch arrays;
  typed-UAV fixture/HLSL/Meson targets; FL12 gate/unit tests; this report.
- Implementation: descriptor-owned native texture-buffer views use the exact
  FirstElement byte offset and NumElements width, with zero padding metadata.
  CPU-only and shader-visible descriptors both own views; copies retain them.
  Snapshots retain under the heap lock; encoder fan-out and retention remain
  outside that lock. Recording and volatile submission declare the view and its
  parent buffer resident. Self-copy does not release its own native view.
- DXBC / AIRCONV Impact: original full-buffer resource ID and element-offset
  encoding unchanged; no shader compiler or shared Buffer view-cache changes.
- DXIL / MSC Impact: aligned views now address the requested buffer range.
  Unaligned, zero-length or failed native views cannot silently dispatch;
  static 1.1 recording fails (`Close=E_FAIL`), while volatile descriptors may be
  unavailable during recording and are validated by submission translation.
  No alternate shader backend is used. The shared residency traversal carries
  its backend identity explicitly; AIRCONV retains its original whole-buffer
  texture, does not require a native MSC view, and is not subject to MSC rejection.
- Shared Runtime Impact: append Unix-call selector 195 for the public native
  minimum texture-buffer alignment query; existing selector values and the
  24-byte MSC descriptor ABI remain unchanged. Both native and WOW64 dispatch
  arrays include the query. PE DLLs and Unix library must be deployed together.
- Tests Added: 126 UAV and 126 SRV view-contract cases per backend. Cases cover
  aligned offsets 16/272, offset-1 behavior, CPU-only descriptor copy, source
  overwrite/destruction, self-copy, and volatile updates from offset zero to
  the desired view after Close, including initially unavailable FirstElement=1
  views. Static 1.1 cases and volatile RS1.0 cases use separate root signatures.
  Buffer readback verifies all prefix/suffix
  poison bytes remain unchanged, not just the expected typed store destination.
  The original 144-case semantic matrix remains the mandatory acceptance test.
  A gate unit test requires all view-contract probes even if the matrix passes.
  A separate CPU translation-seam fixture calls the production submission
  resolver: final unaligned MSC views reject, AIRCONV views remain accepted,
  mixed-backend uses preserve requirements in either order, and static uses
  with no pending entries do not reread. This is not GPU submission acceptance.
- Tests Run: normal/no-private release builds and Meson tests; 16 gate unit
  tests; two-backend format/view gates; Metal API and shader validation;
  existing descriptor/view ownership fixture.
- Capability Status: PARTIAL; `TypedUAVLoadAdditionalFormats=FALSE` unchanged.
- Feature Level Impact: none; FL12_0/FL12_1 gates remain FAIL.
- Known Limitations: current MSC typed texture accesses do not apply the
  descriptor alignment-padding field. Arbitrary D3D typed-buffer offsets,
  including R32 FirstElement=1, are therefore still unsupported. Passing
  rejection contracts is not equivalent to supporting those views. The original
  matrix's R32 offsets 0/4/260 pass but do not prove complete baseline semantics.
  Zero-element views and native allocation failures are also fail-closed.
  No game, performance or tessellation acceptance is claimed. The added native
  view per descriptor may add creation cost; no performance claim is made.
- Git Status: only task files committed; pre-existing DirectX submodule edits
  preserved. Ignored build reports/shader binaries are not staged.
- Push Status: NOT PUSHED.
- Next Recommended Task: obtain a validated MSC path for alignment padding or
  an equivalent, alias-coherent implementation before completing typed-buffer
  capability. Never use an illegal Metal offset, round to another view origin,
  or introduce hidden DXIL-to-AIRCONV fallback.

## Runtime results

Normal and no-private builds: provenance PASS; original DXBC matrix 144 PASS / 0 FAIL;
MSC matrix 132 PASS / 12 FAIL, versus baseline 108 PASS / 36 FAIL.
The remaining 12 failures are explicit static recording rejections for scalar
R8/R16 formats at FirstElement 4/260, not incorrect GPU reads.
Both backends pass 126 UAV and 126 SRV view contracts. Submission residency
CPU seam and API/policy contracts pass. Metal API and GPU Validation activation
is confirmed in the fresh logs; no Metal validation error was observed.
Reports are ignored build artifacts, not committed acceptance substitutes.
Both Meson suites pass, including 16 Python gate unit tests. Normal descriptor/
view ownership regression also passes. The normal PE/Unix runtime was restored
after no-private validation. FL12_0 and FL12_1 remain FAIL in both fresh reports.

## Self-review

### Standards

Resolved the shared AIRCONV residency regression discovered during review.
No remaining hard standards breach identified. Deferred P3 maintainability
feedback: exact-range texture configuration duplicates full-buffer configuration
in the Buffer owner. Sharing that configuration should be a separate scoped
refactor with D3D11 regression coverage, not an incidental change in this repair.

### Spec

Resolved premature volatile recording rejection, backend isolation, and missing
final-invalid-descriptor resolver coverage. Static RS1.1 recording and volatile
RS1.0 late updates are independently exercised. The resolver fixture is a CPU
translation seam, not a real rejected GPU submission. Section 16 remains PARTIAL:
arbitrary unaligned views are unsupported and capability promotion is blocked.

## Reproduction

After building and synchronizing the matching PE DLLs and Unix library with
`sync_dlls.sh`, use the explicit game-Wine prefix/runtime:

```sh
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
WINESERVER=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wineserver \
MVK_CONFIG_LOG_LEVEL=0 PYTHONDONTWRITEBYTECODE=1 \
python3 tests/dx12/dx12_fl12_gate.py --build-dir build --variant normal \
  --wine /Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine \
  --output build/fl12-gate.json
```

For no-private, synchronize `DXMT_BUILD_DIR` pointing to `build-no-private`,
and select that build directory and `--variant no-private`. Restore the normal
runtime afterward. DXIL fixtures are compiled with the repository's DXC and the
eight type macros in Meson, never substituted with DXBC. View-contract rejection
expectations are scoped to the observed Apple alignment; test results must not
be generalized to an untested device/alignment.
