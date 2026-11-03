# GitHub Pages site

The public project site is a dependency-free static page served from this
directory by GitHub Pages.

Local preview:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs
```

Then open <http://127.0.0.1:8765/>. The page uses the screenshots in
`images/screenshots/` and local PixelFonts webfonts in `fonts/webfonts/`. It
does not load fonts, scripts or styles from a third-party CDN.

Original page source and content are Copyright (c) 2026 Stafford Lumsden and
licensed under MIT. PixelFonts remains Copyright (c) 2026 mcer12 under MIT; see
`fonts/PixelFonts-LICENSE.txt`.
