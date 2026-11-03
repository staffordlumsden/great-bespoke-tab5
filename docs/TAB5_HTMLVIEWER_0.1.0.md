# TAB5 HTML Viewer 0.1.0 — SD Card HTML Reader

## Overview

`Tab5HtmlViewer` (`0.1.0`) is a standalone SD-loadable HTML document viewer for the
M5Stack Tab5 (`projects/tab5-htmlviewer/`). It provides a Console-styled SD card
file picker and a clean, unobstructed HTML reader screen with multi-touch
pinch-to-zoom, finger scrolling, and 4-way Landscape/Portrait rotation.

## Architecture & Key Features

1. **Separate Full-Screen SD Card File Picker**:
   - Uses Console `PixelFonts` (`pixelfont_13x7_semibold`, `pixelfont_11x5_thin`)
     and Dark/Light UI modes.
   - Offers quick-access tabs for **ALL HTML ON SD** (recursively discovering
     every `.html` and `.htm` file on the SD card), **SD ROOT (`/`)**, and
     **`/files`**, along with directory traversal (`UP DIR`), finger drag
     scrolling, **`ROTATE`**, and **`CONSOLE`** return.
2. **Unobstructed HTML Reader View**:
   - No UI overlays, sidebars, scrollbars, or footer bars are drawn over the
     rendered HTML canvas.
   - The top bar contains only **`BACK`** (returning to the previous linked
     HTML page or back to the File Picker screen) and **`ROTATE`**.
3. **Document-Specified TrueType Typography (`stb_truetype`)**:
   - UI elements use `PixelFonts`.
   - Rendered HTML documents honor `font-family` declarations from `<style>`
     rules, `:root` CSS custom properties (`var(--...)`), inline `style="..."`
     attributes, and `<font face="...">` tags:
     - Generic or unspecified `sans-serif` → **Source Sans 3**
     - Generic or unspecified `serif` → **Source Serif 4**
     - `monospace`, `<code>` and `<pre>` → **Source Code Pro**
     - Named document families such as Arial, Georgia, Verdana, Times New Roman
       and Courier New map to the closest embedded OFL family.
4. **Multi-Touch Pinch-to-Zoom & Finger Scrolling**:
   - Uses `tab5_touch_poll_multi` on the ST7121 controller (`0x55`) to track up
     to two simultaneous touch points rotated with `display_rotation`.
   - Two-finger pinch/spread scales the HTML document from `50%` to `250%`
     anchored at the midpoint between the fingers.
   - Dragging vertically with either one finger or two fingers scrolls the page.
5. **Landscape & Portrait View (`ROTATE`)**:
   - Cycles 4-way rotation (`0°`, `90°`, `180°`, `270°`) via
     `tab5_display_set_rotation`, exposing logical `1280×720` in Landscape and
     `720×1280` in Portrait, saving `"rotation"` to the shared `tab5console`
     NVS namespace, and reflowing the HTML layout to the active screen width.

## Build & SD Package Metadata

- **Binary**: `Tab5HtmlViewer-0.1.0.bin`
- **Size**: `1,104,944` bytes (`0x10dc30`)
- **SHA-256**: `2ead8c9f881fcfe9b2c7a87b23bfa0969eb05f03bec6cf2726e9759915faa8bb`
- **Target Chip**: `esp32p4` (min rev v1.0, tested rev v1.3)
- **Host Verification**: `projects/tab5-htmlviewer/tests/run_tests.sh` (`PASS` with AddressSanitizer + UndefinedBehaviorSanitizer)
- **Staged Locations**:
  - `projects/tab5-htmlviewer/artifacts/sd-library/Tab5HtmlViewer-0.1.0.{bin,json}`
  - `<SD_MOUNT>/firmware/Tab5HtmlViewer-0.1.0.{bin,json}`
