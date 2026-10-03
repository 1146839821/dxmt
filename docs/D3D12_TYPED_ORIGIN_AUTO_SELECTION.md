# Typed origin automatic compute selection

## Task Analysis

- Hypothesis: deployed DXC plus resource reflection permits automatic typed
  compute selection without imposing origin lowering on ordinary shaders.
- Evidence: current dispatch requires an environment override; current shader
  preparation already validates and reflects complete containers.
- Expected effect: a `dxmt-dxc` directory beside D3D12 supplies the compiler;
  only typed-buffer compute shaders select the private variant automatically.
- Risk: compiler load/reflection failures, unsupported typed consumers and
  static reduction combinations; preserve explicit override and fail-closed
  qualification rather than claiming general feature support.
- Validation: both full builds, default half-float and modern array numeric
  readback, ordinary compute regression, missing deployment compatibility.

## Task Result

Compute PSO creation now discovers `dxmt-dxc/dxcompiler.dll` and `dxil.dll`
relative to the loaded D3D12 DLL, not the executable, working directory or a
repository path. DXC resource reflection selects shaders with typed buffer
SRV/UAV declarations; StructuredBuffer compute stays ordinary. Selected PSOs
retain the absolute compiler directory and use existing guarded preparation,
root/cache, static/live binding and completion-owned submission machinery.
The explicit environment override is retained. Missing deployment returns
S_FALSE and preserves the previous path; load/reflection failures with a
deployment present fail explicitly. Allocation failures return E_OUTOFMEMORY.
Unsupported root-updating indirect dispatch is rejected instead of silently
executing the unlowered shader for an automatically selected PSO.

Evidence: `/Users/zhangbo/.cache/dxmt-typed-auto.uer5zT`.

- Both reconfigured full builds and final reviewed rebuilds succeed; host suites
  pass 4/4 each. `git diff --check` passes.
- With both overrides unset, each final build passes half-float first-element4,
  modern divergent-array volatile/static complete-buffer readback, and ordinary
  StructuredBuffer root-UAV numeric readback 1234: eight GPU executions total.
- A separate normal application directory without deployed DXC passes ordinary
  compute and preserves the expected half-float recording rejection. The explicit
  selected-DXC override restores that half-float GPU readback.
- Runtime/compiler copies are task-owned. No game/prefix DLL replacement.

Standards self-review: bounded discovery/reflection helper, module/COM ownership
ordering and exception translation checked. Spec self-review: selective route,
override precedence, absence compatibility, numeric readbacks and fail-closed
indirect boundary checked. Main-agent review only; independent agents unavailable.
The MSC integration skill guided reflection and retaining the existing binding
and submission contract. No new lowering or backend fallback.

Distribution must still supply the explicitly named compiler/validator folder;
this change does not modify installers or bundle binaries. Graphics stages,
embedded-root integration, root-updating indirect and unsupported typed consumers
remain open. Complete format/lifetime matrices and selection CPU cost are not
qualified by these focused checks. No TypedUAVLoadAdditionalFormats/FL promotion,
game benchmark or tessellation acceptance.
