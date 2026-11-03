# Embedded document font provenance

Tab5 HTML Viewer embeds glyph subsets generated from these OFL-licensed Adobe
font releases. The original files are retained for reproducible regeneration;
only these static faces are used:

| File | SHA-256 |
|---|---|
| `SourceCodePro-Bold.ttf` | `b2095e0d657e6d28dc32444a9dacabab0c9241d0bf39d96371756cc9bdbc3a5f` |
| `SourceCodePro-Regular.ttf` | `74bd80d3e42a08517cd7e1108ba3d86f2da29ac0f3065be95e0357956ab9db37` |
| `SourceSans3-Bold.ttf` | `9214b9d95e4231c609802815c2646c98174e2102d0d37f88978a7f8e71006e6a` |
| `SourceSans3-BoldIt.ttf` | `7978291fc1bf314db887e0366853b33c5cf2e964c7b95cfb9ce403a6ec46a842` |
| `SourceSans3-It.ttf` | `192afd78f0f54a3c69eaf02d43f4d9a821e9d6110e41d3d25d61a7385cd580e4` |
| `SourceSans3-Regular.ttf` | `4644c81b86ec9caaa76b634889968ed3c4f4f52f054855933acc7c2b21e53b0f` |
| `SourceSerif4-Bold.ttf` | `7cf4f4e1ad74f45058d5bc61716b82560442fbdcd9d3654d2dea96bf6c683d86` |
| `SourceSerif4-BoldIt.ttf` | `7b215b37f8873f5579f3f8d2ded3ca7c588e2f435cd996605ebfc5befe2cd5eb` |
| `SourceSerif4-It.ttf` | `9d2950a8f1da66e21502c35d646a1d2148e79f9ea43fd2158cf02f5232e7f430` |
| `SourceSerif4-Regular.ttf` | `e5a4ee6a3d87bb9024796be390c6771e2a0eb1883dae25effaf57ca01668e24b` |

## Sources

- Source Sans 3: `https://github.com/adobe-fonts/source-sans`, branch `release`,
  commit `87b37a2daaed80fcb8e8ccb0085c4d72ddade12e`, `TTF/`.
- Source Serif 4: `https://github.com/adobe-fonts/source-serif`, branch `release`,
  commit `80d3f8894c09c937bebfa9011247d2e1c79fd6f4`, `TTF/`.
- Source Code Pro: `https://github.com/adobe-fonts/source-code-pro`, branch
  `release`, commit `803b7e23ec97ae58b6232ea76519a76d428ba268`, `TTF/`.

`OFL.txt` is the SIL Open Font License 1.1 text distributed with Source Sans 3.
All three families use the same license terms.

Regenerate `firmware/main/fonts/embedded_html_fonts.h` from the repository root:

```sh
python3 projects/tab5-htmlviewer/tools/build_html_fonts.py
```
