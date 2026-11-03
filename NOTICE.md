# Ownership and third-party notices

Unless a file or project states otherwise, publishing this repository does not
grant an additional open-source licence for original Great Bespoke Tab5 project
code. Project-specific licence files take precedence for the files they cover.

Bundled third-party material keeps its original notices and terms:

- `projects/*/components/esp_lcd_st7121/`: M5Stack factory ST7121 driver,
  Apache-2.0 notices; origin and pinned commits are in each `UPSTREAM.md`.
- `projects/*/components/fatfs/`: ESP-IDF FatFs integration and ChaN FatFs.
  ESP-IDF files use Apache-2.0 where marked; FatFs source carries its own
  redistribution notice. Local changes are documented in each
  `TAB5_OVERRIDE.md`.
- `projects/tab5-htmlviewer/third_party/fonts/`: Source Sans 3, Source Serif 4
  and Source Code Pro, licensed under the SIL Open Font License 1.1. The license,
  pinned source commits and file hashes are retained in that directory.
- `projects/tab5-htmlviewer/Tab5HtmlViewer-LICENSE.txt`: application and
  third-party notices for Tab5 HTML Viewer 0.1.0.
- `projects/tab5-console/third_party/PixelFonts/LICENSE`: PixelFonts by
  mcer12, MIT license, pinned at upstream commit
  `344a6cfcd660b8546258485befb7576a47790d7d`; the atlas is
  `projects/tab5-console/main/fonts/pixel_fonts.h`.
- `projects/tab5-terminal/`, `projects/tab5-shell/`, and their nested
  `THIRD_PARTY_NOTICES` or licence files: portable-terminal, libssh2, Lua,
  font and other upstream notices.
- The B1 keyboard mapper/validator is reused from the Great Bespoke CYD
  Contextual Micro project; see `cyd_keyboard/PROVENANCE.md`.
- `companions/cyd-usb-drive/`: original Great Bespoke CYD USB Drive firmware,
  shared components and Mac bridge. The snapshot retains the PixelFonts MIT
  licence and the Go dependencies' licences. See
  `projects/usb-drive/THIRD_PARTY_NOTICES.md`.

The Apache-2.0 text is retained at `third_party/licenses/Apache-2.0.txt`.
Registry dependencies are fetched by ESP-IDF using the checked-in manifests and
lock files; their managed source trees are not republished in this repository.

The separate [Tab5 Media Player](https://github.com/staffordlumsden/Tab5-Media-Player)
is a Console-skin fork of
[Hiroki-Kawakami/Tab5-Media-Player](https://github.com/Hiroki-Kawakami/Tab5-Media-Player).
It retains the upstream MIT copyright and its third-party notices.
