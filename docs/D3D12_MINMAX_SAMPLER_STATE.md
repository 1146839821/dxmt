# MinMax sampler state and snapshots

## Task analysis

- Hypothesis: MinMax private point/ordinary samplers must derive from the original D3D12 descriptor, not the AIR point surrogate or MSC handle payload.
- Evidence: sampler heap snapshots currently retain native objects and GPU descriptors, but lose original filter/address/LOD descriptor fields; reduction samplers use a point surrogate and zero MSC entry.
- Expected effect: preserve the original descriptor coherently through add/copy/snapshot/invalidate, and provide one validated conversion into private sampler infos and the 32-byte DXIL state ABI.
- Risk: payload/metadata mismatch during overwrite; accidentally admitting incomplete DXIL reduction or changing ordinary sampler semantics.
- Validation: focused state tests and actual sampler heap copy/overwrite snapshots in normal/no-private builds. No new sampler admission or command-list selection at this checkpoint.

## Result

The heap now owns original descriptors and copies them under the sampler mutex with object/AIR/MSC payloads. Invalidated slots clear both object and descriptor. Existing snapshots remain independent of later slot writes.

`PrepareD3D12MinMaxSamplerInfo` converts supported basic ordinary/minimum/maximum filters to unclamped point/ordinary native infos and a sampler-dependent state record. It preserves shader LOD clamps and addresses, leaves texture-dependent fields zero for later completion, rejects comparison/anisotropic/unknown filters, non-finite clamps, invalid addresses and unsupported border colors, and publishes outputs only on success. It does not create native samplers or enable DXIL reduction admission.

Normal and no-private full builds succeeded after fixing two test-build omissions: the sampler probe AIR include path and backend-failure probe's private shader/root preparation sources plus ole32. No production fallback was introduced.

Isolated runtime receipts: `/Users/zhangbo/.cache/dxmt-minmax-sampler.XhmVs3`.

- Each configuration passed 72 existing sampler contracts, 24 private state combinations and six rejected inputs with unchanged outputs.
- Actual DLL heap probes passed visible-to-CPU copies, ordinary and existing opt-in AIR reduction original-descriptor preservation, overwrite isolation, invalidation/invalid-slot copy and retained old native sampler lifetime.
- Existing Meson unit suite passed 4/4 per configuration.
- Final staged DLL hashes match current build output; loader logs identify staged native D3D12/DXGI and the isolated winemetal runtime. Early receipts precede final full-build relinks; use `.final.log` receipts.

Self-review checked mutex coverage, invalidation/copy symmetry, filter masks and output publication. Integration skill constraints influenced supportArgumentBuffers, private unclamped samplers and object retention. This checkpoint does not prove dispatch/readback or FL12_0. Remaining MinMax production work includes private texture descriptors, completed per-pair states, static/volatile recording/submission materialization and command-list selection.
