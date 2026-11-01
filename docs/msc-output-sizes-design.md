# MSC two-pass output sizes

## Baseline and evidence

`ConvertD3D12ShaderInternal` performs two separate MSC compilations: one to
obtain sizes and another to fill allocated buffers. The Unix bridge treats a
null output pointer with zero capacity as a size query, including for the
optional stage-in metallib. Thus, if the first pass reports zero stage-in bytes
and the second reports a nonzero size, the second call can succeed without
copying that output. The caller previously accepted it. Smaller second-pass
results also left trailing bytes in the returned vectors. No naturally
occurring MSC size change has been reproduced; these are boundary observations
from the current call chain.

## Design card

- **Goal:** Return only the bytes and entry point actually reported by the
  second compilation, or fail before publishing/caching incomplete output.
- **Scope:** Validate second-pass sizes against allocated capacities, require a
  nonempty metallib and a bounded, terminated entry point, and shrink buffers
  to the actual sizes. Do not change the public bridge's size-query contract.
- **Non-scope:** Make the two independent compilations atomic, retry a growing
  output, alter MSC caching identity, or change feature declarations.
- **Boundary:** `FinalizeMSCOutputBuffers` owns final validation of the three
  returned lengths. A failure returns `E_FAIL` before cache insertion.
- **Tests:** Exercise equal/smaller output, each oversized result, zero-length
  stage-in followed by nonzero output, zero metallib, and invalid entry point.
  Run the existing fragment GPU readback regression and both full builds.
- **Risk:** A nondeterministic converter whose second output grows will fail
  safely rather than retry. Reflection comes from the second compilation.
- **Rollback:** Revert the helper, caller change and focused tests together.
- **Done:** Bounds tests, builds and the available runtime regression pass;
  unsupported scenarios are reported explicitly.

## Validation

The focused size test passed as a Meson native test and as independently
compiled arm64 and x86_64 executables. Both configured full builds passed.
On the local Apple M1 Max, an isolated Wine 11.0 x86_64 fragment readback and
its deliberately wrong expected-output control passed with Metal API and
shader validation enabled. The run checked deployed artifact identities and
is recorded under `dxmt-test/n92-conversion-trace/msc-output-fix`. The size
change scenarios are synthetic tests of the caller's finalization logic; the
GPU run does not demonstrate a naturally varying MSC output size.
