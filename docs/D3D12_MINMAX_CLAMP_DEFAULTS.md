# Task Analysis

Baseline 5bdb0b1. Preserve the full FL12_0 gaps-first objective.

- Hypothesis: unused word 2 of the 32-byte AIR texture descriptor can carry a
  four-bit mask of one-valued out-of-bounds defaults, after view component mapping.
- Evidence: formats already encode native/view swizzles; existing metadata word
  contains clamp and array length, and words 2/3 are padding. MSC uses a separate
  descriptor buffer. Defined OOB components are zero; absent alpha is one.
- Expected effect: unblock correct reduction clamp empty-set results without
  changing stride or sampler semantics.
- Risk: single/two/three-channel and A8 formats; forced/remapped channels; copies,
  null descriptors, D3D11 ABI and old shader caches.
- Validation: independent format/swizzle constants, actual GPU descriptor
  readback/copy, ordinary and reduction regressions in both builds.

Reference: D3D11.3 sections 5.8.5 and 5.9.4.5.4:
https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
Resource/instruction clamps outside the view require OOB component defaults,
not a sample from the final mip. This checkpoint transports those defaults;
clamp admission remains rejected until the actual empty-set lowering is verified.

# Task Result

AIR texture word 2 now carries a four-bit one-mask and bit-4 validity marker.
Word 0 (resource), word 1 (array/clamp), 32-byte stride and separate MSC entries
are unchanged. The format helper recognizes supported DXGI native formats,
distinguishes missing alpha from defined alpha (including A8), and applies the
existing encoded format/view swizzle. Unknown native formats get no valid tag.
Root-signature AIR binding loads word 2 into an optional texture-descriptor field;
legacy D3D11 bindings retain nullptr. No clamp consumer uses this field yet.

Both production builds complete, after Meson reconfiguration registered the
new native unit target. Both native units pass 18 independent constants and
two unknown-format rejections. These are bounded constants, not full format
matrix acceptance. Both Meson suites pass 4/4 including the new unit.

Twelve isolated D3D12 executions pass (RGBA/R8/custom R8 mapping x DXBC/DXIL x
normal/no-private). Each checks actual AIR and MSC GPU descriptor contents,
then CPU-only-to-shader-visible SRV copy, then ordinary GPU sampling returning
255. One-masks are independently expected 0/8/3. Validity, unchanged array/clamp
word, cleared padding, and unchanged MSC resource/metadata words are checked.
Shader cache disabled; matching regular host/Unix binaries and dyld paths used.
This proves descriptor storage/copy, not an empty-set shader result.

Six focused regressions per variant pass: implicit minimum and instruction bias,
same-PSO SampleLevel switch, 1D gradient mip selection, static instruction-clamp
PSO rejection and volatile resource-clamp resolver rejection. Rejection checks
are not successful GPU dispatches. Main-agent Standards/Spec self-review caught
uninitialized padding in CPU-only heap storage: SRV writes now initialize all
four words, and UAV writes clear the new inactive fields. Header layout and
legacy aggregate defaults remain compatible. llvm skill guided field/IR review;
code-review skill guided the two-axis self-review, with independent agents still
unavailable. git diff --check passes. No game DLL deployment or capability change.

Next: consume validity/one-mask in actual reduction clamp empty-set lowering,
define fail-closed handling for unknown/null descriptors, verify view-relative
fractional clamps and component defaults, then reconsider recording/submission
resource-clamp rejection and shader-cache compatibility. Clamp support, full
MinMax, tiled Tier 2, DXIL and complete FL12_0 acceptance remain unfinished.
