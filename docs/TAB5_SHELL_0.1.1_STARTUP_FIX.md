# Tab5 Shell 0.1.1 — first-frame startup repair

1 November 2026. Source commit:
`e56fad54291ced1341bec17c5b585175abee2f3e`.

## Report and reproduced defect

The user physically tested 0.1.0 through Console and observed a black screen,
then return to Console after approximately 20 seconds. **0.1.0 failed physical
acceptance.** Its successful host/build/package checks did not establish startup
success on the device.

A new host test compiles the actual `ShellDisplay`, actual PixelFonts/Terminal
Mono atlases and actual VT100 renderer against a fake HAL implementing the real
HAL's strict 1280x720 rectangle bounds. Before the fix it fails:

```
FAIL startup display: initial chrome failed, variant=0;
first HAL rejection=x=1282 y=12 w=2 h=2
```

The SD status label started at fixed x=1018. With `SD READY / HOME WRITABLE`
at 2x PixelFont scale, its glyphs extended beyond the right edge. The real HAL
returns `ESP_ERR_INVALID_ARG`; `draw_chrome` returns false, and startup skips
terminal rendering and `present_all`. The backlight stays hidden. The existing
15-second failed-confirmation return path then selects Console. Startup time
plus that guard is consistent with the user's approximately 20 seconds.

The previous viewport tests used a fake terminal display without exercising
the actual pixel drawing adapter, so they missed this boundary failure. No
serial trace of the original failed 0.1.0 launch was captured.

## Repair

- Right-align the SD label using its measured PixelFont width and a 14px margin.
  Keep the HAL bounds checks; do not silently ignore failed drawing.
- Preserve complete-frame-before-confirmation, the 15-second recovery guard,
  Console return, shared partition layout, offline startup and C6 policy.
- Add `TAB5_SHELL_FIRST_FRAME` diagnostics for display, chrome, terminal and
  presentation stages; keep the existing boot health/confirmation marker.
- Version application/banner/notices as 0.1.1. Generalize the release validator
  to validate the version from the built image, including historical 0.1.0.
- Add the actual drawing-adapter regression to normal host checks. It covers SD
  ready, SD absent, home unavailable, initial terminal rendering, no presentation
  before the completed frame, status/touch-keyboard bounds and presentation
  failure propagation.

## Verification

- New display regression: **FAIL before / PASS after**, Clang ASan/UBSan.
- `bash projects/tab5-shell/firmware/tests/run_checks.sh`: **PASS**, all six
  native sanitizer binaries and configuration/dependency/partition/lifecycle/
  security/provenance contracts. Upstream parser unused-constant warning only.
- In `projects/tab5-shell/firmware`, `../../../idf.py build`: **PASS**, including
  rebuild after the source commit, ESP-IDF 5.5.5, pre-v3 P4, minimum revision 100.
- SD packager and `tools/validate_release.py`: **PASS**, header/revision,
  checksum/appended SHA-256, malformed-image rejection, image/manifest/licences.
- Image: **1,402,576 bytes**; 5 MiB slot **5,242,880 bytes**; headroom
  **3,840,304 bytes**.
- All three 0.1.1 files copied byte-identically to the user's mounted SD
  `/firmware/`; existing **24 firmware files**, including 0.1.0, unchanged.
  No package AppleDouble sidecars remain. SD safely ejected for operator use.
- Serial independently identifies ESP32-P4 v1.3, resident Console 0.3.5,
  ST7121 detection, SD-ready startup and the existing two 5 MiB OTA slots.
- The first delivery retained nine root-level manifests. Console 0.3.5's
  `TAB5_INSTALLER_MAX_PACKAGES=8` stops scanning after eight; the user therefore
  could not see 0.1.1. Moved only the old Shell 0.1.0 triplet into
  `/firmware/rollback/Tab5Shell-0.1.0/`, verified its three hashes and all other
  files, then verified exactly eight current manifests including Shell 0.1.1.
  No Console firmware change was needed. Card was safely ejected again.
- **0.1.1 physical launch/prompt stability: PASSED, user-reported.** On
  1 November 2026, after being asked to launch 0.1.1 through Console and check
  that its prompt remained visible beyond 30 seconds, the user confirmed:
  "works as intendede". This accepts the startup repair and library visibility.
  A later SSH investigation independently captured 0.1.1's first-frame and
  successful boot-confirmation markers. No `pwd` output was captured.
- A subsequent SSH attempt crashed during ESP-Hosted initialization because the
  build expected ESP32 instead of the physical ESP32-C6. See
  `docs/TAB5_SHELL_0.1.2_SSH_RADIO_FIX.md` for the observed assert and repair.
  Other feature-specific physical and resource checks remain unverified.

## Package

Local ignored directory: `projects/tab5-shell/artifacts/release-0.1.1/`.
SHA-256:

| File | SHA-256 |
|---|---|
| `sd-library/Tab5Shell-0.1.1.bin` | `cefd5b3c006fd9079eecf7a910381ce76a3b89a1b5f8147e8407cf50c473a8c4` |
| `sd-library/Tab5Shell-0.1.1.json` | `8a03407a63f312bc272a251ea4a904df42a9165ff1d3a64db5f91b61d7b1c2d7` |
| `sd-library/Tab5Shell-0.1.1-LICENSE.txt` | `68783aaaff4d32be73e7a7aab19f98d0d81442977483721b5c11a70e0a20d6e2` |
| `Tab5Shell-0.1.1-SD.zip` | `51c32aae78db7eebb80fc2bab9f4b6bcd10edf76d9637f379beec9bad6dfdc3a` |

Archive CRC, contents and byte comparisons pass; it includes the installer
triplet, hello example, instructions and checksum list.

Install only through Console's SD App Library, explicitly selecting **Tab5Shell
0.1.1**. Observe the local prompt beyond 30 seconds, try `pwd`, and confirm touch
CONSOLE/`exit` return. Keep older packages and all private/app data. No direct
flash, erase, partition/factory/NVS/C6 update or remote push was performed.
Raw host/serial logs remain ignored under the project `artifacts/` directory.
