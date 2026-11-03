# Tab5 HTML Viewer — Agent Handoff

## Build & Host Verification

```sh
bash projects/tab5-htmlviewer/tests/run_tests.sh
cd projects/tab5-htmlviewer/firmware && ../../../idf.py build
python3 projects/tab5-htmlviewer/tools/package_sd_app.py \
  --image projects/tab5-htmlviewer/firmware/build/Tab5HtmlViewer.bin \
  --output projects/tab5-htmlviewer/artifacts/sd-library \
  --license projects/tab5-htmlviewer/Tab5HtmlViewer-LICENSE.txt
```

## Safety Rules

- `Tab5HtmlViewer` is an SD-loadable application (`project(Tab5HtmlViewer)`). Never flash it over the factory Console slot.
- Retain `PixelFonts` for UI elements and TrueType document fonts (`Montserrat` for sans-serif, `Times New Roman` for serif) for rendered HTML.
- Keep the HTML Reader screen free of overlaid UI, sidebars, or scrollbars — only `BACK` and `ROTATE` in the top bar, with pinch-to-zoom and finger scrolling.
