# ADR 0001: SoftAP captive-portal provisioning

- Date: 2026-10-12
- Status: Selected and implemented; integrated setup and recovery flow owner-verified on board/phone
- Related issue: [#47 — Provisioning approach](https://github.com/mosaic-device/mosaic/issues/47)

## Context

A new device needs home Wi-Fi credentials and a backend URL without reflashing. The current firmware has a display/touch UI and ESP32-C6-hosted Wi-Fi. Pairing and backend transport are separate, unfinished features.

This record describes the implemented choice and its tradeoffs. It is not evidence of a completed comparative prototype or usability study: BLE and admin-app QR were not independently validated in this documentation pass.

## Alternatives

| Approach                  | Benefit                                                                                               | Cost or limitation                                                                                                                 |
| ------------------------- | ----------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| SoftAP captive portal     | Uses a phone browser and the existing hosted Wi-Fi path; does not require an installed companion app. | Requires switching phone networks; captive-window behavior varies. Setup HTTP relies on the temporary AP's protection.             |
| BLE through the companion | Could avoid switching the phone's Wi-Fi network.                                                      | Requires a supported BLE provisioning transport and companion workflow, neither established by the current implementation.         |
| QR on the admin app       | Could offer a web-based entry point.                                                                  | A QR is not itself a credential transport. Device scanning or another transfer channel would still need design and implementation. |

## Decision

Use a temporary password-protected SoftAP and captive HTTP portal. Show its credentials, direct HTTP URL, and Wi-Fi join QR on the device. The QR joins the AP; it does not carry home credentials, pair the device, or guarantee captive-window opening. Manual join and the displayed URL remain the fallback.

Keep setup AP-only. Validate and persist the submitted SSID, password, and `wss://` URL without a connection test. Show storage success for approximately six seconds, then reboot; only the next boot attempts saved STA connectivity. This separates the portal session from station connection attempts, at the cost of saving potentially incorrect credentials.

Require acknowledged credential rendering before AP startup. A missing acknowledgement fails closed, though software display acknowledgements do not prove physical pixel completion.

After a failed STA connection, retry with backoff rather than automatically opening an AP. Two taps on the device within ten seconds explicitly request recovery, persist `setup_next`, and reboot into setup. A successful replacement save clears that flag in the same settings commit.

## Consequences and limits

- `saved` means persistence succeeded, not Wi-Fi connectivity, server reachability, or pairing.
- Incorrect credentials replace previous settings; recovery is required to correct them.
- The backend URL is syntax-checked only. Setup performs no TLS or backend connection test.
- Setup is plain HTTP inside a temporary WPA2 AP. Knowledge of its displayed key is not proof of physical presence.
- NVS/flash encryption and factory-reset policy remain production decisions.
- The owner has verified the integrated AP-only save-and-reboot and recovery flow on the physical board with a phone; see the [acceptance record](../../firmware/main/features/provisioning/INTEGRATION.md#validation-status).
- Broader phone compatibility, fault injection, shutdown and LAN-isolation coverage are not established by this acceptance report.

## Follow-up work

- Track integrated provisioning acceptance under [#54](https://github.com/mosaic-device/mosaic/issues/54); #47 records the provisioning decision. The owner reports the flow works correctly; exact test-environment metadata remains to be recorded.
- Keep broader validation separate from the completed owner-reported setup/recovery check; see [integration notes](../../firmware/main/features/provisioning/INTEGRATION.md#validation-status).
- Finish Wi-Fi clock sync under [#35](https://github.com/mosaic-device/mosaic/issues/35).
- Keep pairing separate from credential provisioning; see [#46](https://github.com/mosaic-device/mosaic/issues/46).

For operational instructions and recorded hardware observations, see the [firmware README](../../firmware/README.md). For ownership, ordering, and security contracts, see [integration notes](../../firmware/main/features/provisioning/INTEGRATION.md).
