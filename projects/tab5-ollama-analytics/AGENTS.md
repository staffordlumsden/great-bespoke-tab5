# TAB5 Ollama Analytics

Read `README.md`, `OLLAMA_ACCESS.md` and repository `AGENTS.md` first.

- Scope is Ollama only, over direct Internet HTTPS. The dashboard is a single
  screen; the previous provider tabs and account-linking placeholders are removed.
- Preserve the accepted top-five model view, distinct segment colours, grey
  Other segment, actual plan balances/limits and missing-data handling. Keep the
  hidden-group ranking check; spend-selected groups may not prove global rank.
- Keep the 5H/7D views honest: a session with no reported duration is not a
  verified five-hour quota. Unknown stays unavailable rather than zero.
- The original on-device Ollama workflow was accepted on 27 October 2026.
  Version 0.1.1 renames the app and removes the unused providers. Its updated
  image needs separate on-device observation after SD installation.
- Canonical SD profile: `/private/tab5-ollama-analytics.json`. The former
  `/private/tab5-ai-usage.json` is a compatibility fallback only when the new
  file is absent. An invalid new file must not silently use an older key.
- Retain NVS namespace `ai_usage` to preserve the existing light/dark preference.
  Never erase shared NVS or log keys/account payloads. Private profiles and live
  captures belong under ignored `artifacts/` locally.
- Retain PixelFonts, neutral grey/white light mode, readable controls, dirty
  regions and minute-based countdown updates. No whole-screen timer redraws.
- Preserve the existing Tab5 HAL, ST7121/exFAT handling, Hosted pins and 16 MB
  partition map. Power the C6 before station APIs. BLE remains out of scope.
- This is an ESP32-P4 SD app using `tab5_app_boot`. Confirm boot only after
  critical checks and first-frame presentation; provide Console return.
  Never flash this app over factory Console.
- Build: `bash firmware/tools/build.sh build`. Native checks:
  `bash tests/run-native.sh`. Package with `tools/package_sd_app.py`.
- Confirm the actual SD card before copying. Preserve the old triplet in
  `/firmware/previous/` and verify package/profile bytes after flushing writes.
  Host/build checks and SD staging do not prove on-device acceptance.
