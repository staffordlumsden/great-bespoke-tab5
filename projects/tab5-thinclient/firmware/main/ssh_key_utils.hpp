#pragma once

#include <string>

/// Normalizes unencrypted SEC1 `EC PRIVATE KEY` PEM blocks that encode curve
/// parameters as explicit `SpecifiedECDomain` (as emitted by macOS/LibreSSL
/// `ssh-keygen -t ecdsa -m PEM`) into the equivalent `namedCurve` OID encoding
/// supported by ESP-IDF mbedTLS 3.x.
///
/// Returns `pem` unchanged for named-curve keys, encrypted keys, OpenSSH/RSA
/// formats, or unrecognized encodings.
std::string normalizeEcPrivateKeyPemIfExplicit(const std::string& pem);
