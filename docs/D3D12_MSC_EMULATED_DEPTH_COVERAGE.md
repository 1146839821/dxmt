# MSC geometry/tessellation absent-PS coverage

Baseline 0befb46a. Scope: connect the existing specialized internal coverage
function to MSC GS and HS/DS depth-only pipelines, including private PSO wiring.
Full depth/stencil/MSAA or private-variant GPU qualification is not claimed.

Hypothesis: emulation can reuse the ordinary no-binding coverage function if
the specialized function survives the companion pipeline builder. Evidence:
GS/HS/DS mask-5 raw-depth fixtures both reject in the previous production code;
the emulation info already has a base fragment handle, but native mesh descriptor
creation ignores it and the companion overwrites it with null when no fragment
library is supplied. Expected effect: masked samples retain depth 1 while covered
samples receive raster depth 0. Risk: losing specialization, borrowing/releasing
the wrong function, private PSO rebuild loss, or silently ignoring masks on an
older native bridge. Validate exact per-sample values, contrast masks, stage
markers, old-native behavior, module provenance and API validation.

## Implementation

CopyRenderPipelineInfoToMesh now carries the fragment function. Initial
tessellation and private geometry/tessellation infos carry the PSO-retained
depth_coverage_function_. The native mesh descriptor assigns that handle before
calling the existing companion builder. No new root bindings, per-draw buffer,
compiler preparation, draw helper or argument-layout change is introduced.

The vendored companion's four Metal 3 C++/Objective-C GS/tessellation paths only
replace the base fragment function when a fragment library is explicitly supplied.
Invalid supplied libraries/functions still fail closed. With no library, the
base descriptor retains the already specialized function; it may still be null
for ordinary default-all absent-PS pipelines. Cleanup only releases functions
created by the companion, not the borrowed base function. The PSO retains the
coverage function throughout later private rebuilding.

Native optional bit 30 (EMULATION_BASE_FRAGMENT) records this implementation
seam. New PE requires it for non-default absent-PS emulation masks; old native
returns E_NOTIMPL before using the unsupported path. Existing structs, Unix
ordinals and default-all behavior remain unchanged. This runtime seam bit is
not a D3D12 capability promotion or complete GPU conformance declaration.

## Oracle and gate

The existing raw-depth fixture accepts --geometry= or paired --hull=/--domain=.
New DXIL entries pass through a triangle in GS or use three HS/DS control points
with tessellation factor 1. The latter actually binds a three-control-point patch
topology. Both paths omit the application PS, clear D32_FLOAT 4x samples to 1,
draw at 0, and read each sample through the existing float SRV/compute route.
Selected bits must be 00000000; unselected bits must stay 3f800000. Contrast PSOs
exercise different specialization values within the same process.

Fourteen additional mandatory stage/mask cases expand the raster ledger from
169 to 183. They require raw depth, exact mask and actual geometry or hull/domain
markers, explicit runtime provenance and no experimental admission or compiler
deployment. Failure cannot be hidden; all-success remains PARTIAL for full raster.

## Evidence

Evidence directory: /Users/zhangbo/.cache/dxmt-msc-logic.6jblCq.

- emulated-coverage-red.json: old production rejects both stage paths, mask 5.
- emulated-coverage-green.json: both rebuilt no-private mask-5 paths pass with
  Metal API validation and matching PE/Unix provenance.
- emulated-coverage-matrix.json: both builds, two stage paths and seven masks
  0/1/5/10/15/80000000/ffffffff pass: 28 GPU cases and 112 raw sample comparisons.
- emulated-coverage-old-native.json: hash-verified pre-change Unix image rejects
  mask 5 on both stages, but default-all raw-depth draws still pass. The deliberate
  Unix mismatch is compatibility evidence, not formal qualification PASS. Fresh
  cache runtime was restored afterward.
- emulated-coverage-build-{normal,no-private}.log: both complete builds pass;
  optional stage fixtures explicitly built separately.
- emulated-coverage-host-{normal,no-private}-final.log: both host suites pass
  17/17, including 92 gate unit tests. git diff --check passes.
- emulated-coverage-raster-regression.json: final no-private execution passes
  all 183 mandatory cases, including 21 ordinary/GS/HS-DS depth cases, with
  API validation and current PE/Unix provenance. Experimental MSAA remains zero;
  overall raster status stays PARTIAL rather than full qualification PASS.

The diagnosis/compiler/integration/validation skills guided the production
red-to-green oracle and reuse of companion/reflection machinery. Main-agent
Standards/Spec review checks specialization retention, borrowed-function ownership,
invalid-library rejection, unchanged ABI, native availability and fail-closed
gate registration. No independent review, shader validation, actual WOW64 GPU
execution, Metal 4 qualification or native Windows oracle is claimed. Cache-only
runtime deployment; no game/prefix deployment or process restart.

Remaining: actual masked Typed/MinMax private variants, broader topology/count/
format/stencil/indirect/lifetime matrices, AIR absent-PS audit and full raster/FL
qualification. Existing bounded tessellation evidence is not fresh ROTTR
performance or game tessellation acceptance. No FL/SM/LogicOp promotion.
