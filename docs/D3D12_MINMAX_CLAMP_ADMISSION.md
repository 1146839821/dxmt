# Task Analysis

Baseline df13341. Open the actual AIR resource-clamp path after the production
empty-set lowering, without weakening MSC or descriptor-range lifetime rules.

- Hypothesis: storing the packed OOB validity/one-mask alongside the CPU SRV
  identity lets existing locked snapshots validate precisely the descriptor
  generation used by recording or submission.
- Evidence: GPU word 2 already contains these bits; whole CPU/GPU descriptor
  copies and atomic ResolveDescriptors snapshots exist. Previous guards reject
  all nonzero resource clamps rather than checking the new defaults contract.
- Expected effect: known AIR SRV formats can execute nonzero resource clamps;
  unknown/null SRVs remain fail-closed, including ordinary-to-reduction sampler
  changes after Close. Static descriptors are not reread on submission.
- Risk: static resource plus volatile sampler, per-view defaults, CPU-only heap
  copies, null descriptors, fractional final-mip boundary and MSC Load clamps.
- Validation: actual AIR GPU empty-set/default-vector and multi-mip readbacks,
  static/live ownership checks and rejection regressions in both builds. Keep
  instruction clamps, feedback and DXIL reduction rejected in this checkpoint.

# Task Result

Known AIR texture descriptors now admit nonzero ResourceMinLODClamp instead of
blanket rejection. CPU SRVs retain the same packed validity/one-mask as AIR word
2, including CPU-only heap copies. Recording and live snapshots validate those
bits after the heap lock is released. Static invalid-default state is captured
even when the recorded sampler is ordinary, so a later volatile reduction
sampler cannot bypass validation. Null/unknown-default textures remain rejected.
Validation is conservatively table-wide, not precise per-instruction attribution.
MSC Texture.Load clamp checks, instruction-clamp/reflection rejection, opt-in
defaults, descriptor stride and capability declarations are unchanged.

Both production builds pass. Fifteen focused GPU cases per build pass, with
independent packed-RGBA expected constants:

- Empty single-mip RGBA/R8/custom R8 mapping: 0 / 0xff000000 / 0x0000ffff.
- Multi-mip clamp 1.25: 0xff000060; last mip 3.0: 0xff0000a0; 3.1: empty 0.
- Resource clamp 2.25 with sampler MaxLOD 0: 0xff000060.
- Static SRV range, live SRV rewrite after Close and root static sampler: empty 0.
- MostDetailedMip 1 and two exposed mips: resource clamps 1.75/2.0 yield
  0xff000060, while 2.1 is empty even though the underlying resource has mip 3.
- CPU-only R8 SRV copy into the visible heap: empty 0xff000000.
- Static SRV with ordinary volatile sampler changed to reduction after Close:
  empty 0, with no pending SRV descriptor use.

One additional resolver-only negative per build verifies a recorded static null
SRV: ordinary sampler resolution succeeds, the captured invalid-default bit is
set, and a subsequent live reduction sampler is rejected. No invalid GPU
dispatch occurs. This does not prove all unknown native formats: their helper
rejection has bounded native-unit coverage, but a real unknown-format GPU
admission fixture remains unverified.

Seven focused regressions per build pass: implicit minimum/bias, same-PSO
16/240/128/16 switch, 1D gradient mip, volatile-null rejection, instruction-clamp
PSO rejection and ordinary MSC swizzle sampling. Rejections are not dispatches.
Four additional AIR/MSC ordinary executions (two backends x two builds) verify
the new CPU snapshot bits and unchanged GPU words before/after CPU-only copying.
Both Meson suites pass 4/4. Matching isolated host/Unix binaries, shader-cache-off
and dyld provenance verified; logs are `clamp-admit-*.log` in the isolated runtime
and no-private subdirectory. No game DLL deployment or full acceptance matrices.

Independent code-review results:

- Standards: no hard violations; one positional-boolean readability finding,
  resolved by designated fixture initializers.
- Spec: no confirmed implementation bug; two partial validation findings for
  view-relative and descriptor-ownership coverage. Added nonzero view origin,
  CPU-only copy, static ordinary-to-reduction and recorded-static-null probes.
  Unknown-format GPU coverage remains explicitly outstanding, not credited PASS.

Main-agent follow-up review and git diff --check pass. Next implement instruction
clamp admission through the same lowering, retain feedback rejection, and extend
required numeric/descriptor contracts while continuing the full FL12_0 gaps.
Cube, anisotropic, DXIL reduction, tiled Tier 2 and full matrices remain open.
