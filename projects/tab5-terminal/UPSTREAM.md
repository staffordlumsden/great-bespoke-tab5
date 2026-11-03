# Upstream and fork record

- Source: <https://github.com/ciniml/portable_terminal>
- Fork: <https://github.com/staffordlumsden/portable_terminal>
- Imported commit: `44a616ee333242e0fd3181d5771ead55ded58253`
- Imported commit subject: `demo: terminal exercise tools and a guided VT100 tour`
- License: Boost Software License 1.0
- Snapshot location: `upstream/`

The snapshot was imported with `git archive`, so it contains tracked upstream
files but no nested `.git` directory, generated build output or untracked local
configuration. Upstream gitlink/submodule payloads are not embedded. Its
hardware-independent `components/term_core` is built directly by this port;
the original 59 host tests remain in `upstream/host_test`.

## Port boundary

The Great Bespoke port reuses the terminal parser/screen core, cursor design,
HID translation, Tab5 Keyboard register protocol, SSH connection structure and
TOFU approach. It replaces M5GFX/M5Unified display ownership, Wi-Fi setup,
partitions, configuration storage and deployment with the repository's proven
Tab5 HAL, saved Console Wi-Fi, private SD profile and SD app-boot contract.

Most importantly, upstream currently requires ESP-IDF 6.0 plus ESP-Hosted 2.x
and provides `c6_updater/` and `slave_c6_fw/`. Those directories are retained
for provenance only. Great Bespoke Tab5 remains on ESP-IDF 5.5.5,
ESP-Hosted 1.4.0 and Wi-Fi Remote 0.8.5, and never modifies the factory C6.

The project-local libssh2 component comes from the already tested Contextual
Micro ESP-IDF 5.5 port rather than upstream's IDF 6 wrapper. Its exact source
and bounded-buffer adaptation are recorded in
`firmware/components/libssh2_esp/PROVENANCE.md`.
