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
- **Non-scope:** Make independent compilations atomic, alter MSC caching
  identity, or change feature declarations.
- **Boundary:** `FinalizeMSCOutputBuffers` owns final validation of the three
  returned lengths. A failure returns `E_FAIL` before cache insertion.
- **Tests:** Exercise equal/smaller output, each oversized result, zero-length
  stage-in followed by nonzero output, zero metallib, and invalid entry point.
  Run the existing fragment GPU readback regression and both full builds.
- **Risk:** Repeatedly growing or internally inconsistent output eventually
  fails safely. Reflection comes from the successful final compilation.
- **Rollback:** Revert the helper, caller change and focused tests together.
- **Done:** Bounds tests, builds and the available runtime regression pass;
  unsupported scenarios are reported explicitly.

If an output grows, the caller now retries up to three fill compilations,
preserving previously allocated capacities when an unsuccessful bridge call
has not yet reported all required sizes. A successful size-query response for
an initially absent stage-in buffer is also retried with storage. Repeated
growth or a too-small result that reports no growth fails without caching.

## Validation

The focused size test passed as a Meson native test and as independently
compiled arm64 and x86_64 executables. Both configured full builds passed.
On the local Apple M1 Max, an isolated Wine 11.0 x86_64 fragment readback and
its deliberately wrong expected-output control passed with Metal API and
shader validation enabled. The run checked deployed artifact identities and
is recorded under `dxmt-test/n92-conversion-trace/msc-output-fix`. The size
change scenarios are synthetic tests of the caller's finalization logic; the
GPU run does not demonstrate a naturally varying MSC output size.

The bounded retry cases, including staggered size reports and a successful
query with no stage-in storage, passed on both architectures. The normal Wine
fragment readback and negative control also passed after the retry change;
the isolated artifacts are in `dxmt-test/n92-conversion-trace/msc-output-retry`.

The Wine trace wrapper now also returns a shorter entry-point name only on the
first reflection query, then forwards the actual name. With the pre-retry
build, the next bridge call failed with `DXMT_MSC_ERROR_OUTPUT_TOO_SMALL` and
the fragment pipeline failed. With the retry build, the same control performed
five successful MSC compilations rather than the baseline's four and passed
both exact GPU readback and the deliberately incorrect-output control under
Metal validation. The wrapper and loaded converter identities and stage/build
artifacts are checked by the runner; logs are under
`dxmt-test/n92-conversion-trace/msc-output-before-retry` and
`dxmt-test/n92-conversion-trace/msc-output-forced-growth`. This verifies a
forced entry-point size increase, not naturally variable MSC output or every
output type.

The stage-in path has a separate Wine geometry regression. A test wrapper
reports zero bytes only for the first synthesized stage-in metallib, then
reports the real 3,292-byte result. The pre-retry build rejects the second
conversion as invalid and fails PSO creation. The current build recompiles,
performs seven successful MSC compilations instead of the baseline's six,
and passes three exact 3,072-byte geometry readbacks. A deliberately wrong
first pixel fails as expected. Both Metal validation layers, deployed image
hashes, loaded PE export bytes, and Unix bridge path are checked. Evidence is
under `dxmt-test/n93-stage-in/{before,baseline,grow-verified}`. This is a
test-induced stage-in size change on one Wine x86_64 geometry configuration;
it does not demonstrate a naturally variable converter or main metallib growth.
