# Task Analysis

Baseline 1e67623. Remove instruction-clamp rejection for the supported AIR
SampleGrad and pixel Sample/SampleBias operations using the existing lowering.

- Hypothesis: the prior merged constraint operand already implements max of
  resource/view-space instruction clamp, after sampler clamps. Admission and
  reflection are the remaining barriers for these supported operations.
- Evidence: all three operations pass their merged MinLODClamp to the shared
  clamped reduction wrapper; resource-clamp dispatches and default vectors pass.
- Expected effect: actual instruction-clamped reduction dispatches, without
  changing helper ABI, descriptor generation lifetime or backend routing.
- Risk: stale shader eligibility/cache, formerly unsupported test fixture,
  compiler removal of zero-clamp operands, implicit quad derivatives and view
  origins (instruction clamp is already view-relative).
- Validation: nonzero SampleGrad clamp defaults/boundaries and merged constraint
  readbacks, pixel implicit clamps, real remaining unsupported-consumer probes,
  both production builds, focused regressions and two-axis self-review.

Feedback, cube, anisotropic and DXIL reduction stay rejected. No FL promotion.

Diagnosis update before decoder repair: `--minimum-instruction-r` failed with
submission rejection, and the zero-result RGBA probe falsely matched cleared
readback memory after rejected submission. A new production-PSO qualification
assertion makes even `--minimum-instruction-rgba` fail deterministically before
dispatch (`instruction-red-preflight.log`, exit 1). Decoder inspection confirms
clamp opcodes create an optional feedback operand even when its destination is
NULL. Normalize NULL feedback to absence for the three clamp-capable operations;
keep actual status destinations present/rejected. The unsupported fixture now
uses a real, consumed feedback status. Also repair pixel upload mip enumeration
and give SampleBias a nonzero losing resource clamp. This is a diagnosis-driven
implementation correction, not a relaxed acceptance expectation.

# Task Result

Supported AIR SampleGrad and pixel Sample/SampleBias now admit instruction
clamps. CFG eligibility and lowering agree, with meaningful feedback still
excluded. The decoder treats NULL feedback destinations as absent for the three
clamp opcodes; consumed status destinations remain present. Existing merged
view-space constraints, empty-set defaults and final min/mag classification are
reused. Implicit quad operations stay outside sampler-state divergence, including
the existing speculative ordinary sample; no claim that dynamic implicit empty
sampling performs no texture operation at all. Descriptor ABI, MSC path, opt-in
defaults and feature declarations are unchanged. AIRCONV_VERSION is 29.

Both production builds complete. Each passes 12 actual SampleGrad clamp GPU
cases and 8 pixel clamp GPU cases (40 case executions across builds, not a full
matrix). Explicit-gradient probes cover complete RGBA/R8/swizzled defaults,
fractional/integer mip boundaries, resource-wins and shader-wins with distinct
outputs, constraints above sampler MaxLOD, nonzero MostDetailedMip and root
static samplers. Pixel probes cover Sample clamp, SampleBias resource-wins and
shader-wins with nonzero losing constraints, and Sample empty-set output, each
with static/dynamic samplers. Pixel results compare full RGBA, including zero
alpha in empty-set cases.

The first expected-zero RGBA execution was a false positive from a rejected
submission; it is not counted as acceptance. Qualification preflight reproduced
it as a deterministic failure before the decoder repair. All new clamp probes
now check production PSO eligibility; compute probes initialize output to a
nonzero GPU-copy sentinel, so skipped writes do not match expected zero. Accepted
execution logs were also checked for submission rejection. Pixel fixtures use
one SampleMipCount source for allocation and all-subresource copying. No temporary
production instrumentation remains.

Eleven regression invocations per build pass, covering real consumed-feedback
static PSO/dynamic resolver rejection, volatile null and recorded static null,
resource/view clamps, same-PSO descriptor switching, 1D gradients, three existing
implicit/ordinary pixel controls, ordinary MSC swizzle/copy and default-off root
rejection. Negative checks are not GPU dispatches. Both native AIR linkage suites
pass 6/6; both Meson suites pass 4/4. Matching isolated host/Unix binaries, cache
disabled and dyld provenance verified. Logs: `instruction-*.log` in the isolated
runtime and no-private directory; `instruction-red-preflight.log` preserves the
red qualification repro. No game DLL deployment or benchmark.

code-review initial Spec review found the incomplete pixel mip upload and weak
SampleBias merge coverage; both were corrected. Standards noted a nonblocking
legacy numeric-mode maintenance concern; mip-count decisions are now centralized,
without an unrelated full fixture redesign. Independent follow-up Standards and
Spec reviews found no new actionable issue. llvm guided call-chain/eligibility
review, and diagnosing-bugs guided the red-capable NULL-status investigation and
anti-false-zero regression. Main-agent final self-review and git diff --check pass.

Full MinMax remains open: cube/aniso, meaningful feedback and DXIL implementation,
all shape/format/numeric boundaries, unknown-format GPU admission and the complete
matrices are not closed by this checkpoint. Continue missing production behavior
before broad reruns; typed UAV, tiled Tier 2 and the other FL12_0 gaps stay active.
