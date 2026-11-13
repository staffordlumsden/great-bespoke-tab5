# TAB5 Ollama Analytics 0.1.1 - 27 October 2026

TAB5 AI Usage has been renamed to **TAB5 Ollama Analytics**. The source project
is now `projects/tab5-ollama-analytics/`, the ESP-IDF project is
`Tab5OllamaAnalytics`, and the visible header is `TAB5 Ollama Analytics`.
The unused provider pages, navigation/state and generic multi-provider parsing
surfaces have been removed. The app opens directly into the Ollama dashboard.

## Verified candidate

- Version: **0.1.1**.
- Image: **1,091,472 bytes**.
- SHA-256: `ae508c1fe5771e39fc428adc9dad176f2f0ecf57623f822bf040deac94740ed0`.
- ESP32-P4, 16 MB flash, minimum revision 1.0, compatible with the tested v1.3;
  fits the existing 5 MiB SD application slot.
- Package: `Tab5OllamaAnalytics-0.1.1.bin`, `.json`, `-LICENSE.txt`.

The pinned ESP-IDF v5.5.5 build and native ASan/UBSan model/config/UI/HTTPS tests
pass. Both themes were visually checked through the actual native renderer.
The new display title is present in the binary; removed provider titles and
sign-in placeholders are absent. Image identity, provenance hashes and the
SD-only flash guard also pass.

The accepted Ollama network/parser flow is preserved. This rename keeps the
original NVS theme namespace `ai_usage`, the station profile and the existing
Ollama key. The canonical SD profile is `/private/tab5-ollama-analytics.json`;
the old `/private/tab5-ai-usage.json` remains a missing-file compatibility
fallback. An invalid new profile does not select the old one.

## SD delivery

The mounted card was independently identified as the writable 64 GB exFAT
Tab5 card through its physical size and matching existing AI Usage/Weather
image digests. The new triplet and canonical private profile were copied,
flushed and read back byte-for-byte.

The old `Tab5AIUsage-0.1.0-dev` triplet was preserved byte-for-byte under
`/firmware/previous/Tab5AIUsage-0.1.0-dev/`, outside the launcher's catalogue.
The visible catalogue now contains Weather 0.1.2 and Ollama Analytics 0.1.1.
The previous key file and Weather image remain unchanged; all previously
present data files were compared after staging/archive in the copy pass.
Raw receipts and private profiles remain ignored under `artifacts/`.

The original project source/release/build were preserved locally under ignored
`backups/2026-10-27-ollama-analytics-rename/` before the directory rename.
The resident Console, OTA slots, NVS, C6 firmware and SD format were not written.

## Install and acceptance

Return the card to the Tab5. In Console, choose **Launch from SD Card ->
Tab5OllamaAnalytics-0.1.1 -> Install + run**.

The user accepted the original Ollama workflow on 27 October 2026. At this
checkpoint version 0.1.1 had been staged to SD but not installed by that update.
On 13 November 2026, the user confirmed the Tab5 firmwares work as intended.
Instrumented first-boot rollback/recovery checks remain separate.
