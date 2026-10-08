# Experimental D3D12 LogicOp MSAA integration

Starting HEAD 9dc30119. Scope: implement the next resource/admission gaps and
establish D3D12 raw-sample evidence; not full MSAA or FL12 qualification.

Hypothesis: the native MSC mechanism can be reused through ordinary D3D12
graphics conversion if the resource capability and single-sample admission
blockers are corrected. Evidence: the native probe passed distinct 4x samples,
but the first D3D12 normal probe failed MSAA target creation with E_INVALIDARG.
Expected effect: valid integer MSAA allocation plus a controlled experimental
MSC LogicOp path. Risk: expanding unrelated formats/resolve support, promoting
unqualified shader frequency/side-effect semantics, or accidentally enabling
the experimental path in formal qualification.

## Production changes

The shared Apple7+ format inspector was missing MSAA for scalar R32Uint/R32Sint.
[Apple's Metal feature tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
list MSAA for these scalar integer formats on Apple7 and later. Their MSAA flags
are now added separately from the common wide-integer capabilities; integer
resolve and RG32/RGBA32 capability flags are not changed. The D3D12 fixture
checks R32_UINT MSAA render/load, no resolve, and nonzero 4x quality levels.
R32_UINT creation and actual raw sample loads now pass on M4; signed-format GPU
coverage is not claimed by this fixture.

MSC multisample integer LogicOp admission can use the existing framebuffer-fetch
conversion with DXMT_EXPERIMENTAL_LOGIC_OP_MSAA=1. It is disabled by default.
Zero sample count and AIRCONV multisample lowering remain rejected; ordinary
single-sample admission is unchanged. The flag changes admission, not conversion
configuration or cache identity: the transformed bytecode/feature space and PSO
sample description already participate in their respective keys. Formal
OutputMergerLogicOp, FL/SM declarations are not promoted.

The qualification runner explicitly sets the new flag to zero, including when
the parent environment requests one. The child-environment unit test covers
this isolation alongside SM6.6/FL12_0 controls. Experimental observations are
kept separate from the mandatory raster ledger.

## Evidence

The new dx12_logic_op_msaa fixture uses an empty graphics root, a sample-ID seed
PS, an ordinary constant-source PS and a compute texture2DMS/raw root-UAV readback.
It seeds 0x12340000 + sample*0x101, executes LogicOp, reads every raw sample,
and compares against an independent sixteen-operation CPU oracle. No resolve
or averaging is involved. Resources/root objects remain retained through fence
completion and readback.

Normal's native LogicOp path and no-private's explicit experimental MSC path
each pass all sixteen operations under Metal API validation: 32 cases and 128
sample comparisons, with matched PE/Unix/compiler provenance where applicable.
The final no-private default control still rejects the MSAA logic PSO with
E_NOTIMPL and matched PE/Unix provenance. Both full builds and host suites pass
(17/17 each, 90 gate tests).

Evidence under /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq:

- msaa-d3d-normal-first.json: original resource-creation failure.
- msaa-d3d-resource-fixed.json: normal XOR after the format fix.
- msaa-d3d-no-private-before.json: pre-integration admission rejection.
- msaa-d3d-experimental-first.json: initial experimental XOR.
- msaa-d3d-matrix.json: final API-validation operation matrix.
- msaa-d3d-default-control.json: final disabled-path rejection.
- msaa-d3d-regression.json: all 162 existing mandatory raster executions pass
  with experimental MSAA explicitly disabled; overall status remains PARTIAL.

Compiler/integration skills informed reuse of the public feature-space path and
texture2DMS/root-UAV bindings. The diagnosing-bugs loop separated allocation
from admission failure. Main-agent Standards/Spec review checks default-off
isolation, no integer resolve, raw sample readback and object lifetime; no
independent reviewer or shader validation is claimed. No game/prefix DLL
deployment or Steam/Wine process restart was performed.

## Remaining implementation and qualification

Do not remove the development flag or promote capability from these cases.
SampleMask/coverage, write masks, depth/stencil, interpolation/derivatives,
pixel-frequency UAV side effects, other sample counts/formats, explicit
sample-frequency application shaders and private variants still require work.
The ordinary fixture uses a constant source with no interpolants or UAV side
effects; it cannot prove their preservation. AIRCONV MSAA is a separate gap.
Normal here means DXMT's native Metal LogicOp path, not a Windows hardware oracle.

The sample-ID seed can trigger a speculative MinMax-preparation diagnostic due
to the existing sampling-symbol-prefix classifier; it returns unsupported and
does not select a MinMax draw. The raw-sample oracles pass. This is not a clean
diagnostics/frequency qualification claim and merits classifier follow-up.
