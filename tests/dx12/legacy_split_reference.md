# Cross-queue legacy split barrier reference probe

The `dx12_legacy_split` executable has two reference modes:

- `--reference-cross-queue`: submit BEGIN and signal on the producer, then wait
  and submit END and readback on a second direct queue.
- `--reference-reverse-queue`: submit the second queue's wait and END before
  submitting the producer's BEGIN and signal. GPU order is still established by
  the fence.

Both modes use the same exact byte readback oracle as the DXMT regression. They
do not assert a driver's internal COM resource-release timing. They attempt to
enable the D3D12 debug layer before creating the device, then print the available
info queue messages after completion. A corruption or error message makes the
probe fail. A missing debug layer or info queue is reported explicitly: a
successful readback without them is not evidence that the debug layer accepted
the barrier sequence. In particular, the cross-queue ownership rules for the
write state around the split remain an issue to assess with the native debug
layer; do not infer their validity merely from a successful readback.

To run a native reference, build the `dx12_legacy_split` Meson target for
Windows and copy only the executable to an otherwise empty directory on the
Windows host. Do not copy DXMT's `d3d12.dll`, `dxgi.dll`, or WineMetal DLLs
there. Install the Windows Graphics Tools optional feature for the debug layer.
For example, in PowerShell from that directory:

```powershell
Get-FileHash .\dx12_legacy_split.exe -Algorithm SHA256 | Out-File identity.txt
Get-ComputerInfo | Out-File -Append identity.txt
dxdiag /t dxdiag.txt
& .\dx12_legacy_split.exe --reference-cross-queue 2>&1 | Tee-Object cross.txt
$crossExit = $LASTEXITCODE
& .\dx12_legacy_split.exe --reference-reverse-queue 2>&1 | Tee-Object reverse.txt
$reverseExit = $LASTEXITCODE
"cross=$crossExit reverse=$reverseExit" | Out-File exits.txt
```

Retain the executable hash, OS and GPU/driver details, both complete outputs,
and exit codes. Check that each output reports an enabled debug layer and an
available info queue, as well as exact readback success and no error messages.
Warnings must be reviewed individually. Record observed native behavior
separately from specification requirements. A crash, timeout, device removal,
missing debug layer, or missing output is not a pass. These two modes do not
test invalid split pairs, all resource types, or all cross-queue orderings.
