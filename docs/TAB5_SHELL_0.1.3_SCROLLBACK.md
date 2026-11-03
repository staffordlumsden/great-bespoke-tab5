# Tab5 Shell 0.1.3 — window scrollback

1 November 2026. Source commit:
`cff775497b0986a94c786f871b06b68df6fa09b6`.

## Result

The user reported that long responses disappeared above the window. The shared
VT100 core already retained 512 rows, but Shell exposed no history view.
0.1.3 adds a native viewer over that existing bounded buffer for local commands
and connected SSH normal-screen output.

- **Shift+PageUp/PageDown** pages older/newer output; **Shift+Home** selects the
  oldest retained row and **Shift+End** returns live. Plain PageUp/PageDown also
  work at the local prompt. Tab5 I2C and USB-A raw-key filters share the mapping.
- Footer **UP/DOWN/LIVE** buttons and vertical swipes provide touch navigation:
  drag down for older text, up for newer. Keyboard-overlay touches are excluded
  from swipe recognition.
- The amber `SCROLLBACK offset/rows` indicator identifies history mode. The live
  cursor is hidden there; typing another key returns live and handles that key
  normally. Up/Down command history and remote plain PageUp/PageDown are retained.
- Incoming output continues receiving. A cumulative captured-row counter anchors
  the displayed page even when the 512-row ring is full. If its rows are evicted,
  the view clamps to the oldest retained rows. No periodic viewport repaint.
- Local output above the optional touch keyboard also enters history. The core
  accepts an explicitly configured top-aligned local viewport height; default
  full-screen/alternate-screen exclusions remain intact. Remote alternate-screen
  TUIs and local editor/viewer receive their own navigation instead of Shell's
  history. Remote PTY dimensions remain 80x24.

History remains RAM-only, bounded to 512 rows, and clears on app reset or the
existing fresh-local-screen path after SSH disconnect. The existing 16 KiB local
command output limit remains. This is not an unlimited or persistent output log.
The lazy reusable view scratch allocation is 46,080 bytes; runtime heap/PSRAM
peaks and stack margins remain unmeasured.

## Verification

- `bash projects/tab5-shell/firmware/tests/run_checks.sh`: **PASS**, seven
  ASan/UBSan native binaries and project contracts. Scrollback target additionally
  rerun after the final gesture arithmetic cleanup: **PASS**.
- Tests use real temporary-mount Shell `cat` output exceeding one screen, real
  VT100 buffers, oldest/live navigation, 512-row saturation, continued output
  anchoring, eviction/clamping, alternate-screen exclusion, history clearing,
  raw modifier mappings, touch bounds/swipes, explicit cropped local capture,
  and default partial-region exclusion.
- Real ShellDisplay/font startup tests cover the new footer/history label and
  long diagnostic bounds. First-frame presentation protection remains checked.
- `bash projects/tab5-terminal/tests/run-native.sh`: **PASS**, HID/scrollback
  regressions, contract and all **59/59** untouched upstream VT100 tests. Updated
  the source contract for the explicit crop opt-in while retaining its default
  history exclusion check.
- In `projects/tab5-shell/firmware`, `../../../idf.py build`: **PASS**, including
  rebuild after the source commit, ESP-IDF 5.5.5, P4 pre-v3/minimum revision 100,
  factory C6 target/pins and dependency locks retained.
- Packager/release validator: **PASS**, image header/revision/checksum/SHA-256,
  manifest/licences and malformed-image rejection. Image **1,405,424 bytes**;
  actual OTA slot **5,242,880 bytes**; headroom **3,837,456 bytes**.
- SD triplet copied byte-identically; active catalog **8/8**. Retained **0.1.2**
  as the latest rollback and removed older 0.1.1 SD files only after a verified
  local recovery copy. Other firmware and the private SSH pin file were compared
  and unchanged. SD safely ejected. Raw logs/audits remain ignored/local.
- **Physical scrollback/navigation acceptance: pending operator check.** Prior
  0.1.2 startup/SSH acceptance is separate from this exact package.

## Release files

Local ignored directory: `projects/tab5-shell/artifacts/release-0.1.3/`.

| File | SHA-256 |
|---|---|
| `sd-library/Tab5Shell-0.1.3.bin` | `2f7029e3e96d2c600dac4bd4ab354b80e6178b9605b20d6f3a3cfa8e5acfafc2` |
| `sd-library/Tab5Shell-0.1.3.json` | `24b6d849e84cbfedcf97938e748371fab417863d38eee06d636edb9da076efcd` |
| `sd-library/Tab5Shell-0.1.3-LICENSE.txt` | `10b44ae892f46a094978d29f09bec8236394883fe13a6160ee76e595338f18a7` |
| `Tab5Shell-0.1.3-SD.zip` | `3825d91d5ec213687f833953a0c1bd316e1ad469edff1c21d56767b1deead684` |

Archive **819,432 bytes**; CRC/contents/byte checks pass. Contains installer
triplet, hello example, instructions and checksum list; no private pin/profile.

Install through Console SD App Library, REFRESH/DOWN and select **Tab5Shell
0.1.3**. Verify first frame, long local and SSH responses, keyboard/footer/swipe
paging, live cursor restoration, touch-keyboard layout, incoming-output anchoring
and editor/remote-TUI navigation. Confirm ordinary SSH input and Console return.
No host flash, factory/table/NVS/C6 update, SD format or remote push performed.
