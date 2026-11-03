# Tab5Camera handoff

Read the workspace `AGENTS.md`, this project's README and
`docs/TAB5_CAMERA_BRINGUP.md` before making changes. This is an SD-launched
development app; never overwrite the resident Console, partition table, NVS or
C6 firmware. Use ESP-IDF 5.5.5 through the workspace wrapper and retain
`sdkconfig` and `dependencies.lock`.

The camera app has a project-local copy of the Console HAL to share its board
I2C bus and expander and add the preview blit. Compare it with the shared HAL
before importing future HAL fixes; preserve ST7121 reset and framebuffer rules.
Do not initialize a separate BSP I2C bus on top of the HAL bus. The 0.0.5-dev
portrait preview was physically observed and reported responsive. The 0.1.0-dev
shutter and JPEG SD saving passed a short physical run and independent decode.
The 0.2.1-dev gallery and grid/monochrome controls were physically accepted;
on-device deletion confirmation, thumbnails, zoom and longer reliability tests
remain outstanding. Record physical findings
separately from host compilation and package checks.
