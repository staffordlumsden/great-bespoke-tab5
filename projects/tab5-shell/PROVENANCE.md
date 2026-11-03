# Provenance

Tab5 Shell 0.1.0 reuses proven Great Bespoke Tab5 components rather than
replacing hardware or terminal infrastructure:

- `firmware/main/hid_translate.{cpp,hpp}` and `firmware/main/input_tab5_kbd.{cpp,hpp}`
  originate in Tab5 QuickNote 0.1.0; Shell renames its local keyboard callback
  type to avoid collision with the SSH byte-stream callback.
- `tools/package_sd_app.py` is copied unchanged from Tab5 QuickNote 0.1.0 and
  retains the Console SD manifest schema.
- The build references `projects/tab5-terminal/firmware/components/term_core/`
  from Tab5 Terminal 0.1.4 for the VT100 parser, screen, and terminal classes.
- The build references the unmodified Tab5 Terminal Mono and PixelFonts atlases
  under `projects/tab5-quicknote/firmware/main/fonts/`.
- Display, SD, touch, USB HID, and application-boot integration use the shared
  components under `projects/tab5-console/components/`.

The new shell parser, path sandbox, line editor, display adapter, boot policy,
application loop, and host tests are original to this project. Third-party
licences are reproduced in `Tab5Shell-LICENSE.txt`.

- `firmware/main/ssh_connection.cpp` is adapted from the proven Tab5 Terminal
  transport. It retains algorithm/authentication/PTY behavior, replaces NVS TOFU
  with read-only SD SHA-256 pins, adds bounded connect/handshake timeouts,
  fixes zero-read/zero-write handling and cooperative cancellation, publishes
  task ownership before spawn, and keeps the TX buffer alive until its producer
  is quiescent. Headers and libssh2 component are referenced without modification.
- `firmware/main/file_store.cpp` adapts QuickNote's flush/fsync/rotate/publish
  pattern to arbitrary sandboxed files, preserving transaction sidecars on error.
  The document model is independent: QuickNote's daily append semantics and
  fixed-size note buffer are unsuitable for editing arbitrary existing files.
- `firmware/components/lua54/` vendors official Lua 5.4.8. See its provenance,
  source hashes and MIT notice. Upstream C/H sources remain byte-identical.
- HTTPS uses ESP-IDF's esp_http_client, mbedTLS certificate bundle, and SNTP;
  no TLS validation bypass or executable download integration is present.

- Shell 0.1.3 references the existing terminal-core 512-row history. The shared
  core adds a cumulative captured-row counter and an explicitly configured
  local viewport height; its default full-screen/alternate-screen exclusions
  remain unchanged. Shell's history viewer, gesture/button mapping and tests
  are local. Its accessory keyboard now accepts a non-blocking raw-key filter
  following the existing Terminal pattern; USB uses the shared HAL filter API.
