# TAB5 Ollama Analytics

An independent Ollama dashboard for the M5Stack Tab5. Version **0.1.1** renames
TAB5 AI Usage and opens straight into Ollama Analytics, with the unused provider
pages removed. The original Ollama workflow was accepted on the device on
27 October 2026.

The native 1280x720 dashboard retains the original PixelFonts, light/dark
appearance, colour-coded top five models by 24-hour request count, grey Other
segment, plan balances, quota/reset views and Console return. Light mode uses
neutral greys and whites. Network fetching runs separately from touch/rendering;
only changed regions redraw, and reset countdowns update once per minute.

The five models are sorted by requests. When upstream hidden groups prevent
proving the global top five, the view labels that limitation. The coloured bar
represents request share. Credit plans show included/purchased balances; legacy
plans show session and weekly usage. A session without a reported duration is
not assumed to be 5H, so that meter remains unavailable. The 7D meter uses the
reported weekly limit. See [Ollama API contract](OLLAMA_ACCESS.md).

## Configure and launch

1. Configure Internet Wi-Fi in Console. The app reconnects to the saved station.
2. Make a private copy of `config.example.json`, add your existing Ollama key,
   and save it as SD `/private/tab5-ollama-analytics.json`. The blank template
   makes no requests. The former `/private/tab5-ai-usage.json` is also accepted
   when the new file is absent. An invalid new file does not fall back.
3. Put the matched **Tab5OllamaAnalytics-0.1.1.bin**, `.json` and `-LICENSE.txt`
   files in SD `/firmware`. Use **Console -> Launch from SD Card ->
   Tab5OllamaAnalytics-0.1.1 -> Install + run**.
4. **Refresh** honours cooldown/backoff. **Light/Dark** preserves the existing
   preference in the original `ai_usage` namespace. **Console** returns to the
   resident launcher.

This update keeps the existing key and station settings. The old application
triplet is preserved outside the catalogue in SD
`/firmware/previous/Tab5AIUsage-0.1.0-dev/`.

## Build and verify

From this project directory:

```sh
bash tests/run-native.sh
bash firmware/tools/build.sh build
python3 tools/package_sd_app.py \
  --image firmware/build/Tab5OllamaAnalytics.bin \
  --output artifacts/sd-library \
  --license THIRD_PARTY_NOTICES.md
```

Uses ESP-IDF v5.5.5, ESP-Hosted 1.4.0, Wi-Fi Remote 0.8.5 and the existing
ST7121/exFAT HAL. The shared Weather station component supplies Wi-Fi only.
Retain the revision-v1.3-compatible P4 branch and existing 16 MB partition table.
Direct flash targets are blocked; deploy through the Console SD installer.

ASan/UBSan tests cover strict JSON/config handling, request sorting, hidden
models, stale data, refresh cooldowns, both themes, control targets and dirty
footer isolation. Tests exercise the real HTTPS client with fake transport,
including certificate verification, fixed origins, bearer headers, redirects,
authentication, bounded/chunked bodies, partial success and Retry-After.
Response size/depth limits remain 24 KiB/12. Public fixtures are synthetic.

[Delivery checkpoint](DELIVERY.md) records image identity and SD verification.
On 13 November 2026, the user confirmed the Tab5 firmwares work as intended;
instrumented rollback fault/recovery checks remain a separate evidence category.
Private keys/configuration, live responses, SDKs and build caches are
ignored. See [notices](THIRD_PARTY_NOTICES.md) and `PROVENANCE.json`.
