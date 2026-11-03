# Tab5 Camera development bring-up

2 November 2026. The requested standalone camera is being developed as a
separate SD-loadable application in `projects/tab5-camera/`. Version
`0.0.1-dev` establishes an unverified live-preview path; it is not the minimum
viable camera described in the attached design brief.

## Source decisions

- ESP-IDF 5.5.5, tested pre-v3 P4 build branch, 16 MB flash, 32 MB PSRAM, and
  the existing resident Console and two 5 MB SD application slots.
- Use the local ST7121 HAL and exFAT override. A project-local HAL copy extends
  the established board code with a camera power switch, access to its existing
  I2C bus, and an RGB565 landscape preview blit. This avoids starting a second
  BSP I2C owner. Only camera expander bit 6 is changed for sensor power.
- The Espressif component documentation and the M5Stack reference list the
  SC202CS as SC2356. The build enables its 1280 x 720 RAW8 mode, CSI, ISP and
  ISP pipeline controller. These are source-level selections, not detection
  evidence from the target unit.
- Two camera-owned PSRAM frame buffers and single-entry handoff queues prevent
  reuse while the application renders. The application validates the negotiated
  format and frame size before reading pixels. The output is scaled in software
  into the existing display's double-buffered landscape coordinate system.
- A visible startup screen and touch return-to-Console remain available if
  camera initialization fails. No network or radio functionality is started.

## Verification and open work

ESP-IDF 5.5.5 build: **passed**. The application is 851,728 bytes and its
SHA-256 is `2dc1bc33e96a52b94cf8fd0aab8112251ed0e8ce90cbf9349029bcac45e7eba8`.
The build emits two deprecation warnings in the managed `esp_video` ISP source.
The existing SD package validator accepted the application, and the local
package image was compared byte-for-byte with the build and against its JSON
metadata. Generated build rules show that `flash`, `app-flash`,
`bootloader-flash` and `partition-table-flash` depend on the failing SD-only
guard. These checks do not test the Console's on-device installer.

At the 2 November checkpoint the build and generated package did not establish
physical behavior. The
camera module, power path, ISP, color order, orientation, throughput, SD
mount, touch return and recovery must be checked on the actual Tab5. No target
P4 serial device had been identified for that build, and no device had been flashed.

Still capture, JPEG encoding, durable collision-safe photo storage, gallery,
settings, battery display and error/recovery testing are not implemented. No
performance, sustained operation or 24-hour stability claim is made. The
attached design's hardware acceptance and complete offline camera requirements
remain outstanding.

## SD staging — 3 November 2026

The connected USB serial/JTAG device was identified by esptool as ESP32-P4
revision v1.3. The `0.0.1-dev` package triplet was copied to the existing
64 GB exFAT card's `/firmware/` directory, and each file was independently
compared byte-for-byte with the local package. macOS-created sidecars for
those three files were preserved locally and removed from the card; the card
was then safely ejected. This is host-side delivery, not an installed or
physically verified camera. The application binary SHA-256 remains the value
above. A serial monitor was opened for the subsequent device launch.

## First hardware test and format fix — 3 November 2026

Console launched `0.0.1-dev` from `ota_1`. Serial output confirms 32 MB PSRAM,
ST7121 touch/display initialization, exFAT SD mount, first-frame presentation,
and detection of the SC202CS sensor (PID `0xeb52`). The user observed the
development screen and `CAMERA NOT AVAILABLE`. The log recorded `RGB565 format
setup failed` before streaming; no camera image was observed.

The video component's `VIDIOC_G_FMT` implementation copies a stored format whose
stream type can be zero. The first build reused that returned structure for
`VIDIOC_S_FMT`; driver source indicates this can cause stream lookup to fail.
The new `0.0.2-dev` build retains the default RGB565 format when already
selected, otherwise explicitly supplies the capture-stream type on subsequent
ioctls, and logs the initial format and error numbers. This repair remains a
hypothesis until the new build is tested on the device.

`0.0.2-dev` builds successfully (851,936 bytes; SHA-256
`4b65f46cf4a6089e2b1875ce7d2c99fb2cea9ee66ee1469716e6cb5aff05cff9`).
Its three SD package files were uploaded through Tab5 File Server to
`/firmware/`, then downloaded and verified byte-for-byte. Console scans at
most eight valid manifests; the user reported only the earlier camera build
visible. To make the revised build visible without changing Console, the
complete original `0.0.1-dev` triplet was copied and readback-verified under
`/firmware/rollback/`. The original early-directory manifest name
`/firmware/Tab5Camera-0.0.1-dev.json` now contains the verified `0.0.2-dev`
manifest and points at the separately named 0.0.2 image and license. The
original 0.0.1 image and license also remain in `/firmware/`. The revised
package was subsequently launched as described below.

## Second hardware test and CSI memory fix — 3 November 2026

`0.0.2-dev` launched and the user again observed `CAMERA NOT AVAILABLE`. The
serial trace confirms that the sensor was detected and that the initial format
was RGB565, 1280 x 720, with the returned structure's type set to zero. This
confirms the cause addressed by the 0.0.2 negotiation change. The CSI stream
then failed: `dw_gdma_channel_register_event_callbacks: user context not in
internal RAM`. The camera controller's IDF source uses internal allocation
when `CONFIG_CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE` is enabled; its CSI callbacks
are IRAM-marked and the esp_video context is internally allocated. The 0.0.3
build enables that configuration to address the observed error. Actual CSI
startup and capture are still pending hardware confirmation.

`0.0.3-dev` builds (852,176 bytes; SHA-256
`f9dd93d0b0e2d74559b35b9b3cbfedecb016b2e14f40209ffe9e9316e7bb7d20`).
The triplet was uploaded using File Server and each file downloaded and
compared byte-for-byte. The early-directory `0.0.1-dev.json` catalog entry
now contains the verified 0.0.3 manifest; the 0.0.1 rollback triplet remains in
`/firmware/rollback/`, and both 0.0.2 and 0.0.3 triplets remain in `/firmware/`.

The user wants the eventual camera UI in portrait orientation with viewfinder
above and capture controls below, accounting for the lens on the right bezel
in landscape (top in the intended portrait orientation). Verify the image
rotation and mirroring on the physical device before finalizing that layout.

`0.0.3-dev` subsequently launched successfully. Its log shows the SC202CS
detected, `CSI stream started 1280x720 RGB565, two mapped buffers`, and `First
complete camera frame received`. The user saw an upright live image in
landscape with the lens on the right, but estimated the refresh rate at about
one frame per second. The controls were still on the right. This demonstrates
initial image acquisition and display on this hardware, not camera or
performance acceptance; still capture and gallery are not implemented.

The 0.0.4-dev portrait build launched, started CSI and received frames. The
user reported smoother updates but an upside-down image with the lens at the
top. The next development build reverses the frame rotation. It also attempts
hardware PPA rotation and scaling into the inactive display buffer, preserving
a software fallback, and logs measured presentation throughput. That change
requires a new on-device test; no specific frame rate is promised.

## Portrait preview accepted — 3 November 2026

`0.0.5-dev` builds successfully (869,552 bytes; SHA-256
`d3281d595838a16c122ae2ae1b2921dc55588f8edc47067d1fda19daa51e91a3`).
Its SD triplet was uploaded to `/firmware/` via File Server and every file was
downloaded and verified byte-for-byte. The visible early-directory manifest
points to 0.0.5; the complete 0.0.1 rollback triplet remains in
`/firmware/rollback/`, with later packages retained in `/firmware/`.

The device launched 0.0.5 from the Console SD library, detected the camera,
started the CSI stream and received complete frames. The user confirmed the
portrait image is correctly oriented and said responsiveness is "excellent".
The app's five-second presentation counters reported 7.2–7.5 fps in this
short run. This is an observed preview rate, not a sustained performance or
stability guarantee. No PPA fallback warning appeared in the observed log.
Portrait touch return to Console was observed in the preceding 0.0.4 run.
Still photography, storage, review, settings, battery UI and extended
reliability tests remain outstanding.

## Capture development build — 3 November 2026

`0.1.0-dev` adds a touch shutter and a separate JPEG/storage worker. A full
1280 x 720 RGB565 camera frame is rotated to 720 x 1280 to match the accepted
portrait preview, JPEG-encoded at quality 80 using the ESP32-P4 hardware, and
saved under `/DCIM/100TAB5/`. The SD writer uses exclusive temporary creation,
collision-checked names, sync, full byte-for-byte readback and FatFs rename;
the UI waits for completion before indicating success or allowing a Console
return. There is no EXIF metadata, gallery or configurable quality yet.

The host storage tests passed for name collisions, interrupted temporary
files, readback, JPEG marker rejection and missing timestamps. ESP-IDF build
passed: 886,448 bytes, SHA-256
`a94fd4e4094669ce93682d88bfe87b1106b9867b4f6fa811f3718319769d94c2`.
All three SD package files were uploaded and independently read back. The
full 0.0.5 preview triplet was verified under `/firmware/rollback/` before
the visible early-directory manifest was updated to point to 0.1.0.
At staging, device capture and independent JPEG decoding were pending.

On-device follow-up: Console launched `0.1.0-dev`; the user tapped the shutter
and confirmed that `SAVED` and the filename appeared and live preview resumed.
Serial output recorded three saves of 148,656, 139,304 and 126,814 bytes, with
reported JPEG-encode/write/readback times of 207, 113 and 110 ms respectively.
Preview counters around captures remained approximately 7.2–7.5 fps.
After returning to File Server, all three files were downloaded from
`/DCIM/100TAB5/` and independently verified and fully decoded with Pillow as
720 x 1280 RGB JPEGs. One was visually inspected as a recognisable upright
portrait. The source photos remain on the card. These results cover a short
normal-operation test; media removal, full-card handling, interrupted power,
colour accuracy and sustained repeated capture remain untested on hardware.

## Gallery and image settings — 3 November 2026

`0.2.0-dev` added on-screen grid and monochrome switches, stored in the
`tab5camera` NVS namespace, and a portrait gallery for camera JPEGs with
previous/next navigation and confirmed deletion. Grid is a preview-only overlay;
monochrome applies to the preview and the raw frame passed to JPEG encoding.
Host tests passed for gallery ordering, selection boundaries, rejecting invalid
delete paths, and RGB565 monochrome conversion. The verified SD package was
installed after the 0.1.0-dev triplet was copied to `/firmware/rollback/` and
readback-verified.

On the device the user confirmed that grid and monochrome worked in the preview,
that captures were saved and that gallery navigation operated. The serial log
recorded monochrome and colour captures and successful JPEG decoder calls, but
the user reported both gallery images were badly pixelated with green/purple
colour. The decoder configuration used `JPEG_DEC_RGB_ELEMENT_ORDER_RGB` for
RGB565. The local Tab5 JPEG compatibility reference and IDF's decoder source
identify a P4 byte-order quirk: BGR selects the native RGB565 packing needed by
the display. `0.2.1-dev` switches the decoder to that setting. Its result must
be physically tested before claiming the gallery image defect is resolved.

During the short 0.2.0 test, live-preview counters were approximately 7.3 fps
in colour and 4.2 fps in monochrome. Settings writes returned success; their
persistence across a fresh launch remains to be checked. The test did not delete
any existing photos.

`0.2.1-dev` built successfully (917,728 bytes; SHA-256
`8982579e4c8d6861f4878c351e21134024645cace87e6d0dc6d6156480aaaa69`).
The SD triplet was uploaded and readback-verified; 0.2.0 and the previously
accepted 0.1.0 capture package are retained in `/firmware/rollback/`. The
early-directory manifest points to 0.2.1 because Console scans only eight
packages. Host tests passed for gallery ordering and boundaries, restricting
delete paths, monochrome conversion, and the existing photo storage checks.

The user launched 0.2.1 and confirmed the colour and monochrome gallery
images were correct and "works as intended". Serial logs show the gallery
decoded several images while navigating, and the grid setting reloaded after
the application was relaunched. Separately, two 0.2.0 test JPEGs were fetched
from SD and fully decoded on the host: the monochrome image had mean RGB
channel differences of 1.0–2.84 levels, while the colour image was coloured.
No existing photographs were deleted in these checks; the on-device delete
confirmation remains physically untested. The gallery browses one camera photo
at a time and does not yet offer thumbnails, zoom or pan. The device returned
to Console after the test. Extended operation and storage-failure scenarios
remain untested.
