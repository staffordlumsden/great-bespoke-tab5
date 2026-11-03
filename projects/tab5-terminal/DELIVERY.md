# TAB5 Terminal 0.1.4 delivery checkpoint

This file separates completed host evidence from physical acceptance.

## Host evidence

- Upstream fork created and tracked snapshot imported.
- Upstream VT100 host suite: 59/59 passed from the imported snapshot.
- HID translation suite: passed under AddressSanitizer and
  UndefinedBehaviorSanitizer for printable symbols, modifiers, arrows and
  terminal escape sequences.
- Static terminal safety/delivery contract: passed.
- Local scrollback suite: passed under AddressSanitizer and
  UndefinedBehaviorSanitizer, including the 512-row bound, primary/alternate
  screen separation, partial scroll regions and resize preservation.
- ESP-IDF 5.5.5 build: passed for 0.1.4.
- Target dependencies: M5Stack Tab5 1.3.2, ESP-Hosted 1.4.0,
  Wi-Fi Remote 0.8.5 and USB Host HID 1.2.1.
- App image: ESP32-P4, 16 MB flash declaration, 1,147,776 bytes.
- C6 policy: runtime version query only; `action=NONE`; no update API.
- C6 compatibility evidence: this unit's previously captured factory build is
  `ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a`; the current live Console 0.3.0
  check again completed ESP-Hosted initialization and joined Wi-Fi. The exact
  terminal runtime line remains a physical acceptance item below.
- Orientation: fixed landscape rotation 0; no terminal rotation control.
- Input: Tab5 Keyboard I2C primary, USB-A HID fallback.
- Input diagnostics: enumeration and first received key byte are reported as
  separate states; offline key tests never display the typed character.
- Sidebar: all section/value text uses the Console-label PixelFont baseline.
  It automatically collapses to a 56-pixel status rail after SSH connects,
  restores after disconnect/failure and can be reopened without PTY resize.
- Terminal text: dedicated OFL-licensed Tab5 Terminal Mono fixed-width atlases;
  readable 60×22 defaults to 10 pt, compact 80×30 uses 8 pt and large 40×15
  uses 15 pt. UI chrome remains PixelFont. Ctrl and touch zoom still propagate
  SSH PTY window size.
- Character coverage: printable ASCII plus Source Code Pro U+2500–U+25A1 box,
  block and geometric terminal glyphs in every size/weight. The local suite
  separately verifies literal `-`, `_`, `|`, UTF-8 box characters and VT100
  DEC G0/G1 Special Graphics with SI/SO selection.
- Scrolling: bounded 512-row primary-screen history with Shift navigation and
  vertical touch swipes; alternate-screen page actions go to the remote TUI.
- SSH: Contextual Micro algorithm policy, bounded private-key loading,
  SHA-256 TOFU and fail-closed trust storage.
- Network scope: ordinary routed LAN SSH only. The preserved upstream
  Tailscale/WireGuard sources are not compiled into this factory-C6-compatible
  build, so direct tailnet `100.x` targets are not yet supported.

## Delivery package

- Package name: `Tab5Terminal-0.1.4`
- Image size: 1,147,776 bytes
- Image SHA-256:
  `f316a17e2e835b172f2f5252024fb5cc8ae0d7e6353aa0639ed51dfebff27c58`
- Host package validation: passed.
- Byte-for-byte SD copy verification: passed on 29 October 2026 against the
  mounted removable 63.8 GB exFAT volume. The `.bin`, `.json` and license match
  the host package. Three package-specific AppleDouble sidecars were moved into
  ignored local recovery and the SD package directory rechecked clean. The
  verified 0.1.2 and 0.1.3 triplets remain on SD as rollback; no unrelated SD
  file was changed.
- The existing `/private/tab5-terminal.json` passed a redacted schema check for
  password authentication. No host, username or credential value was printed,
  copied into the repository or included in the package.

## Connected-device preflight

- USB ROM identity: ESP32-P4 revision v1.3.
- Resident image: TAB5 Console 0.3.0.
- Physical display probe: ST7121 firmware 1.
- SD: exFAT, 60,906 MiB capacity, mounted successfully by Console.
- C6: powered and initialized over ESP-Hosted; saved Wi-Fi reconnected.
- No P4 or C6 flash write was performed during this preflight.

## Physical evidence through 0.1.1

- The exact 0.1.0 package launched and produced a complete readable frame.
- The factory C6 was reported without an update attempt; saved Console Wi-Fi
  connected and supplied an IP address.
- The USB-A HID device enumerated, but no private SSH profile was present.
  Because 0.1.0 forwarded keys only to a live SSH connection and had no local
  input diagnostic, this did not establish that key reports reached the app.
- The sidebar was too small on the physical five-inch display, and its static
  boot copy misleadingly continued to say it was waiting for Wi-Fi.
- Version 0.1.1 booted successfully, rejoined saved Wi-Fi, connected to the
  configured LAN SSH host and accepted USB-A keyboard input.
- `whoami`, `asciiquarium` and `openclaw tui` all ran successfully in that live
  session.
- The enlarged sidebar was usable, but 1× terminal text remained much too small
  relative to the sidebar and there was no local scrollback navigation.

## Physical evidence from 0.1.3

- The device reached the terminal after the 0.1.3 install, allowing physical
  inspection of the new mono face.
- Unicode/terminal line graphics appeared as `?`. Source inspection confirmed
  that 0.1.3 deliberately replaced every non-ASCII codepoint with `?`, while
  host inspection confirmed that the literal ASCII `-`, `_` and `|` bitmaps
  themselves were present. This is the defect repaired by 0.1.4.

## Physical acceptance still required for 0.1.4

- first complete ST7121 frame and successful app-boot confirmation;
- connected Unicode and DEC box drawing, block/shade characters, literal
  `-`, `_`, `|`, punctuation distinction and real bold rendering at the 60×22
  / 10 pt default;
- 80×30 / 8 pt and 40×15 / 15 pt zoom transitions;
- automatic sidebar collapse after SSH connection, INFO/rail reopening and
  full-sidebar restoration after disconnect without an unsolicited PTY resize;
- Ctrl `+`, `-`, `0` and touch A−/A+ operation with remote TUI redraw;
- primary-screen Shift/touch scrollback and return-to-live behaviour;
- remote Page Up/Page Down behaviour while `openclaw tui` owns the alternate
  screen;
- no periodic full-screen repaint or radio-strength flicker;
- Tab5 Keyboard accessory typing, symbols, Ctrl/Alt, arrows and repeat;
- reset/Console return.

No item in this section should be marked complete from a host build alone.
