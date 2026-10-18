# SoftAP provisioning

Status: implemented and verified on the physical board with a phone.

## Decision

Use a temporary WPA2 SoftAP and HTTP captive portal. Display the AP credentials, Wi-Fi join QR and direct HTTP URL. The QR joins Wi-Fi; the URL is the fallback when a captive window does not open.

Keep setup AP-only. Validate and save the home SSID, password and `wss://` URL, show success for about six seconds, then reboot. Only the next boot attempts Wi-Fi. Incorrect credentials require explicit on-device recovery: two taps within ten seconds reboot into setup.

Require display acknowledgement before opening the AP; missing acknowledgement fails closed.

## Why

This works with a phone browser and the existing hosted Wi-Fi transport, without installing an app. BLE would require another transport and companion workflow. A QR alone cannot transfer home-network credentials to the device.

## Tradeoffs

- The phone must switch networks; captive-window behavior varies.
- Saved means persisted, not connected, paired or server-verified. New credentials replace old settings without a connection test.
- Setup uses HTTP within the temporary WPA2 network. Knowing its password does not prove physical presence.
- Pairing, backend transport, flash/NVS encryption and factory-reset policy are separate concerns.

See the [setup guide](../../firmware/README.md#phone-setup) and [design constraints](../../firmware/main/features/provisioning/README.md#design-constraints).
