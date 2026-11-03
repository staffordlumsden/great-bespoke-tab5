# Tab5 HTML Viewer (`Tab5HtmlViewer` 0.1.1)

Standalone SD-installable HTML Viewer application for the M5Stack Tab5
(ESP32-P4 revision v1.3, ST7121 1280×720 / 720×1280 display and multi-touch
controller). Version 0.1.1 replaces host-licensed document fonts with embedded,
subsetted OFL faces so the complete source is safe to redistribute. See
[0.1.1 notes](../../docs/TAB5_HTMLVIEWER_0.1.1.md).

## Key Features

1. **Separate Full-Screen SD Card HTML File Picker**:
   - Styled with Console `PixelFonts` (`pixelfont_13x7_semibold`, `pixelfont_11x5_thin`) and Dark/Light themes.
   - Quick tabs for **ALL HTML ON SD** (recursive scan for every `.html`/`.htm` file on the card), **SD ROOT (`/`)**, and **`/files`**, plus folder navigation and `CONSOLE` return.
2. **Clean, Unobstructed HTML Reader View**:
   - Avoids overlaying any UI chrome, sidebars, scrollbars, or footers on top of the rendered HTML document.
   - Features only a compact top bar with **`BACK`** and **`ROTATE`** buttons; the entire remainder of the screen is dedicated to the HTML document.
3. **Document-Specified TrueType Typography (`stb_truetype`)**:
   - UI elements use `PixelFonts`.
   - Rendered HTML honors the `font-family` rules in `<style>` blocks, CSS custom properties (`var(--...)`), inline `style="..."` attributes, and `<font face="...">` tags.
   - Generic or unspecified `sans-serif` fonts render in **Source Sans 3**; generic or unspecified `serif` fonts render in **Source Serif 4**; monospace content renders in **Source Code Pro**. Named document families such as Arial, Georgia, Verdana, Times New Roman and Courier New map to the closest embedded OFL family.
4. **Multi-Touch Pinch-to-Zoom & Finger Scrolling**:
   - **Pinch-to-Zoom**: Two-finger pinch/spread scales the HTML document smoothly (`50%` to `250%`) anchored at the midpoint between the fingers.
   - **Finger Scrolling**: Dragging vertically with either one finger or two fingers scrolls the page smoothly.
5. **Landscape & Portrait Rotation (`ROTATE`)**:
   - Cycles 4-way rotation (`0°`, `90°`, `180°`, `270°`) using the shared `tab5_hal` display/touch transform (`1280×720` in Landscape, `720×1280` in Portrait), persists `"rotation"` in the shared `tab5console` NVS namespace, and reflows the HTML document to the active width.
