# TAB5 HTML Viewer 0.1.1 — redistributable embedded fonts

3 November 2026. Version 0.1.1 is a source-publication maintenance release for
Tab5 HTML Viewer.

## Changes

- Replaced the generated font atlas that embedded host-installed Times New Roman,
  Arial, Georgia, Verdana and Courier New faces.
- Embedded subsetted Source Sans 3, Source Serif 4 and Source Code Pro faces
  instead. These families are licensed under the SIL Open Font License 1.1.
- Vendored the exact static font inputs, their hashes and pinned upstream commit
  references under `projects/tab5-htmlviewer/third_party/fonts/`.
- Made `tools/build_html_fonts.py` independent of host font locations and
  machine-specific Python imports.
- HTML font-family names continue to be honoured. Generic sans-serif uses
  Source Sans 3, generic serif uses Source Serif 4, and monospace content uses
  Source Code Pro. Named proprietary families map to the nearest embedded OFL
  family.

## Validation

- Host unit and sanitizer suite: PASS.
- ESP-IDF v5.5.5 ESP32-P4 build: PASS.
- Binary size: 950,192 bytes.
- Binary SHA-256:
  `d1abf2477a012e365e296c037da61c7514403618c5a3669bea240ea1d05b581e`.
- The public-release scanner and `gitleaks` pass for this repository state.

At this checkpoint the 0.1.1 image had not yet been installed on a physical
Tab5. On 13 November 2026, the user confirmed that the Tab5 firmwares work as
intended. The confirmation did not enumerate each rendering or input scenario.
