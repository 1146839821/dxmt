# Typed origin compiler packaging

## Task Analysis

- Hypothesis: an explicit deployment input can connect Meson install to the
  runtime's DLL-relative compiler location without silently redistributing the
  repository's Microsoft binary release.
- Evidence: CI packages Meson install trees; current install omits dxmt-dxc.
  The vendored DXC includes separate Microsoft distribution terms.
- Expected effect: configured compiler/validator DLLs and supplied notices
  install beside D3D12 in a deterministic dxmt-dxc folder.
- Risk: wrong PE architecture or missing notices; reject malformed input and
  leave default redistribution policy unchanged.
- Validation: configure/install into a fresh task-owned tree, compare deployed
  bytes, reject wrong architecture, actual default GPU readback from installed
  layout. This is a deployment mechanism, not license clearance or FL promotion.

## Task Result

`-Dtyped_origin_dxc_path=/absolute/deployment/directory` now installs
dxcompiler.dll, dxil.dll and a required nonempty LICENSE.txt into
`<D3D12 install directory>/dxmt-dxc`. Supplied MIT, LLVM, Microsoft and third-party
notice files are preserved when present. Operators must supply complete applicable
notices and establish their distribution rights; the option does not do that.
The default is empty and CI release settings remain unchanged.

Configuration checks absolute paths, PE/DOS header bounds, DLL flag and optional
header/machine agreement. x86 and x64 are distinct; aarch64 Windows cross builds
require ARM64EC input rather than pretending the vendored pure ARM64 DLLs are
interchangeable. This does not verify signing, exports, dependencies or legal
approval. Native builds reject this Windows deployment option.

Validation at `/Users/zhangbo/.cache/dxmt-typed-package.cZTYI4`:

- Both variants reconfigure, fully build and install into task-owned trees.
  Installed compiler/validator and four supplied notice files match input bytes.
- Six synthetic deployment unit cases pass; each Meson suite passes 5/5.
- Actual installed D3D12/compiler paths, with both runtime overrides unset,
  pass half-float nonaligned-view and ordinary structured root-UAV GPU readback
  in both builds: four actual executions. Load logs identify installed DXC paths.
- Actual x64 input is rejected for x86. No x86/ARM64EC runtime acceptance.
- Local build options restored to empty; normal default install manifest omits
  dxmt-dxc. Both restored full builds/host suites pass; diff whitespace passes.
- An initial no-private probe staging path error was corrected before its GPU
  run; it is not a rendering failure. Generated Python cache was moved to the
  evidence directory and future tests disable bytecode writes in the repository.

Standards self-review: explicit deployment only, deterministic install location,
native Python validation and task-owned staging. Spec self-review: deployment
mechanism works without changing default redistribution policy or capability
claims; installed numeric oracles pass. Main-agent review only, independent
reviewers unavailable. MSC integration guidance kept installed-path evidence
separate from source/build claims. No game/prefix modifications or push.

Unconditional release packaging still needs an approved compiler distribution.
Graphics, embedded roots, root-updating indirect and complete typed/FL matrices
remain open. This is not full default FL12_0 support or game/tessellation acceptance.
