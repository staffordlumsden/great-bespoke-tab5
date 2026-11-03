# Tab5 Shell 0.1.2 — SSH radio-startup repair

1 November 2026. Source commit:
`941c2a56d13d886843bf07d5f85ce92eb011820a`.

## Observed failure

The user reported that an SSH attempt in 0.1.1 rebooted to Console. A passive,
read-only USB capture of a repeated attempt independently confirmed:

```
TAB5_SHELL_BOOT version=0.1.1 target=ESP32-P4 local_only=1
TAB5_SHELL_FIRST_FRAME display=1 chrome=1 terminal=1 presented=1
TAB5_SHELL_STATUS healthy=1 first_frame=1 confirmed=1 radio=NONE sd=1 home=1
Identified slave [esp32c6] != Expected [esp32]
assert failed: verify_host_config_for_slave transport_drv.c:500 (0!=0)
```

The failure is in ESP-Hosted transport initialization, before SSH negotiation or
host-key verification. The physical companion identifies as ESP32-C6, while the
compiled host expects ESP32. The assert reboots the P4, whose confirmed-app reset
path correctly returns to resident Console.

The earlier integration set the derived target string in defaults but omitted
the controlling `CONFIG_SLAVE_IDF_TARGET_ESP32C6=y` choice. Kconfig consequently
selected ESP32 and regenerated both target strings as `esp32`. This configuration
error was missed by the earlier contract tests, which checked version pins and
power ordering without checking the actual companion selection.

## Narrow repair

- Select ESP32-C6 in both `sdkconfig` and `sdkconfig.defaults`. Reconfigure/build
  with ESP-IDF; generated definitions now select C6 and derive `esp32c6`.
- Add compile-time checks for the choice and both effective target strings.
- Strengthen host contract checks for saved/default/generated configuration,
  resolving generated macro aliases. The new regression failed on 0.1.1's
  ESP32 choice and passes on the corrected configuration.
- Version the package/banner/notices as 0.1.2. Retain the accepted first-frame
  repair, offline startup, powered radio ordering, optional SSH profiles, strict
  host-key pins and all existing session logic. No crypto downgrade, trust
  bypass, library upgrade or C6 firmware update.

## Verification and delivery

- `bash projects/tab5-shell/firmware/tests/run_checks.sh`: **PASS** — all six
  ASan/UBSan native binaries and strengthened contracts. The existing upstream
  parser unused-constant warning is non-fatal.
- In `projects/tab5-shell/firmware`, `../../../idf.py build`: **PASS**, including
  a rebuild after the source commit. Compiled C6 guards pass against the actual
  generated configuration. ESP-IDF 5.5.5, pre-v3 P4/minimum revision 100, Hosted
  1.4.0 and Wi-Fi Remote 0.8.5 retained.
- SD packager/validator: **PASS** — P4 header/revision, checksum/appended SHA-256,
  manifest/notice equality, image corruption/truncation rejection.
- Image **1,402,576 bytes**; OTA slot **5,242,880 bytes**; headroom
  **3,840,304 bytes**.
- The 0.1.2 triplet was copied to the mounted SD `/firmware/` and byte-verified.
  Root catalog remains **8/8**, so Console 0.3.5 can list the package. SD safely
  ejected for an operator Console installation.
- At the user's request, inventoried every firmware manifest recursively and
  retained the newest release plus newest prior rollback per project. Shell
  0.1.2 is active; 0.1.1 is under `/firmware/rollback/Tab5Shell-0.1.1/`. Removed
  the three older 0.1.0 SD files only after a verified local recovery copy.
  The other seven firmwares each had one version and were unchanged. Existing
  local release/recovery artifacts remain preserved; all retained bytes match.
- **Corrected-image radio startup: PASS, live serial observed.** The 0.1.2
  capture confirms first-frame/boot confirmation, C6 base transport setup, Wi-Fi
  RPC exchange and progress through TCP/SSH handshake to host-key verification,
  without the previous companion-mismatch assert. The user reported that it
  seemed to work but displayed `HOST KEY CHANGED`.
- That later message came from a **missing Shell SD pin file**, not a demonstrated
  server key change. Fresh ECDSA and Ed25519 scans matched this Mac's existing
  saved host keys; a credential-free strict ECDSA OpenSSH check passed. Added
  only the verified ECDSA pin to Shell's private SD trust file. The firmware's
  actual pin parser, under ASan/UBSan, accepts the verified key and rejects an
  altered key; SD bytes were reread/verified and the card safely ejected. Pin
  data, target identity and audit remain local/ignored; the firmware image and
  this Mac's trust store were unchanged. Current firmware's broad error label
  also covers missing/unknown/malformed pins.
- **Remote SSH connection: PASS, user-reported.** After the verified pin setup,
  one attempt reported `Network connection failed`; serial recorded a
  `getaddrinfo` failure (202). The user subsequently confirmed "connected
  successfully" on 1 November 2026. No resolver change was made. The successful
  connection is operator evidence; an authentication-success marker or remote
  command output was not captured in the host log.
- Disconnect/Console return from an authenticated session, heap/PSRAM peaks,
  task stack margins and other feature-specific physical checks remain
  unverified until separately observed.

## Release hashes

Local ignored directory: `projects/tab5-shell/artifacts/release-0.1.2/`.

| File | SHA-256 |
|---|---|
| `sd-library/Tab5Shell-0.1.2.bin` | `b532ad3e5c9e2a39e5b43f6055d4d0d8644d1c474b16ccd60b85ee60c5dbb1a5` |
| `sd-library/Tab5Shell-0.1.2.json` | `1620645ed5429d88bb7b9b5c3dab4cdf8e746a7a09f3f99a25a7b9768844f46a` |
| `sd-library/Tab5Shell-0.1.2-LICENSE.txt` | `447e533070ea550dd80d189c6e7b3bf42af04ba5ad61e198918ee527fe2cd71c` |
| `Tab5Shell-0.1.2-SD.zip` | `9206946e0edbfe09f963d5d41d5828e79fff5aee2c8904714b9d1c879c9c4ad8` |

ZIP CRC/content/byte checks pass. Archive includes the triplet, hello example,
instructions and checksums. Local ignored serial logs and SD-retention audit
are under project `artifacts/`; no raw private state is committed.

Install through Console SD App Library only, explicitly selecting **Tab5Shell
0.1.2** after REFRESH/DOWN as needed. Recheck offline prompt, repeat SSH, observe
whether it connects or returns a local error, then verify clean local/Console
return. Unknown/changed host keys must still fail before authentication. No
host flash, erase, factory/table/NVS/C6 update, SD format or remote push occurred.
