# MSC typed-buffer experiment

## Goal and baseline

At baseline `9ed1ed4c`, MSC typed-buffer views do not consistently honor
`FirstElement`, view bounds, or `GetDimensions`. On the local M1 Max, a
four-element R32 view returned the 64-element backing-buffer width, and a
write/read at view index 4 accessed the backing resource. The existing
AIRCONV path is independent. These are local observations, not a statement
about all converter versions or hardware.

## Scope and design

`msc-typed-buffer-experiment` is a **standalone research tool**; no production
pipeline calls it. It matches a direct texture-handle load from field 1 of a
three-qword descriptor GEP, including a dynamic element index, and uses the
same GEP to load field 2. It recognizes typed-buffer read, write and width
operations, rejects unknown texture-buffer intrinsics and indirect calls,
and verifies the rewritten module before writing a metallib. The tool assumes
an experimental texel-size byte in metadata bits 40–47; production descriptors
**do not provide this field**, and no claim is made that MSC reserves those
bits for this purpose. Do not use its output as a production shader.

The tool maps out-of-view indices to the maximum unsigned index and uses the
experimental texel size and byte length to compute dimensions. A zero size
produces a zero count. The approach is not approved for production: descriptor
origin, all shader shapes, conversion versions, atomics, null handling,
residency, volatile updates, bounds and status semantics remain unverified.
In particular, changing MSC's descriptor ABI without a compatibility guarantee
is not an acceptable production design. A separate sidecar and its copy,
update, lifetime, synchronization and direct-indexing behavior require study.

## Evidence and tests

`meson test -C build 'airconv msc typed buffer experiment'` runs synthetic
positive and rejection controls. Local native Metal and isolated Wine GPU
experiments have exercised dynamic R16/R32 descriptors and R32 texture-buffer
loads, stores, bounds and dimensions. The GPU fixtures use experimental
metadata and injection; they do not validate the existing production path.
See `dxmt-test/n95-capability-gate/msc-buffer-air/evidence.md` for the machine,
artifacts, validation limits and logs. Offline metallib shader-validation
instrumentation has not been established.

Rollback is limited to the optional Meson target and files in `tests/airconv`.
No feature level, shader model or tier is promoted by this experiment.
