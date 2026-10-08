# Temporary SM6.6 / FL12_0 development gates

Task Analysis: expose the requested development capabilities behind independent,
default-off environment gates while incomplete runtime matrices are still open.
Expected effect: permit targeted application startup and capability probing.
Risk: applications may select unsupported operations after seeing the experimental
aggregate capability. This is not conformance or game-rendering acceptance.

- `DXMT_EXPERIMENTAL_SM6_6=1`: report up to SM6.6 when the MSC core path is usable
  (converter API 4+, converter available, argument buffers Tier 2).
- `DXMT_EXPERIMENTAL_FL12_0=1`: permit FL12_0 device creation and report it through
  feature-level queries when the same core prerequisites and Apple GPU family 7+
  are present, including experimental no-private builds.

Unset, `0`, and any value other than the exact string `1` preserve existing
behavior. The two gates are independent. Set them before creating the device;
shader-model capability is retained in the device capability snapshot.
Neither gate advertises FL12_1 or SM6.7, changes individual feature options,
changes DXBC/AIRCONV vs DXIL/MSC routing, substitutes unsupported operations,
or bypasses validation. In particular, no-private logic-op limitations and
incomplete tiled-resource/timestamp matrices remain visible limitations.

Remove these temporary opt-ins after full qualification.

Initial validation: reconfiguration and full normal/no-private builds pass;
`git diff --check` passes. Source self-review confirms device creation retains
the selected maximum feature level, so FEATURE_LEVELS queries use the same
snapshot; shader-model requests are still capped to the snapshot maximum.
Runtime result: the same `dx12_experimental_caps.exe` per build passes five
combinations in both normal and no-private cache-only Wine runtimes: both `0`,
SM only, FL only, both `1`, and both `true` (non-enabling values). The probe checks
device-creation/query agreement, rejects FL12_1, caps SM6.9 requests to the selected
maximum, and preserves SM5.1 requests. Normal defaults to FL11_1/SM6.0 on the
tested host; no-private defaults to FL11_0/SM6.0. Both enabled report FL12_0/SM6.6.
Evidence: `dxmt-reconciliation.ZLDvwE/experimental-caps-{normal,no-private}-*.log`.
Missing converter/hardware prerequisite cases are source-reviewed, not runtime
injected. Unset and `0` share the exact-string comparison but only `0` was tested.

Final review: Standards and Spec report zero actionable findings on the gates.
Probe self-review checks expected values supplied independently by the runner;
it does not derive the expectation from the queried capability. No deployment
to the game/prefix and no push were performed. These results validate the gates,
not complete SM6.6/FL12_0 conformance or game-rendering acceptance.
