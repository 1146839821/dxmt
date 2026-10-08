# Focused typed-UAV semantic reproduction

## Task Analysis

Branch `feat/d3d12-1`; reference `origin/feat/d3d12`; baseline `c2cc2eb`.
Hypothesis: remaining scalar R8/R16 buffer failures depend on byte-origin
alignment, not shader compilation or texture shape. Fresh normal matrix:
DXBC 144/144 PASS; DXIL FAIL; view/SRV/policy/residency contracts PASS.
Existing min/max PASS is rejection-contract evidence, not semantic support.
Expected effect: one-case CLI preserving real CPU-seeded load/store/readback
assertions, default mandatory matrix untouched. This task improves the failing
feedback loop and confirms the blocker, not implements unaligned-view support.
Relevant files: typed-UAV fixture, existing view-alignment and min/max reports.
Ranked predictions: alignment changes verdict; lifetime changes verdict; or
aligned input still corrupts due to descriptor/shader encoding. First isolate
alignment with zero/aligned/unaligned offsets on the same format and backend.
Risk: selectors silently truncate acceptance or unsafe offsets allocate huge
buffers. Separate selected summary, reject invalid arguments before GPU setup,
bound diagnostic offsets to 4096 and allow only offset zero for textures.
Plan: `--dxil|--dxbc --case FORMAT SHAPE FIRST_ELEMENT`; fresh selected readbacks
in both builds, unchanged full matrices, min/max rejection refresh, provenance
checks/restoration and independent review. No production backend/ABI change,
no capability promotion, no new gate exemption for the known failures.

## Reproduction

After the normal build/deployment, run from
`/Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12`:

```sh
env WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
  WINESERVER=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wineserver \
  WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' DXMT_SHADER_CACHE=0 \
  /Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine \
  ./dx12_typed_uav_formats.exe --dxil --case R16_FLOAT buffer 4
```

Observed exit 1: `Close failed: 0x80004005`,
`DXIL R16_FLOAT buffer first=4 FAIL`,
`typed UAV selected case: passed=0 failed=1`.
Changing only FirstElement to 8 gives a passing readback. Changing only backend
to DXBC at offset 4 also passes. The selected failure remains a failure, not a
positive unsupported-contract mode. Existing full gate invocation is unchanged.

## Task Result

Branch/reference unchanged; local commit: this document's commit. Changed files:
typed-UAV fixture and this report only. No production runtime/ABI/compiler edits.
CLI case selection retains the existing CPU seed, typed load/store, full buffer
poison guards and readback comparisons. Independent selected summary prevents
a selected success from matching the full gate's 144/0 acceptance marker.

2026-10-02: both configurations reconfigured/built successfully; Meson 3/3 each
and existing Python gate tests 55/55 PASS. Both variants freshly ran 16 selected
cases: all eight DXBC cases pass; five DXIL cases pass and three remain red.
R16_FLOAT offsets 0/8 pass, 4 rejects; R8_UINT 0/16 pass, 4 rejects; R32_UINT 4
passes, 1 rejects. These failures occur before GPU execution at Close. Aligned
cases exercise GPU output; zero origin alone is not sufficient acceptance.
Each variant also rejected ten invalid CLI inputs with exit 2 and passed four
selected texture/4096-boundary controls. Argument checks precede the fixture's
D3D12CreateDevice, not necessarily Metal initialization during DLL startup.

Fresh full matrices: DXBC 144/144 PASS, DXIL 132 PASS / 12 FAIL; both backend
UAV/SRV view contracts each 126/126 PASS. Policy/API/submission-residency pass.
The retained view rejection contracts do not turn unsupported offsets into GPU
semantic support. Min/max refresh: all 13 rejection/control probes pass, including
four ordinary sampling readbacks; no actual min/max GPU support is claimed.
Receipts: `build/typed-focused-normal.json`,
`build-no-private/typed-focused-no-private.json`, and per-build
`typed-focused-cli.json` (ignored outputs). Before/after provenance PASS. API/GPU
validation markers present for device-backed probes; CPU-only policy/filter
probes do not create a Metal device. No failed assertion or Shader/GPU Validation
Error markers found. Normal DLLs restored and fresh normal provenance PASS.

### Diagnosis / Remaining fix

The prediction from hypothesis 1 holds: changing byte-origin alignment alone
changes the outcome. `SetMSCTypedBufferView` queries native format alignment and
refuses unaligned origins. The descriptor metadata cannot substitute arbitrary
FirstElement for MSC typed texture accesses. Existing copied/late-update controls
pass; aligned selected GPU results also pass, narrowing the current failure away
from ordinary format encoding or a general lifetime defect. This does not prove
every lifetime/shader issue absent. No rounding, illegal native offset, detached
copy buffer, generated-AIR patch or cross-backend fallback was introduced.
R32_UINT offset 1 also reproduces the limitation despite existing baseline API
load reporting; API-policy PASS is not complete baseline GPU correctness.

### Standards

Independent review against c2cc2eb found no hard violations or concrete bugs.
It checked argc short-circuit safety, full numeric consumption, allocation bounds
and unchanged default matrix counts. No actionable heuristic findings.

### Spec

Independent review found no missing/wrong implementation or scope creep. Runtime
work pending at review time was subsequently completed and verified by the main
agent above. Diagnosing-bugs skill drove the red-capable selector and controlled
alignment comparisons; MSC integration/Metal validation skills kept ABI/residency
requirements separate from readback evidence. Code-review skill separated axes.

### Capability / Git / Next

Typed-UAV semantic gate remains FAIL; min/max semantic support remains unverified/
blocked on this path. No capability or feature-level promotion. Only the two task
files included, NOT PUSHED. Next: audit eligibility of the existing bounded DXIL
typed-origin lowering for an alias-coherent production solution; keep unsupported
formats/instructions fail-closed and use this red case before any enablement.
