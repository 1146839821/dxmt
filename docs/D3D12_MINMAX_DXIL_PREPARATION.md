# DXIL MinMax shader preparation boundary

## Task Analysis

- Hypothesis: expose the existing structural pair lowering through an optional
  fixed-width Wine/Unix boundary and reuse validated DXC container preparation.
  This removes the offline-only compiler gap without prematurely enabling PSOs.
- Evidence: LowerReductionSamplerBindings currently runs only in native probes;
  typed-origin preparation already validates input/output and publishes atomically.
- Expected effect: owned validated shader bytes and pair identities available to
  D3D12 compiler-root and submission integration, with no backend fallback.
- Risk: PE32 pointer truncation, output overlap, old DXC unnamed SSA numbering,
  unavailable exports and accidental artifact publication on failure.
- Validation: both builds, actual isolated Wine export/preparation round trips,
  invalid-input/output preservation and existing typed-origin regression.
  Full runtime/root/PSO and GPU admission remain a separate unfinished contract.

## Implementation

`DXMTMSCLowerReductionSamplers` is a separate optional export with Unix slot 197,
not an overload of typed-origin semantics. Its 64-byte fixed-width request is
shared by PE32/PE64/Unix. The returned 16-byte records name sampled application
texture/sampler pairs; the 32-byte runtime state ABI is unchanged. Both normal
and WOW64 dispatch arrays append the new operation without shifting old slots.

The native boundary validates ranges and rejects overlapping input/output and
parameter aliases. Parameter aliases reject before counts or ret can mutate the
aliased bytes. Sizing returns explicit IR length and pair count; insufficient
capacity reports required sizes without publishing either payload. It parses a
private LLVM module, uses existing pair qualification/lowering and explicitly
names unnamed SSA values/blocks for the selected older DXC assembler.

`PrepareD3D12MinMaxShader` shares the existing DXC selected-directory pipeline:
validate input, inspect the supported SM6.0 compute envelope, lower bitcode,
assemble, validate output, inspect again, then move the owned candidate artifact
into the caller. Operation traits bind export name, parameter type and artifact
type. Missing optional exports return E_NOTIMPL, without invoking a missing
Wine import in production. The standalone focused boundary probe additionally
imports the new export and therefore requires the matching staged runtime.

## Validation (2026-10-03)

Normal and no-private production D3D12/Winemetal DLLs and both preparation probes
build successfully. Both Meson suites pass 4/4. PE32 thunk compilation produces
an i386 COFF object with compile-time request size/offset checks; this is not a
PE32 Wine execution result.

Actual PE64 Wine calls in fresh isolated runtime layouts pass in both variants:

| Fixture | Validated input/output bytes | Records |
| --- | --- | --- |
| MinMax one pair | 3464 / 13008 | 1 |
| MinMax two pairs in a loop | 3764 / 22704 | 2 |
| Existing typed-origin float regression | 3168 / 4008 | 2 |
| Existing typed-origin branch/loop/atomic regression | 3600 / 4652 | 2 |

MinMax probes check exact pair identities, complete prepared artifact preservation
after malformed-container and relative-directory rejection, undersized IR and
binding capacities, output/output and input/output overlap, parameter/output and
parameter/input aliases, malformed bitcode, address overflow, reserved fields and
null requests. Alias probes compare the entire parameter/input bytes. These are
focused checks, not a complete hostile-input or PE32 pointer matrix. Missing
optional export and injected assembly/validator failures have not been rerun.

Both build variants produce byte-identical MinMax containers. The final containers
also match the earlier current-task outputs compiled by MSC 4.0.1 (Apple9,
textureMinLODClamp). Reflection-driven native GPU readback passes fifteen
single-pair and fifteen two-pair shared-resource state scenarios on the normal
probe. This verifies the artifact from the real Wine preparation path, not a
D3D12 augmented-root PSO dispatch.

Receipts: `/Users/zhangbo/.cache/dxmt-minmax-preparation.MwaZom`, including
`{normal,no-private}-{one,two}-alias.log`, typed regression logs and `one.gpu.log` /
`two.gpu.log`. Loader receipts identify the new isolated native winemetal.so and
copied ntdll; staged native SHA-256 matches each corresponding build. Existing
installed Wine/game DLLs were not replaced or restarted.

## Standards

Independent source review found no documented violations and three advisory
smells: transport duplication, SSA naming duplication, and independently selected
export/type. The last is resolved with operation traits. Native transport/naming
consolidation is deferred; this task does not alter typed-origin native behavior.

## Spec

Independent review found a parameter-alias atomicity defect, incomplete focused
failure coverage and missing runtime receipts. Alias preflight and corresponding
no-mutation checks now precede all writes; additional boundary negatives and
current-build Wine/typed regression receipts address the scoped evidence gaps.
The test message was narrowed to focused checks. Broader failure injection and
automated full matrices remain unverified, not inferred from the Meson host suite.
Follow-up source review found no remaining concrete correctness defect and
requested a bindings/ret alias probe; that complete-parameter preservation check
is included. Runtime receipts are main-agent evidence, not subagent validation.

## Remaining closure

The next production gap is augmented/reflected compiler-root preparation and
mapping pair identities to app slots/static samplers, followed by PSO selection
and recording/submission state ownership. Static slots must never become pending
live reads; volatile unique slots resolve/retain under lock and fan out unlocked.
This helper is not yet called from an enabled MinMax PSO path. Ordinary shader
routing, resource descriptor strides and FL/SM declarations are unchanged. No
game/tessellation benchmark, full GPU matrix, default enablement or push.
