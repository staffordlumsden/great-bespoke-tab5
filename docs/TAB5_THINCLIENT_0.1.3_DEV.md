# Tab5 ThinClient 0.1.3-dev: SSH full-packet cipher compatibility

14 November 2026. The user reported `openclaw tui` returned to the local prompt
on 0.1.2-dev with `SSH read rc=-43: transport read (TCP EOF)`. The previous
callback did not distinguish a real zero return from `recv` with length zero.

The shared libssh2 component uses a 1 KiB inbound staging buffer and a 4 KiB
outbound buffer. Its provenance specifies AES-CTR, SHA-256 MACs, no
compression, and at most 512-byte channel writes for this adaptation. ThinClient
had not applied these cipher, MAC, compression, or write-size restrictions.
Its preferred ChaCha20-Poly1305 cipher requires a full packet in the staging
buffer. When such a packet exceeds that buffer, the transport can call `recv`
with length zero, returning zero even when the peer has not closed the socket;
this can produce the observed false EOF. The negotiated cipher on the device
and the specific failure path have not yet been observed, so this is a strong
hypothesis rather than a confirmed hardware diagnosis.

This build applies the shared library's documented AES-CTR, SHA-256 and
no-compression policy in both directions and fails the connection if policy
setup fails. Writes are limited to 512 bytes per libssh2 channel write. The
receive diagnostic now separately identifies zero-length receive calls and real
TCP EOF. Existing host-key and KEX choices are preserved.

Native host tests passed, including a socket-pair check demonstrating that a
zero-length receive returns zero while the connection remains usable. The
contract check and ESP-IDF v5.5.5 build passed. The build used a temporary copy
of the original configuration; the saved `sdkconfig` and `dependencies.lock`
were preserved. The packager verified the image's chip, revision, 16 MB flash
setting, digest, checksum and 5 MiB slot bound. Image: 2,635,904 bytes;
SHA-256 `d46aaf92bca5c421fdc927c07356b87046d3a75be895bb5697689c85d2389d3f`.

The package triplet was copied to the mounted Tab5 SD card and read back
byte-for-byte. The superseded diagnostic package was backed up off-card; the
0.1.1-dev package remains on the card as a fallback. The NUC was unreachable
from the Mac during this build, so the new policy has not been independently
negotiated against it. The image has not been installed or tested on hardware.
Confirm repeated `openclaw tui` launches and exits, ordinary SSH commands,
large output and SD bridge behavior. Devices or servers that do not support the
specified AES-CTR/SHA-256 combination will fail to connect instead of silently
negotiating an incompatible cipher.

## Physical feedback

On 14 November 2026, after installing 0.1.3-dev and being asked to try
`openclaw tui` repeatedly, the user reported that it "works as intended".
This confirms the reported OpenClaw workflow from the user's perspective; it
does not independently establish the exact cipher negotiated or demonstrate
extended reliability, large transfers, SD bridge operation, or rollback.
