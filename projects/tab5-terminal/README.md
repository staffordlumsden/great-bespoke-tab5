# TAB5 Terminal

TAB5 Terminal 0.1.4 is an SD-installable, zoomable SSH terminal for Great
Bespoke Tab5. It ports the hardware-independent VT100 core from
[`ciniml/portable_terminal`](https://github.com/ciniml/portable_terminal) onto
the repository's proven ST7121/exFAT HAL and factory-compatible C6 stack.

The display is intentionally fixed at 1280×720 landscape. The M5Stack Tab5
Keyboard accessory is the primary input device; a conventional USB-A HID
keyboard remains available as a fallback. Both paths translate letters,
numbers, common ASCII punctuation, Ctrl/Alt modifiers, navigation keys and
terminal escape sequences. The accessory uses I2C address `0x6D` on ExtPort1
(`SDA GPIO0`, `SCL GPIO1`, active-low `INT GPIO50`) and supplies software key
repeat.

Version 0.1.1 enlarged every sidebar state to the same PixelFont size used for
Console labels and distinguished HID enumeration from actual key input. The
device then completed a live LAN SSH session with USB-A input; `whoami`,
`asciiquarium` and `openclaw tui` all ran successfully. That physical test also
showed that the 1× terminal glyphs were still too small and that shell output
needed scrollback.

Version 0.1.2 therefore defaulted to a **60×22 terminal** and added 80×30 and
40×15 zoom levels with matching remote SSH PTY resize. A bounded 512-row local
history records only full primary-screen upward scrolls; alternate-screen TUIs
and partial DEC scroll regions do not contaminate shell history.

Version 0.1.3 keeps those proven grid sizes but replaces PixelFonts in terminal
cells with **Tab5 Terminal Mono**, a conventional fixed-width bitmap conversion
derived from Source Code Pro under the SIL Open Font License 1.1. The readable
default is nominally **10 pt**; compact and large modes use 8 pt and 15 pt.
Regular and real bold atlases are rasterised separately. PixelFonts remain in
the title, status, controls and footer. Normal output still uses damage-region
updates, so clock/network polling never repaints the full terminal.

Physical testing then exposed that the 0.1.3 renderer accepted only printable
ASCII and substituted `?` for the Unicode box-drawing and block characters used
by modern terminal applications. Version 0.1.4 extends all six regular/bold
atlases with the complete U+2500–U+25A1 terminal range. It also implements
VT100 DEC G0/G1 Special Graphics designation and SI/SO selection for older
curses applications. Literal ASCII `-`, `_` and `|` remain independently
verified in the atlas and parser path.

### Connected layout

The full Wi-Fi/C6/SSH/input sidebar remains visible while the app starts and
connects. On the transition to `SSH CONNECTED`, it collapses to a 56-pixel
status rail and the terminal is centred across the reclaimed area without
changing the grid or remote PTY dimensions. Tap the rail or header **INFO**
control to reopen it. A disconnect, connection failure or reconnect restores
the full sidebar automatically.

### Readability and scroll controls

- **Ctrl + `+`** or **Ctrl + `=`**: increase terminal text size.
- **Ctrl + `-`**: decrease terminal text size.
- **Ctrl + `0`**: return to the readable 60×22 / 10 pt default.
- Header **A− / A+** buttons: touch equivalents for zoom.
- **Shift + Page Up / Page Down**: move through local shell history.
- **Shift + Home / End**: oldest available history / live screen.
- Swipe down/up inside the terminal: older/newer history.

Typing an ordinary key while viewing local history returns to the live screen.
When an alternate-screen program such as `openclaw tui` is active, the page
commands are sent to the remote application instead of opening shell history.

## Safety and C6 compatibility

The original upstream project targets ESP-IDF 6.0, ESP-Hosted 2.x and includes
a C6 updater. This port deliberately does not use that update path. It builds
with ESP-IDF 5.5.5, `esp_hosted` 1.4.0 and `esp_wifi_remote` 0.8.5 to match the
Tab5 factory C6 firmware. At runtime it reads the coprocessor version and only
continues when the reported line is 1.4.x; it logs `action=NONE` and contains no
C6 update call. The upstream updater directories are retained under
`upstream/` solely to preserve an auditable snapshot.

### Tailscale boundary

The imported upstream snapshot contains a bespoke Tailscale/WireGuard client,
but it is **not compiled into this factory-C6-compatible 0.1.x port**. A
Tailscale `100.x` address is therefore not reachable directly from this image.
Use the SSH host's ordinary LAN address for acceptance testing, or connect to
an on-LAN machine that already belongs to the tailnet and continue from that
shell. The upstream Tailscale path expects its ESP-IDF 6 / ESP-Hosted 2.x
environment; it must be adapted and tested separately rather than silently
updating this Tab5's factory C6 1.4.x firmware.

The image is an SD application, not a replacement for the resident Console.
Its normal ESP-IDF flash targets are blocked. Console validates the ESP32-P4
image and writes it to an inactive OTA slot without changing the factory
Console, NVS, partition table or C6.

## Private configuration

Copy one example to SD `/private/tab5-terminal.json` and replace only on the
card:

- `config.example.json`: password authentication;
- `config.public-key.example.json`: RSA private-key authentication, with the
  key stored separately below SD `/private`.

Version 0.1.x loads exactly one active profile. To reach several machines
without changing the card, use a trusted SSH jump host as that profile and run
`ssh other-host` from its shell. The imported upstream snapshot has an NVS
multi-profile store and picker, but those UI/storage components are not yet
ported to the ST7121 HAL; direct named-target selection is a later milestone.

Configuration is bounded and never logged. Host identity uses SHA-256 TOFU in
NVS; a changed key is rejected. The trust store fails closed if unavailable.
The sample values are placeholders and no working credential is committed.
SSH negotiation is restricted to the algorithms already proven by Contextual
Micro on this ESP-IDF/libssh2 stack: ECDH or group14-SHA256 key exchange,
ECDSA P-256 host keys, AES-CTR encryption, HMAC-SHA2-256, RSA-SHA2 user-key
signatures and no compression. SHA-1 and CBC fallbacks are not enabled.

## Build and package

From this project directory:

```sh
bash tests/run-native.sh
bash firmware/tools/build.sh build
python3 tools/package_sd_app.py \
  --image firmware/build/Tab5Terminal.bin \
  --output artifacts/sd-library \
  --license THIRD_PARTY_NOTICES.txt
```

The committed `terminal_fonts.h` is generated offline from the regular and bold
TTFs at the pinned Source Code Pro commit recorded in `PROVENANCE.json`.
`firmware/tools/generate_terminal_fonts.py` verifies both input SHA-256 values
before producing the renamed Tab5 Terminal Mono bitmap subset; firmware builds
do not need Pillow, the source TTFs or a network connection.

Copy the resulting matched triplet to SD `/firmware`:

```text
Tab5Terminal-0.1.4.bin
Tab5Terminal-0.1.4.json
Tab5Terminal-0.1.4-LICENSE.txt
```

Then use **Console → Launch from SD Card → Tab5Terminal-0.1.4 → Install + run**.
Reset, power-cycle, or the on-screen **Console** button returns to the resident
launcher according to the shared app-boot contract.

`upstream/` is the exact tracked snapshot of upstream commit
`44a616ee333242e0fd3181d5771ead55ded58253`; `UPSTREAM.md` documents the fork
and compatibility decisions. Generated SDKs, build trees, secrets and private
configuration remain local and ignored.

## Verification boundary

Host tests cover the imported terminal core and static delivery contract. An
ESP-IDF build proves compilation for ESP32-P4 and the package tool verifies the
image header, revision range, 16 MB declaration, checksum and appended SHA-256.
The 0.1.1 physical run established display, saved Wi-Fi, USB HID and live SSH.
The 0.1.3 device run exposed the missing line-graphics coverage, addressed in
0.1.4. On 13 November 2026, the user confirmed the Tab5 firmwares work as
intended. See `DELIVERY.md` for the earlier detailed evidence and checklist.
