# Timestamp-zero diagnosis (baseline f3a3c3fa)

Task Analysis: an unresolved GPU timestamp query occasionally returns an end
value of zero although graphics readback is correct. Do not remove the query
assertion or report serial retry as a fix. First establish a sharper feedback
loop before changing synchronization. All probes below use matching cached
PE/native overlays, not game or Wine-prefix DLL deployment.

Task Result — reproduction evidence:

- Normal build, 24 sequential process invocations alternating packed UINT MSC
  direct and indirect: 24 passed the full existing fixture, including timestamp,
  occlusion and RGBA checks. Logs `timestamp-baseline-0.log` through `-23.log`.
- Same alternating commands with only `DXMT_SHADER_CACHE=0` added: 7/8 passed.
  Invocation 0 (direct) failed with timestamp `1003929323278083 -> 0` while RGBA
  remained `0xaa4080ff`. Logs `timestamp-no-cache-0.log` through `-7.log`.
- Logs are under `/Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/`.
- No build, renderer edit, or fixture assertion change occurred between these
  two groups. These small samples do not establish a causal cache dependency.
  They do establish a current direct-render reproduction without ExecuteIndirect.

Reproduction command (exit 1 on the observed timestamp failure):

```sh
env DXMT_SHADER_CACHE=0 \
  WINEPREFIX=/Users/zhangbo/Documents/Vibe-Codeding/wineprefix \
  WINEDEBUG=+loaddll WINEDLLOVERRIDES='d3d12,dxgi,winemetal=n,b' \
  /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/safety-runtime/bin/wine \
  /Users/zhangbo/.cache/dxmt-reconciliation.ZLDvwE/safety-normal/dx12_graphics_sm6.exe \
  /Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/graphics_packed_uint.vs.cso \
  /Users/zhangbo/Documents/Vibe-Codeding/dxmt/build/tests/dx12/graphics_sm6.ps.cso \
  --packed-uint
```

Next action: minimize toward timestamp sample/resolve without rendering while
retaining this original reproduction. Compare sampling, resolve and queue fence
completion; exclude cache as a proven cause until a controlled intervention
changes the failure rate. Generated ICB root handling is not required for this
direct-render failure. A single passing retry does not qualify timestamp support.

Self-review: checked current exit codes and exact failure text against all 32
logs; no production workaround or capability promotion was introduced. Warm
passes and a cache-disabled failure are recorded separately. Timing correctness
remains unresolved, and this report does not claim full FL12_0 qualification.
