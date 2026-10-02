# Typed-origin native/PE boundary

## Task Analysis

Baseline `f2d7ba2`, clean worktree. Continue implementation before expanding
matrices. Hypothesis: the structured lowering needs a callable Wine boundary
before production conversion can use it. Evidence: the helper is native-only,
with no PE thunk. Expected effect: a private-module transformation that returns
IR and ordered binding records without exposing LLVM objects. Risk: PE32/PE64
layout, output publication and stale Unix dispatch tables. Validation: both
build variants and focused native boundary checks; actual Wine invocation and
production shader integration remain separate acceptance requirements.

## Contract

`DXMTMSCLowerTypedBufferOrigins` appends Unix call 196; previous entries do not
move. Fixed-width addresses and sizes use the same 64-byte parameter layout in
both dispatch tables. PE32 rejects addresses/ranges outside its address width.
Input is LLVM bitcode, bounded to 16 MiB, not a validated DXIL container.
Reserved fields must be zero. Outputs must not overlap each other, the input,
or the parameter block. Pointer accessibility remains the trusted caller's
responsibility, as with existing converter calls.

Zero output addresses/capacities query required sizes. IR is explicit-length,
not NUL terminated. Insufficient capacity returns OUTPUT_TOO_SMALL with required
sizes but without publishing either output. Rejected input clears reported sizes.
Size fields are always reset first, including invalid overlapping requests;
there is no preservation promise for forbidden buffers aliasing those fields.
Each binding is three uint32 words: resource class, register space, register.
Its ordinal selects the matching 16-byte hidden-CBV record.

No converter/PSO call site is enabled. Callers still need full DXC container
regeneration/validation, augmented compiler root, cache identity and matching
descriptor-origin binding/residency. Unsupported handles/arrays/GetDimensions
remain implementation gaps. The LLVM frontend stays separate from AIRCONV.

## Task Result

Both variants built the PE DLL, Unix library and native harness. Existing Meson
regressions passed 3/3 in each. The harness checks size queries, short IR and
binding capacities, reserved-field rejection after a query, overlapping outputs,
and successful IR/binding equivalence with the direct helper on real FLOAT and
CFG/atomic inputs. Normal/no-private CFG output is identical. Module identifiers
were removed to avoid nonsemantic entry-point-dependent output differences.

Standards review found stale PE32 size outputs on rejection; the PE thunk now
clears them before address validation. Spec review raised overlapping aliases
of the parameter size fields; the contract explicitly gives mandatory size
reset precedence for these forbidden requests. No unresolved scoped hard finding
remains. PE32 compilation/runtime and actual Wine dispatch are not verified.
No deployment, game benchmark, GPU acceptance or feature-level promotion.
