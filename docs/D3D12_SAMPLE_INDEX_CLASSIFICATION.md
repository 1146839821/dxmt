# DXIL SampleIndex classification repair

Baseline: 022dd307. Scope: eliminate false texture-sampling classification and
speculative MinMax preparation for a pixel shader using only SV_SampleIndex.
This is not SampleMask, invocation-frequency or complete MSAA qualification.

Hypothesis: the dx.op.sample prefix also matches dx.op.sampleIndex.i32.
Evidence: a new test-linked production ClassifyD3D12Shader probe, before the
production change, returned `texture_sampling observed=1 expected=0 FAIL` for
logic_msaa_d3d12_seed.cso, with exit code 1. The fixture only computes a uint
from SV_SampleIndex. Expected effect: avoid useless MinMax variant preparation.
Risk: excluding all sampling when SampleIndex and real sampling coexist, or
adding repeated bitcode scans to shader classification. Validation: compare
SampleIndex-only, constant-only, SampleLevel and live SampleLevel+SampleIndex
classification, then rerun original MSAA and actual MinMax composition draws.

## Implementation

DXILBitcodeReader's existing recursive symbol scan accepts an optional excluded
prefix. Only the current candidate symbol is excluded; traversal continues so
another real sampler symbol can still match. The texture-sampling call excludes
dx.op.sampleIndex. (including the overload delimiter) from dx.op.sample. Other
symbol and derivative queries retain the default empty exclusion. No second
bitcode pass, capability promotion, compiler ABI or runtime binding change.

dx12_backend_failure has classify-no-sampling/classify-sampling modes which call
the real production classifier without creating a D3D12 device or invoking its
wrapped compiler fault hooks. Shader validation HRESULT and MSC backend are
also checked. The mixed fixture keeps both operands live through red XOR sample;
it would catch a shader-wide `!HasValueSymbolPrefix(sampleIndex)` workaround.

## Reproduction

After reconfiguring Meson, explicitly build dx12_backend_failure.exe and
graphics_logic_op_minmax_sample_index.ps.cso; optional shader targets are not
part of the default build. Invoke the executable through the cache Wine runtime:

```text
dx12_backend_failure.exe classify-no-sampling logic_msaa_d3d12_seed.cso
dx12_backend_failure.exe classify-no-sampling logic_msaa_d3d12_source.cso
dx12_backend_failure.exe classify-sampling graphics_logic_op_minmax.ps.cso
dx12_backend_failure.exe classify-sampling graphics_logic_op_minmax_sample_index.ps.cso
```

All four classifications pass on both builds. This test-linked source evidence
is intentionally separate from DLL/Unix/GPU provenance evidence.

## Current evidence

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- sample-index-classification.json: eight classification controls, executable
  and shader hashes, exact return codes and outputs.
- sample-index-msaa-matrix.json: both builds, sixteen operations each, 128 raw
  sample comparisons, Metal API validation and target PE/Unix provenance.
  No case contains the original MinMax graphics preparation failure diagnostic.
  No-private uses explicitly labelled experimental admission; normal is DXMT's
  native Metal LogicOp path, not a Windows hardware reference.
- sample-index-raster-regression.json: all 162 no-private raster executions
  pass under API validation, including real Typed/MinMax sampler composition;
  experimental MSAA remains explicitly zero. Overall status stays PARTIAL.
- sample-index-default-control.json: default no-private MSAA still returns
  E_NOTIMPL at logic PSO creation, with matched PE/Unix evidence and no MinMax
  preparation failure. Its nonzero exit is expected rejection, not GPU PASS.
- sample-index-full-{normal,no-private}.log: both complete builds pass after
  reconfiguration; optional production-linked classifier probes built separately.
- sample-index-host-{normal,no-private}.log: both host suites pass 17/17,
  including 90 gate unit tests. git diff --check passes.

The diagnosing-bugs skill supplied the red-to-green production seam; compiler
and validation skills informed the original runtime regression. Main-agent
Standards/Spec self-review checks single-pass traversal, exclusion propagation,
mixed-symbol preservation and unchanged experimental isolation. No independent
review or shader validation is claimed. No game/prefix DLL deployment or process
restart. Complete raster qualification and MSC SampleMask propagation stay open.
