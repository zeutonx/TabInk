# TabInk Threat Model

Source: [architecture.md §12](architecture.md#12-security-model). TabInk injects pen and keyboard input into a Windows desktop and streams screen content. Treat any bug in auth or targeting as a security bug.

## Assets

- Control of the Windows desktop through synthetic pen input and shortcuts
- Screen content of the captured window
- Clipboard (LaTeX paste)
- Pairing keys on both devices

## Threats and required mitigations

| Threat | Mitigation (required unless marked later) |
|---|---|
| Unpaired device connects and injects | Mutual-auth TLS 1.3 on every channel. Explicit PC-side approval for new devices. Never open an unauthenticated injection port, even on loopback. |
| Network sniffing (Wi-Fi) | TLS on all channels, including video. USB uses the same TLS so the model stays identical. |
| QR/bootstrap token theft | One-use, short-lived, high-entropy token. Never advertised over mDNS (mDNS carries only service name/port). |
| Replay of captured packets | Per-session random `session_id`, monotonic `seq`, TLS. Reject stale sessions. |
| Injection into the wrong or a sensitive app | Allowlist of the selected capture target. Suspend when the foreground window changes unexpectedly. Never inject on the lock screen, UAC/secure desktop, elevated apps, or unknown windows. |
| Malicious screen viewing | Capture only a user-consented WGC target. Don't use capture to get around protected windows. |
| Recycled ADB authorization | ADB is MVP-only and needs developer mode. TabInk's own TLS pairing still applies on top of ADB. |
| Lost or stolen tablet | Revoke the device on the PC. Store keys in Android Keystore / encrypted prefs. Use short-lived session tokens. |
| Denial of service | Size-limit every message type. Rate-limit each client. Use bounded queues. |
| Privileged actions (shortcuts, clipboard, paste) | Require a user gesture. Verify the focused target (e.g. the Overleaf editor) before paste. |
| Data leakage through logs | By default, don't log screenshots, note content, or LaTeX. Diagnostics are opt-in and clearly labelled. |
| Tampered builds or updates | Signed distributables and update verification (later). |

## Security acceptance tests

From [architecture.md §18](architecture.md#18-testing-and-acceptance-plan):

- [ ] Unpaired device is rejected
- [ ] Replayed session packets are rejected
- [ ] Invalid or unpinned TLS certificate is rejected
- [ ] Injection stops when focus changes away from the target
- [ ] No injection while the PC is locked
- [ ] Oversized messages are rejected without a crash
- [ ] Spoofed mDNS discovery doesn't lead to a connection without approval
- [ ] Expired QR token is rejected
- [ ] A revoked device can't reconnect
- [ ] The host never serves unencrypted video, even over USB
