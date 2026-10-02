# DXIL MinMax pair binding

## Task analysis

- Hypothesis: one retained per-pair artifact can combine the captured SRV and original sampler descriptor into the private point/ordinary descriptors and complete 32-byte state without rereading a heap.
- Evidence: descriptor snapshots retain the exact MSC native view; original sampler descriptors and unclamped sampler-info conversion are now available. Compiler lowering expects private texture clamp zero and shader-owned sampler/resource clamp order.
- Expected effect: a production pair materializer usable by recording/static and submission/volatile paths, with coherent payload/object lifetimes.
- Risk: dereferencing a later renamed view, integer/array/multisample mismatches, double clamp/bias, invalid defaults and publishing incomplete sampler handles.
- Validation: actual native sampler creation from retained snapshots, clamp/default preservation, failure publication, normal/no-private builds. Dispatch and full MinMax admission remain subsequent work.

## Result

`PrepareD3D12MinMaxPairBinding` now builds a retained pair artifact without heap lookup. It uses the captured MSC native texture view, qualifies 2D single-sample non-integer/non-depth views, validates defaults and finite resource clamp, creates owned point/ordinary samplers using the existing sampler factory, and writes zero texture clamp/bias descriptor metadata plus complete per-pair state. It never reacquires the current texture allocation. Failure leaves the output unchanged; residency and command-list selection remain the caller's subsequent responsibility.

The subset probe caught and corrected an overly strict negative-clamp rejection. `d3d12_texture.cpp` subtracts MostDetailedMip before storing resource clamp; API clamp .25 with mip start 1 legitimately becomes -.75. The pair artifact preserves that view-space value.

Validation receipts: `/Users/zhangbo/.cache/dxmt-minmax-pair.QW9QR3` (use final logs after full builds).

- Normal/no-private full builds and the focused pair executable built successfully. The probe compiles the production pair helper directly and obtains descriptor snapshots from the actual staged D3D12 DLL.
- Each configuration passed native ordinary/minimum/maximum sampler creation, exact captured view identity, zero private metadata, R32_FLOAT default-alpha one, subset view dimensions/mips and relative clamp, six rejection/publication checks, and native view/sampler survival with other references removed.
- Existing Meson suite passed 4/4 per configuration. No shader dispatch or readback was performed.
- Final staged D3D12 hashes match build output; loader receipts identify the staged DLL and isolated winemetal runtime. The first extended subset run failed and is not acceptance evidence.

Self-review checked source snapshot ownership, view-relative clamp, format qualification, zero private clamp/bias and transaction-like publication. The integration skill influenced native object retention and descriptor encoding. Remaining production work: static/volatile pair capture, private tables/state buffer materialization, residency, command replay selection and actual GPU readback; no FL/SM promotion.
