# Tab5 Shell 0.1.3

A native, local-first SD application for the M5Stack Tab5. Starts at an 80x24
prompt, with C6 unpowered and no network/profile dependency. Uses Tab5 Terminal
Mono for terminal cells, PixelFonts for chrome, the landscape ST7121 HAL,
ExtPort1 Tab5 keyboard and independent USB-A HID fallback.

**0.1.3 adds keyboard/touch scrollback for local and SSH output.**
See `docs/TAB5_SHELL_0.1.3_SCROLLBACK.md` for release and validation status.

0.1.2 corrected the ESP32-C6 build selection that crashed 0.1.1 on SSH startup.
The user confirmed a successful SSH connection on 1 November 2026 after verified
server host-key pin setup.
See `docs/TAB5_SHELL_0.1.2_SSH_RADIO_FIX.md` for verification status.

0.1.1 repaired a reproduced startup drawing failure in 0.1.0. The 0.1.0
package failed physical acceptance (black screen and automatic Console return).
The user physically accepted 0.1.1 startup/prompt stability on 1 November 2026.
See `docs/TAB5_SHELL_0.1.1_STARTUP_FIX.md` for current verification status.

## Local commands and files

`help clear pwd cd ls mkdir cat echo touch rm df free exit`

Quotes, escaped spaces, history, insertion/delete, Home/End, readline
Ctrl+A/E/K/U/W and Ctrl+C input cancellation are supported. Long command input
scrolls horizontally within one row. Commands are limited to 512 bytes and local
command output to 16 KiB. Execution is synchronous; Ctrl+C does not interrupt
an already executing local command. `cat` filters C0/DEL terminal control bytes.
There are no pipelines, redirection, recursive `rm`, shell expansion or Linux
binaries.

### Window scrollback

The window retains the latest **512 primary-screen rows** for local commands
and connected SSH sessions. Use **Shift+PageUp/PageDown** to page,
**Shift+Home** for the oldest retained row and **Shift+End** to return live.
Plain PageUp/PageDown also work at the local prompt. Tap footer **UP/DOWN/LIVE**
or drag **down** over terminal text to see older output and **up** for newer.
The footer shows `SCROLLBACK offset/rows`; the live cursor is hidden while
viewing history. Typing another key returns live and sends/edits that key normally.

Incoming SSH output continues to be received. The viewed page stays anchored
while those rows remain in the bounded buffer; once evicted, the view advances
to the oldest retained rows. No periodic viewport repaint is introduced.
History is also retained for the shorter local console above the touch keyboard.
Local editor/viewer and remote alternate-screen TUIs keep their own navigation;
page controls are routed to them. Session disconnect returns to a fresh local
screen and clears that session's history as before. History is RAM-only and is
not preserved across reset. The 16 KiB local-command output limit is unchanged.

`/home` maps to `/sdcard/files/Tab5Shell/` and is the sole writable mount.
`/sd` is read-only; `/sd/private` is denied, including case variants. Paths reject
traversal outside mounts, symlinks, embedded controls, FAT aliases made with
trailing dots/spaces, backslashes and drive separators. SD failure leaves the
diagnostic prompt usable. Firmware and other app directories are never written.

## Editor and Markdown

- `nano FILE`: native full-screen editor. Ctrl+O/S saves; Ctrl+W searches;
  Ctrl+G goes to a line; arrows/Home/End/PgUp/PgDn navigate; Ctrl+K cuts a line;
  Ctrl+U pastes; Ctrl+X leaves, prompting when dirty. `n` leaves a recovery draft.
- `nano --recover FILE`: explicitly opens an existing draft or interrupted-write
  copy, with dirty state. Recovery files prevent a normal open from silently
  replacing unsaved work. Inspect them before saving.
- `glow FILE`: offline heading/list/code-fence formatting, wrapping and scrolling
  through the existing VT100 renderer. Arrows/j/k/PgUp/PgDn/Space/Home/End scroll;
  `q` exits. No remote service or external executable is involved.

The editor supports ASCII/LF text up to 64 KiB. Binary, UTF-8 and CRLF input is
rejected without changing the file. Markdown displays ASCII; unsupported bytes
become `?`. Full Unicode editing/rendering, inline Markdown styling, tables and
images are incomplete. These are bounded native tools, not Nano/Glow ports.

Writes flush and fsync a `.tab5-new` sibling, rotate the original to
`.tab5-prev`, then publish the replacement using FAT-compatible renames. Failed
transactions preserve recoverable copies and block another overwrite. Dirty
editor text checkpoints every two seconds while editing, and before leaving or
returning to Console, as `FILE.tab5-draft`. Power loss can leave that draft's
own `.tab5-new`/`.tab5-prev` siblings. `--recover` considers all of these.
Inspect with `cat`, preserve needed copies externally, then remove only stale
sidecars with `rm`; saving is intentionally blocked by unresolved transaction
files. There is no guarantee of FAT metadata atomicity under sudden power loss.
The editor detects external changes before replacing the original.

## Optional SSH and Wi-Fi

```
ssh user@host.example.invalid
ssh -p 2222 -i /private/operator-key user@host.example.invalid
ssh --profile office
wifi
```

`ssh` prompts for a hidden password (Enter uses the specified/profile key).
Passwords never enter command history or logs. Private keys must be under
`/private/`; encrypted key passphrase entry is incomplete. Ctrl+] disconnects;
remote EOF/failure clears remote parser/screen state and returns to a fresh
local prompt, without automatic reconnect.
Remote bytes bypass local command parsing and use the proven libssh2/PTY path,
including VT100 responses. Only its established SHA-2/AES-CTR/ECDSA policy is
supported; unsupported server algorithms fail closed.

Operator-managed optional SD files (never commit actual files):

- `/private/tab5-shell-profiles.txt`: one `NAME user@HOST [-p PORT]
  [-i /private/KEY]` record per line. Multiple names are supported; none is
  selected automatically. Comments start with `#`.
- `/private/tab5-shell-known-hosts.txt`: one `lowercase-host PORT HEX_SHA256`
  record per line, where HEX_SHA256 is the 64 hex digits of SHA-256 over the SSH
  host public-key blob, not the public-key text file. The pin must correspond
  to the **ECDSA P-256 server host key** selected by this transport; an Ed25519
  or RSA host-key fingerprint does not match it. Obtain it via an
  independently authenticated administrator/host channel (decode the `SHA256:`
  base64 fingerprint from `ssh-keygen -lf HOST_PUBLIC_KEY -E sha256` to hex).

Unknown, changed, duplicate or malformed pins fail **before authentication**.
In 0.1.2, `HOST KEY CHANGED` also covers a missing/unknown/malformed SD pin;
check the pin file before concluding that the server changed its key. Verify
its live ECDSA key against an independent trusted record before adding/replacing
that host's entry. There is no TOFU enrollment, bypass flag or automatic NVS
trust write.

`wifi` reports status without starting the radio. SSH/download explicitly
powers the C6 before hosted Wi-Fi initialization, checks factory 1.4.x
compatibility, and joins the network already saved by Console, using RAM
configuration. This HAL exposes `tab5_radio_power_set` but no `tab5_radio_init`:
`esp_wifi_init` is its hosted initialization boundary. A partial initialization
failure requires an app restart. Shell provides no Wi-Fi credential editor and
never updates C6 firmware.

## Packages and Lua

Copy `examples/hello/hello.pkg` and `hello.lua` to an operator-selected location
in `/home` or read-only `/sd`, then:

```
pkg install /home/hello.pkg
pkg list
hello world
pkg remove hello
```

These commands perform real SD reads/writes. Installation validates bounded
metadata and SHA-256, stores code in `/home/.packages/`, then publishes metadata
last. Listing and every execution recheck integrity. Names/entries are safe
single-component paths; built-ins cannot be replaced. Duplicate/partial
installs require removal before retrying; at most 32 packages are supported.
The text manifest format is demonstrated in `examples/hello/hello.pkg`.

Lua 5.4.8 is embedded from verified official source. Each execution uses a new
state, text-only source (16 KiB max), a hard 256 KiB PSRAM allocator, 100,000 VM
instruction budget, 24 nested C calls and 16 KiB output limit. `arg[1...]` and
`print` work; basic/string/table/math/UTF-8 libraries are available. File/OS,
package, debug, dynamic load and bytecode APIs are absent. Execution checks for
at least 384 KiB free PSRAM first. Host `hello` peak is 13,387 bytes; actual
ESP32 heap/PSRAM peaks and task high-water marks remain **unverified**.

Install only scripts you have reviewed and trust. A hash detects corruption,
not authorship. This is an in-process runtime, not an adversarial security
boundary; VM instruction hooks do not bound time spent inside C library calls.
There is no online repository, signature system, automatic execution on boot,
script-to-shell API or arbitrary native module support.

## Touch keyboard and HTTPS

Tap **KEYS** to toggle the optional overlay. Shift/Ctrl/SYM, letters, digits,
space, arrows, backspace and Enter are available. It obscures the lower terminal
rows; editor/viewer reflow above it. SSH retains its 80x24 PTY size. Device
redraws remain driven by damage/events, not periodic full-screen repainting.

`download https://HOST/PATH /home/FILE` joins saved Wi-Fi, synchronizes the
system clock via SNTP if needed, verifies the TLS certificate chain/hostname/date
with ESP-IDF's bundled roots, and saves at most 64 KiB using recovery-aware
writes. Only HTTP 200 is accepted; redirects, credentials in URLs, non-HTTPS,
alternate ports, oversized or incomplete bodies fail. No downloaded file is
executed/installed automatically. DNS/TLS/transfer are synchronous and may pause
local input; there is no resumable download or background cancellation. Live
network and TLS operation remain unverified.

## Build, validate and install

Use ESP-IDF **5.5.5**, configured with the ignored host environment file:

```sh
cd projects/tab5-shell/firmware
bash tests/run_checks.sh
../../../idf.py build
python3 ../tools/package_sd_app.py --image build/Tab5Shell.bin \
  --license ../Tab5Shell-LICENSE.txt --output ../artifacts/release-0.1.3/sd-library
python3 ../tools/validate_release.py --image build/Tab5Shell.bin \
  --package-dir ../artifacts/release-0.1.3/sd-library
```

Flash targets are blocked. With separate hardware-install authorization, copy
only the versioned `.bin`, `.json`, `-LICENSE.txt` triplet to SD `/firmware/`,
retain rollback packages, and select **Tab5Shell 0.1.3** in Console's SD App
Library. Console 0.3.7 scans up to **32** valid root-level manifests. If
adding a version would exceed that limit, preserve the earlier Shell triplet
under `/firmware/rollback/Tab5Shell-OLD_VERSION/` instead of leaving both
versions at the scanned root. Do not move other apps. Press REFRESH after a
card change; DOWN scrolls the library's five visible rows. Do not write bootloader/table/factory/NVS/C6 images. First complete
frame precedes `tab5_app_boot_ready(true)`; reset returns to factory Console.
Failed confirmation has a 15-second return watchdog. See the linked release
checkpoints for exact hashes and physical acceptance gates. Build/package checks
do not establish hardware success.
