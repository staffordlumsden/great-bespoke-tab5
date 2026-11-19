# Tab5 ThinClient 0.1.2-dev: SSH receive diagnostics

14 November 2026. After installing 0.1.1-dev, the user reported that
`openclaw tui` returned to the local prompt with `SSH read rc=-43: transport
read`. A fresh attempt was correlated with the SSH server log, which recorded
an SSH disconnect from the client; this does not identify the underlying
socket receive condition.

This diagnostic build wraps libssh2's receive callback, retaining the existing
would-block and interrupted-call behavior, and displays either TCP EOF or the
underlying socket errno alongside a `-43` error. It does not claim to fix the
disconnect. It retains the 0.1.1-dev transport and SSH menu changes.

Native host and contract checks passed. The modified SSH client also passed a
host-side connection test against an authenticated LAN host with concurrent
shell and bridge traffic. ESP-IDF v5.5.5 built successfully with the original
`sdkconfig` and dependency lock preserved; the temporary configuration differed
only by the disabled H.264 version selector described in the prior checkpoint.
The SD packager verified the P4 image, chip revision range, checksum, digest,
and 5 MiB slot bound. Image size 2,635,616 bytes; SHA-256
`211a03629b7a03fd4650bb7772e1765bb2d233ffd70eff31efa6b602bbd7d18f`.

The package was copied to the Tab5 SD card and read back byte-for-byte; the
previous 0.1.1-dev package remains for rollback. Neither the diagnostic build
nor `openclaw tui` has been physically verified on the device. Install using
Console, reproduce once, and record the complete SSH close message, including
`TCP EOF` or `socket errno`, to guide the actual repair. No device was flashed
in this step.
