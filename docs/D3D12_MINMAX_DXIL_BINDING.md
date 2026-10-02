# DXIL reduction state and private bindings

## Task Analysis

- Hypothesis: qualify sampled texture/sampler pairs in DXIL, load runtime
  reduction state from a private CBV and branch between original sampling and
  the existing point-footprint lowering. This permits ordinary/reduction sampler
  changes without recompiling shaders or rewriting application descriptors.
- Evidence: the preceding lowering passes real DXC/MSC/GPU probes, but constant
  probe state cannot represent a volatile sampler or resource descriptor.
- Expected effect: compiler-visible point SRV/sampler bindings plus explicit
  application binding identities and a runtime state record for submission.
- Risk: metadata aliases, modern/nonuniform handles, register collisions,
  feedback and branch SSA must fail closed. Root reflection, observation and
  retention must be connected before production admission.
- Validation: private-module verification, regenerated full DXC container
  validation, MSC compilation and state-changing native readback; D3D12 replay
  acceptance remains required after integration.

## Implementation

The internal module pass qualifies legacy float Texture2D SampleLevel pairs and
returns application texture/sampler register identities. Pair ordinal N selects
private tN/sN in space 2; s(N + pair count) is an ordinary sampler preserving the
application filter but with native LOD limits/bias removed. Private b0 holds one
32-byte runtime state record per pair. This is not the MSC descriptor ABI.

The enabled bit selects ordinary versus reduction without recompilation. Both
branches explicitly apply sampler limits followed by resource clamp, and return
component defaults for an empty mip set. Point views have zero native resource
clamp. During the probe, the original MSC ordinary SampleLevel returned 128 for
resource clamp 0.75 where 104 was expected. Explicit ordinary lowering repairs
this prepared path only; the default D3D12 MSC path is not changed here.

Arrays, nonuniform/modern handles, unsupported sample consumers, status feedback,
resource interval aliases, private-space collisions and inconsistent entry-point
resource roots fail closed. Equivalent but distinct entry resource roots are
also deliberately rejected. Metadata publication uses copy-on-write roots and
entry records. A failed lowering requires discarding the private module and
leaves the caller's binding vector unchanged.

## Validation and review

- Normal and no-private native libraries/probes build successfully. Both builds
  produce byte-identical binding IR for the single- and two-pair fixtures.
- Nine qualification probes cover eight rejections and accepted duplicate-pair
  deduplication, including metadata layout and failure output preservation.
- Two full regenerated containers pass DXC validation and MSC 4.0.1 compilation
  with Apple9 and textureMinLODClamp. Reflection drives the native argument layout.
- Single-pair and two-pair loop fixtures each pass fifteen runtime state cases
  on the normal native probe. The two-pair fixture shares resource clamp/defaults
  but varies sampler state; four output words verify independent pair indexing
  and loop repetition. This is thirty dispatches, not thirty new matrix suites.
- An untransformed ordinary fixture separately returns 128. Default-component
  255 is a synthetic transport-mask check, not full format acceptance.
- Both Meson suites pass 4/4; structural IR probes and diff whitespace checks pass.

Artifacts: `/Users/zhangbo/.cache/dxmt-minmax-binding.wRW3Pe`. The earlier
`binding-two.final.gpu.log` predates the shared-resource test correction and is
superseded by `binding-two.shared-resource.gpu.log`.

Standards review led to grouped pair records and named DXIL opcodes. Independent
Spec review found resource aliases, entry root ambiguity and weak test oracles;
qualification and coverage were corrected. Follow-up review found inconsistent
shared resource clamps in the two-pair probe; both records now share the texture
state, and adjusted readbacks pass. Main-agent final self-review confirms these
changes do not admit a production D3D12 reduction path.

## Remaining production work

No native export, D3D12 shader preparation, augmented/reflected application root,
PSO selection or recording/submission binding is connected yet. Native linear
binding probes do not prove D3D12 integration. Static descriptors must snapshot
at recording with no pending/live reread; volatile descriptors must resolve and
retain unique live slots under the heap lock, then fan out outside it. The next
bounded task is the host/compiler preparation boundary, followed by root and
runtime ownership integration. Cube, anisotropic, meaningful feedback and wider
operation/format contracts remain open. No capability promotion, game acceptance,
installed DLL deployment or push.
