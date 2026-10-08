# MSC absent-PS ordinary depth coverage

Baseline 2c1ae1d3. Scope: implement masked coverage for ordinary MSC vertex
pipelines without an application pixel shader. Emulated GS/HS/DS remains open.

Hypothesis: an internal fragment which only outputs sample_mask can preserve
the rasterized depth while applying the PSO mask. Evidence: the previous fragment
SampleMask path has no shader to configure when PS is absent and rejects the
new raw-depth fixture with E_NOTIMPL. Expected effect: after clearing four D32
samples to 1 and drawing at depth 0, only mask-selected samples become 0.
Risk: introducing resource/root bindings, changing depth via an output semantic,
losing the function during private PSO rebuild, or trusting an invalid depth SRV
readback. Validation: exact float bit patterns per sample, zero/interleaved/high
masks, different-mask contrast PSOs, module provenance and API validation.

## Reused infrastructure

InternalCommandLibrary already builds and owns a shared Metal library and
supports function constants. Its new fs_depth_coverage uses a named uint
function constant at index 0x106, separate from presenter constants. It has no
inputs, resource bindings, color output, depth output or early-fragment-tests
attribute. The only output is [[sample_mask]]. No DXC compiler deployment,
runtime MSL source compilation or new Unix bridge entry is needed for it.

The ordinary graphics PSO specializes the function for a non-default mask,
retains it with WMT::Reference, and supplies it as the actual fragment function.
Application PS presence/reflection/root layout remain unchanged. Default-all
absent-PS pipelines retain their previous no-fragment path. Ordinary Typed and
MinMax rebuilds fall back to the retained function when their application PS is
absent; that fallback is implemented/source-reviewed, not GPU-qualified here.
Masked absent-PS geometry/tessellation emulation still rejects explicitly, rather
than losing coverage in those separate linking/private rebuild paths.

## GPU oracle and registration

The existing MSAA fixture's --depth-only mode now performs real depth rendering:
R32_TYPELESS 4x allocation with D32_FLOAT DSV, clear to 1, absent-PS draw at 0,
transition to SRV, R32_FLOAT view and Texture2DMS<float> raw compute readback.
Each result must be exactly 00000000 (selected) or 3f800000 (unselected). No
resolve or average obscures individual samples. --contrast-mask first builds
another depth-only PSO with a different mask, then draws the requested PSO.

The paired zero/full and alternating-mask results establish that this M4 DXMT
depth view/readback path distinguishes preserved samples from actual writes;
they do not establish every adapter or depth/stencil format's view semantics.
Depth has its own optional read_depth HLSL entry and explicit Meson target.

Seven nonexperimental depth cases expand the mandatory raster ledger from 162
to 169. They require exact depth and mask markers, actual runtime provenance,
and no compiler deployment. Failures remain mandatory FAIL; success remains
PARTIAL for complete raster qualification. Gate tests check registration,
experimental exclusion, compiler scope and propagation of a depth-case failure.

## Evidence

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- depth-coverage-red.json: old production rejects mask-5 PSO, exit 1.
- depth-coverage-green.json: rebuilt mask-5 draw passes four raw depth values
  with matching PE/Unix images and Metal API validation.
- depth-coverage-matrix.json: both builds pass masks 0/1/5/10/15/80000000/ffffffff,
  fourteen cases and 56 raw comparisons; all experimental capability flags stay
  explicitly off. Contrasting PSO masks are checked in every case.
- depth-coverage-build-{normal,no-private}.log: both complete builds pass after
  reconfiguration; optional depth fixtures explicitly built separately.
- depth-coverage-host-{normal,no-private}-final.log: both host suites pass 17/17,
  including 91 gate unit tests. git diff --check passes.
- depth-coverage-raster-regression.json: all 169 mandatory no-private raster
  executions pass under API validation, including seven actual depth cases.
  Target PE/Unix provenance matches and experimental MSAA stays zero. Overall
  status remains PARTIAL; this is not a complete raster or feature-level pass.

Diagnosis, compiler/integration and validation skills guided the production
oracle, no-binding internal function and depth-view check. Main-agent
Standards/Spec review checks ownership, constant-index separation, private
ordinary fallback and remaining emulation guards. No independent review or
shader validation is claimed; no game/prefix deployment or process restart.

Remaining: GS/HS/DS masked depth coverage; actual masked Typed/MinMax variants;
stencil writes, other depth formats/counts, sample-frequency/side effects,
AIR absent-PS mask audit, indirect/lifetime and native Windows matrices.
No capability promotion, complete
MSAA/FL12 qualification or game acceptance is claimed.
