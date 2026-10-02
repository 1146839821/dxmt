# DXIL MinMax SampleGrad implementation

## Task Analysis

- Hypothesis: the existing SampleLevel footprint can serve SampleGrad once a
  view-relative isotropic LOD is emitted and the original sampler bias is
  transported separately from the unbiased private native samplers.
- Evidence: AIR's CreateIsotropicGradientLOD already implements normalized
  Gram-matrix major-axis selection and the parallel/zero-gradient caveat.
  DXIL binding currently qualifies SampleLevel only; its ordinary and point
  samplers deliberately have zero bias. The 32-byte state has a reserved word.
- Expected effect: reuse the AIR LOD algorithm and existing conditional
  reduction taps rather than introduce another sampling footprint.
- Risk: SampleLevel must continue ignoring sampler bias. Instruction clamps
  must be applied after sampler clamps, and both ordinary and reduction
  branches must share the original view dimensions. A compiler-only helper
  does not establish production dispatch admission.
- Validation: first compile and verify the generated DXIL IR; then connect
  bias/state and both branches, regenerate fully validated DXIL, compile with
  MSC and run distinguishable multi-mip GPU readbacks in both builds.

Opcode and SampleGrad signature reference:
https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst

This task is incomplete until binding integration and numeric runtime
acceptance pass. No capability or FL promotion is authorized by preparation.

## Gradient emitter checkpoint

Implemented CreateReductionGradientLOD2D, including declaration/signature
qualification before mutation, original-view dimension queries, normalized
major-axis calculation and the AIR degenerate-gradient rules. The native IR
probe verifies malformed-opcode rejection without mutation, valid-module
generation, expected dimension/unary operations and absence of fast-math flags.
Both reconfigured builds compile and run that probe successfully; both host
suites pass 4/4. Main-agent self-review checked the algorithm against AIR and
the DXIL operand positions. These are structural compiler checks, not numeric
GPU acceptance or regenerated-container validation.

Production still rejects SampleGrad. Next: connect runtime bias and clamp order
to both binding branches, then validate actual multi-mip dispatch results.
