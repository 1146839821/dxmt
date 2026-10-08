# Task Analysis

Baseline `6391858`, branch `feat/d3d12-1`. User authorized the next restricted
offline binary-transform milestone. Preserve dirty DirectX submodule. Local
commit after self-review; no push.

Hypothesis: a fail-closed transform of finite straight-line SM6.0 compute DXIL
can inject a private origin/count CBV, guard logical typed accesses, and add
origin before MSC compilation. DXC can regenerate resource/PSV metadata and
validate the whole container without generic LLVM 15 serialization.

Evidence: prior unchanged round-trip accepts existing containers, validates
input/output flags=0, and preserves IR text; synthetic origin shaders execute
correctly but did not rewrite an existing binary.

Expected effect: demonstrate actual binary-to-binary rewriting for a bounded
typed UINT SRV/UAV load/store/atomic-add corpus, including logical OOB.
No production feature or performance effect.

Risk: arbitrary textual substitution can silently misidentify handles or alter
control flow. Use an explicitly accepted grammar, static resource metadata,
direct handle uses, one straight-line main and whitelisted instructions;
reject every unrecognised flow, operation, declaration or collision. Preserve
original executable statements except guarded typed input operations. Rebuild
containers through DXC and validate before writing a new output.

Validation: original binary input only, regenerated DXIL/full-container checks,
native MSC execution with API/GPU validation, byte-wise oracle and whole-buffer
sentinels. Include zero/short/full logical counts and near-wrap indices; reject
dynamic handles, preexisting CBVs, graphics/libraries/debug/root parts.

Scope: opt-in test module/tool and native probe. Production binding, static/
volatile integration, caches, capability bits and AIRCONV remain unchanged.

# Task Result

Implemented an actual opt-in binary-to-binary transform using DXC disassembly,
a restricted textual IR adapter, DXC assembly and full-container validation.
This is not the production/general-purpose lowering module.

## Accepted contract and implementation

The tool's `--lower-typed-origin` mode accepts SM6.0 compute with one straight-
line `main`, scalar UINT typed input at t0/u0 space0 and output u1 space0.
Finite ranges have one descriptor each; handles are direct CreateHandle with
constant range/index and nonuniform=false. Input/output metadata must match
kind=typed buffer, component=UINT and non-coherent/non-counter/non-ROV flags.

The complete module envelope allowlists known types, declarations, attributes
and named metadata. Numbered metadata is preserved, not exhaustively decoded;
the resource graph is checked explicitly and full DXC validation is mandatory.
The executable instruction grammar accepts threadId.x, ordinary i32 addition,
component-zero extraction, scalar typed load/store (mask15) and atomic add.
All other control flow, calls, handle flow, masks and observable status lanes
reject. Libraries, graphics, root/debug/unknown container parts reject at the
existing container boundary. This is a bounded DXC textual adapter, not an
LLVM pass or arbitrary shader parser.

Only unquoted unnamed SSA tokens are renamed; conflicting preexisting %vN
names reject. Each input load/store/atomic is
guarded by logical index < count and non-wrapping index + origin; successful
paths retain the typed operation, failed loads merge zero and failed writes
skip. OOB atomic return is unspecified; zero is one implementation choice and
is not a required oracle result. See Microsoft's
[atomic-add contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/imm-atomic-iadd--sm5---asm-).

No original CBV/sampler is accepted, so b0/space1 is collision-free within this
limited contract. Injected CBV metadata contains uint origin/count (8 logical
bytes); the native probe binds 16 bytes via its reflected internal root offset.
The external application root signature is neither accepted nor modified.
This does not establish collision-free mapping for arbitrary production roots.

Both input and regenerated output pass dxil.dll validator flags=0, including
HRESULT and operation status. DXC regenerates resources/PSV/reflection; the
output is not byte-identical. No HLSL compilation occurs inside the transform,
and no source-derived replacement shader is substituted for the original body.

## Validation

DXC/validator versions and DLL hashes remain as pinned in
[the round-trip milestone](D3D12_DXIL_ROUNDTRIP.md#versions-and-execution-path).
Normal/no-private tools and native probes compile. Six original binaries
transform and validate: existing UAV/SRV/atomic fixtures plus three unsigned
index-wrap fixtures compiled without origin/count or source bounds guards.

Native MSC execution uses API/GPU validation on M4. The byte-wise oracle checks
entire allocation sentinels and output values, excluding only undefined OOB
atomic returns. For each build:

| Matrix | Cases | Purpose |
| --- | ---: | --- |
| Existing binaries, logical counts 8/0/1/3/4/5/7 | 210 | Load-zero, dropped stores/atomics, aligned/padded origins |
| Original indices tid.x + UINT_MAX - 1, same counts | 210 | Logical index wrap, OOB-to-inbounds mixture, wrapped store indices |

Each matrix covers UAV/SRV/atomic, FirstElement 0/1/4/257/260, and BoundsCheck
off/on. Native origins come from queried 16-byte alignment. Index-wrap testing
does **not** independently exercise the injected origin-addition overflow arm:
valid supplied origins/counts are small. No arbitrary large-origin backing,
dynamic mapping, aliases, sparse/null resource, graphics, normalized/packed
format or broader atomic correctness is established.

The native parser unit suite additionally covers quoted percent tokens,
missing attributes, metadata before main, guarded merges, unsupported module
metadata/declarations, branches, masks/status use, dynamic/nonuniform handles,
wrong component types, CBV collisions, repeated lowering and missing anchors.
These parser checks are not substitutes for full DXIL or GPU validation.

Final rerun uses `accepted_uav/srv/atomic.cso` and `accepted_wrap_0/1/2.cso`:
all six pass full input/output validation; both native builds match all 420
cases with no observed API/GPU validation diagnostics. The two matrices each
have 126 aligned and 84 padded cases. Original, untransformed shader controls
still have 18 aligned matches and 12/12 padded mismatches per build. Original
shaders with only the hidden CBV bound fail the OOB matrix (72 aligned and all
84 padded cases), returning 1/INCONCLUSIVE, so guards are not merely cosmetic.

18 native parser checks pass, and both Meson suites pass 3/3 including this new
unit suite. Nine real-container rejection cases return 1 without creating an
output: existing-CBV, raw resource, direct-indexed profile, multi-resource/CBV
shader, root signature, debug shader, pixel shader, library and already-lowered
binary. Some reject at the profile/container boundary rather than the handle
parser; direct dynamic/nonuniform handle rejection is covered by parser units.
All reviewer findings are closed at source level; GPU evidence was rerun by
the main agent rather than the reviewers.
Original round-trip mode still passes identical IR/metadata comparison.
Input-as-output in lowering mode returns 1 and leaves the input SHA256 unchanged.
The preceding raw/typed-origin R32/R8/R16 synthetic regression matrix also
remains 100/100 per native build; that does not widen this transform's R32 MVP.

## Reproduction

```sh
meson setup --reconfigure build
meson compile -C build dxil_roundtrip dxil_origin_transform_test msc_typed_buffer_padding_probe
WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
WINESERVER=/Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wineserver \
  /Users/zhangbo/Documents/Vibe-Codeding/wine/bin/wine \
  build/tests/dx12/dxil_roundtrip.exe \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/typed_uav_1_0.cso \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/new_lowered_uav.cso \
  Z:/Users/zhangbo/Documents/Vibe-Codeding/dxmt/tools/dxc/bin/x64 --lower-typed-origin
```

Output must not exist. Transform `typed_uav_srv_1.cso` and
`msc_typed_buffer_padding.cso` similarly; pass the three rebuilt paths to the
native probe with `--origin-cbv-oob`. For `--origin-cbv-wrap`, compile the opt-in
`dxil_typed_buffer_transform_wrap_0/1/2` targets, transform those binaries, and
pass rebuilt paths to the probe. Final evidence is under ignored
`build/dxil-transform.jff3ZL/`. Set MTL_DEBUG_LAYER=1, MTL_SHADER_VALIDATION=1
and MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 before each native launch.

## Self-review and scope

Standards review found quoted-string renaming, missing insertion anchors and a
stale main offset when metadata precedes it. Fixed with quote-aware token
handling, verified anchors, re-finding main and focused unit regressions.
Spec review found insufficient whole-module rejection. Fixed with an explicit
envelope allowlist; numbered-metadata limitations are stated above.

LLVM/MSC guidance determined dialect-aware assembly and reflected private
binding; Metal validation checks were run separately from semantic oracles.
No production descriptor/runtime/shader-cache integration, static/volatile
metadata lifetime, capability promotion, AIRCONV change, game acceptance or
performance result is claimed. Production still rejects unaligned typed views.
Dirty DirectX submodule remains excluded. Commit locally, NOT PUSHED.

Next: broaden the accepted resource/handle model with explicit rejection tests
and alias coverage before designing production hidden-root/metadata integration.
