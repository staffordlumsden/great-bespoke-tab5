# Tab5Camera development bring-up

Early SD-launchable camera bring-up for the Great Bespoke Tab5. In 0.0.5-dev
the user confirmed the portrait preview orientation and reported excellent
responsiveness. Version 0.1.0-dev adds touch capture and SD JPEG saving.
Version 0.2.1-dev adds a gallery and persisted grid and monochrome controls;
the user confirmed both colour and monochrome gallery images look correct. See
the checkpoint document for evidence and remaining checks.

The app uses the project's ST7121 display/touch/SD HAL and its existing Console
recovery flow. It powers the camera using bit 6 of the first board expander and
passes the already-initialized I2C bus to Espressif's CSI video stack. The
SC202CS driver identifies the sensor referred to as SC2356 in the M5Stack demo.
The intended sensor mode is RAW8 1280 x 720, converted by the ISP to RGB565. A
separate capture task owns two mapped PSRAM frames and lends one to the display
owner at a time; a bounded queue prevents a frame from being requeued while the
preview is reading it. The app discards incomplete frames. The preview is a
scaled portrait image. PPA acceleration has a software fallback. Color quality,
capture reliability and sustained performance still need physical verification.
ISP automatic processing is enabled as in M5Stack's reference demo.

Tap **CAPTURE** to save a full-resolution 720 x 1280 portrait JPEG at quality
80 to `/DCIM/100TAB5/`. The app shows the saved filename once writing and
readback succeed; it reports errors and prevents Console return during a save.
Files have date/time and collision-safe sequence numbers, or `UNDATED` if the
RTC is unavailable. **GRID** toggles a viewfinder-only composition grid;
**MONO** changes the preview and newly saved photos to monochrome. Both
settings are retained across restarts. **GALLERY** browses saved camera JPEGs
one at a time with previous/next controls, a return button and confirmed
deletion. Thumbnails, zoom, pan, EXIF and quality settings are not yet present.

The screen shows an SD mount indication and a touch button to return to
Console. The resident Console is selected after the display
and touch initialize and the initial frame is presented; reset then returns to
Console. Camera failure is displayed on screen and logged over serial. No Wi-Fi
or C6 radio API is used. The HAL performs its usual RTC and battery setup, and
may initialize an invalid RTC from build time; see the shared HAL source.

## Build

Install and configure ESP-IDF 5.5.5 as described in the workspace README, then:

```sh
cd projects/tab5-camera/firmware
../../../idf.py build
```

`sdkconfig` and `dependencies.lock` are retained for reproducibility. The
project uses a local copy of the Console HAL with camera-bus access, camera
power, and bounded RGB565 preview and capture rotation added. Its dependencies
include the shared project-local ST7121 driver and exFAT override. Direct flash
targets are blocked: this application belongs in an SD launcher app slot, never
the resident Console factory partition. Do not use the generic flash commands
printed by ESP-IDF.

The checked build and SD-library triplets are local under `firmware/build/`
and `artifacts/sd-library/`. The checkpoint document records which exact
versions have been delivered and tested.

## Bring-up gate

The 0.1.0-dev hardware run verified normal shutter capture, SD write/readback,
independent JPEG decoding and preview recovery. Test media removal, full-card
handling, interrupted writes, colour accuracy and longer-term stability before
claiming field reliability. The on-device deletion confirmation remains untested.
A currently
unrecognized serial device must not be assumed to be the P4. The source/build
checkpoint is in `docs/TAB5_CAMERA_BRINGUP.md`.
