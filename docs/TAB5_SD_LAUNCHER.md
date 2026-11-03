# Tab5 resident Console and SD firmware launcher

27 October 2026. Partition migration installed and Console boot verified.
Weather SD launch and Console return were subsequently accepted on the device;
the unconfirmed-boot rollback and physical recovery paths remain unexercised.

## Flash layout

Console and Weather now build with the same 16 MiB partition table. The original
NVS, PHY and 4 MiB factory Console retain their addresses and sizes.

| Partition | Offset | Size | Purpose |
|---|---:|---:|---|
| NVS | `0x9000` | 24 KiB | Existing settings, saved station and bonds |
| PHY | `0xf000` | 4 KiB | Existing PHY data |
| Factory | `0x10000` | 4 MiB | Resident `tab5_console` |
| OTA data | `0x410000` | 8 KiB | Boot selection and first-boot state |
| OTA 0 | `0x420000` | 5 MiB | SD application slot A |
| OTA 1 | `0x920000` | 5 MiB | SD application slot B |

The final slot ends at `0xe20000`; the remaining flash stays unallocated. Both
Console's bootloader and application enable ESP-IDF first-boot rollback. Anti-
rollback eFuse changes are not used. ESP-Hosted 1.4.0, Wi-Fi Remote 0.8.5 and the
factory C6 firmware remain unchanged.

## Launch and return

1. In Console, choose **Launch from SD Card**, select a package in `/firmware`,
   tap **INSTALL + RUN**. Console 0.2.2 combines validation and installation in
   one operation with stage/progress feedback.
2. The installer validates the manifest, P4 revision/flash compatibility, image
   identity, checksum and appended SHA-256. It also checks the actual flash
   size, exact partition geometry and resident Console identity before writing.
3. Console selects factory as the recovery target, then stages the image into
   alternating OTA slots. It hashes the written bytes and compares SD/flash
   readback before selecting the new app and restarting. Rotation uses only
   `tab5_launcher/last_slot` in NVS; network and bond namespaces are preserved.
4. Weather renders its first frame and completes its critical startup checks.
   The shared boot component confirms a pending OTA boot, then selects factory
   for the **next** reset while Weather continues running. A later reset or
   power cycle therefore returns to Console.
5. Weather's visible **CONSOLE** action unmounts SD, selects factory and restarts
   immediately. An unsuccessful startup also attempts Console return after a
   15-second grace period following hardware/service initialisation.

An app that resets before confirmation is rejected by the rollback-enabled
bootloader. The Console clears previous OTA selection before each launch, so
the fallback is factory Console. A task stuck before the grace-period code
requires a reset; this implementation does not add a separate hardware watchdog.

ESP-IDF implements factory selection by erasing **only `otadata`**. It does not
erase NVS or the factory app. See the pinned
[ESP-IDF OTA documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-reference/system/ota.html)
and [`esp_ota_set_boot_partition` implementation](https://github.com/espressif/esp-idf/blob/v5.5.5/components/app_update/esp_ota_ops.c).

## One-time migration

An application-only Console update does not install the new bootloader or table.
The migration tool provides offline preparation, explicit application and targeted
recovery. Run it from the repository root in the pinned ESP-IDF environment.
Keep bundles inside ignored `backups/`; snapshots contain private settings.

Confirm the actual P4 port and release other serial owners first. With the board
held in download mode, capture a full snapshot without starting the application
again between capture and apply:

```sh
. .tools/export.sh
mkdir -p backups/sd-launcher-migration
python -m esptool --chip esp32p4 --port /dev/cu.REPLACE_WITH_P4 \
  --after no_reset read_flash 0 0x1000000 \
  backups/sd-launcher-migration/full-flash.bin

python tools/sd_launcher_migration.py prepare \
  --backup backups/sd-launcher-migration/full-flash.bin \
  --output backups/sd-launcher-migration/bundle
```

Preparation verifies a full 16 MiB backup, partition-table MD5, compiled layout,
P4 revision compatibility, image integrity, and the generated application's and
bootloader's rollback configuration. It saves the four replacement regions and
exact recovery copies with SHA-256 digests in `plan.json`. It performs no device
operation. Use a new output directory for every preparation.

Apply the reviewed bundle with an explicit port:

```sh
python tools/sd_launcher_migration.py apply \
  --bundle backups/sd-launcher-migration/bundle \
  --port /dev/cu.REPLACE_WITH_P4
```

The tool verifies chip/revision and disabled secure boot/flash encryption, reads
the complete flash again, and requires it to match the prepared snapshot before
writing. It writes Console, initial OTA data, bootloader and finally the table,
verifying each region. A complete final readback must match the expected flash
byte-for-byte, including unchanged NVS, PHY, OTA slots and unallocated space,
before the tool resets the device using esptool's supported P4 watchdog reset.
This avoids the USB-UART reset path that retained ROM download mode on this
unit. It never uses chip erase, force, C6 firmware
updates or SD formatting.

A failure stops further commands and automatic resets. ROM download access is
the recovery path during this one-time bootloader/table replacement; there is no
claim that this migration is atomic under power loss. To restore the four saved
regions after re-entering download mode:

```sh
python tools/sd_launcher_migration.py recover \
  --bundle backups/sd-launcher-migration/bundle \
  --port /dev/cu.REPLACE_WITH_P4
```

Recovery takes a fresh snapshot and verifies all restored regions and all other
flash. It restores the pre-migration bootloader, table, Console and OTA data;
NVS, PHY and any app-slot contents are preserved as they were when recovery
started. Preserve the original complete backup for separate disaster recovery.

## App authoring contract

Other firmware can join the SD library if it targets this P4 revision, declares
16 MiB flash, fits a 5 MiB app slot and is packaged with its manifest and licence.
Link the shared `tab5_app_boot` component and enable
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in the app configuration.

After critical hardware/service checks and the first presented frame, call
`tab5_app_boot_ready(true)` and check its return code. This confirms first boot
and arms Console for the next reset. Before that point, a failure should call
`tab5_app_boot_return_to_console()` or restart to let rollback take effect.
Provide a visible Console action that flushes/unmounts app storage and calls
`tab5_app_boot_return_to_console()`. NVS must be initialised before using the
launcher; apps must not erase shared NVS.

The installer validates image integrity and compatibility, not arbitrary app
behaviour. Third-party firmware must implement this return contract to receive
the same reset/Console behaviour as Weather. It runs with normal firmware flash
privileges; partitioning is not a sandbox.

Weather's direct flash targets are blocked because ESP-IDF's default first-app
offset belongs to Console. Build Weather, package `Tab5Weather.bin`, and copy
the matched `.bin`, `.json` and `-LICENSE.txt` into SD `/firmware` instead.

## Verification boundary

The real boot lifecycle is exercised under ASan/UBSan with fault-injecting SDK
fakes. Offline migration checks simulate apply, complete recovery, changed-device
rejection, payload/plan corruption and interrupted writes. Both projects build
with ESP-IDF v5.5.5. Existing weather/parser/UI/HTTPS and keyboard checks remain
separate from physical acceptance.

The migration and Console serial boot are verified on the P4. The user first
observed SD validation/install, Weather launch and touch Console return with
0.1.0-dev, then accepted Console 0.2.2 / Weather 0.1.1 as working as intended
after the smooth-launch and search repair. Host simulations and builds do not
establish unconfirmed-boot rollback, physical recovery or destructive failure
paths; those remain deliberately unexercised.
