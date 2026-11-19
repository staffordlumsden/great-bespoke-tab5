#include "ssh_key_utils.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr const char* kEcBegin = "-----BEGIN EC PRIVATE KEY-----";
constexpr const char* kEcEnd = "-----END EC PRIVATE KEY-----";

bool readAsn1Tlv(const uint8_t* data, size_t len, size_t& pos,
                 uint8_t& outTag, size_t& outContentStart, size_t& outContentLen) {
    if (pos >= len) return false;
    outTag = data[pos++];
    if (pos >= len) return false;
    uint8_t first = data[pos++];
    size_t contentLen = 0;
    if ((first & 0x80u) == 0) {
        contentLen = first;
    } else {
        size_t numBytes = first & 0x7Fu;
        if (numBytes == 0 || numBytes > 4 || pos + numBytes > len) return false;
        for (size_t i = 0; i < numBytes; ++i) {
            contentLen = (contentLen << 8) | data[pos++];
        }
    }
    if (contentLen > len - pos) return false;
    outContentStart = pos;
    outContentLen = contentLen;
    pos += contentLen;
    return true;
}

void appendDerLength(std::vector<uint8_t>& out, size_t len) {
    if (len < 128) {
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFu) {
        out.push_back(0x81u);
        out.push_back(static_cast<uint8_t>(len));
    } else {
        out.push_back(0x82u);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFFu));
        out.push_back(static_cast<uint8_t>(len & 0xFFu));
    }
}

int decodeBase64Char(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 52 + (c - '0');
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool decodeBase64(const std::string& input, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t accum = 0;
    int bits = 0;
    bool seenPad = false;
    for (unsigned char c : input) {
        if (c == '\r' || c == '\n' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            seenPad = true;
            continue;
        }
        if (seenPad) return false;
        int val = decodeBase64Char(c);
        if (val < 0) return false;
        accum = (accum << 6) | static_cast<uint32_t>(val);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accum >> bits) & 0xFFu));
        }
    }
    return !out.empty();
}

std::string encodeBase64PemLines(const uint8_t* data, size_t len) {
    static const char kB64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4 + len / 48 + 4);
    size_t col = 0;
    auto pushCh = [&](char ch) {
        out.push_back(ch);
        if (++col == 64) {
            out.push_back('\n');
            col = 0;
        }
    };
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = data[i];
        uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        pushCh(kB64[(triple >> 18) & 0x3Fu]);
        pushCh(kB64[(triple >> 12) & 0x3Fu]);
        pushCh((i + 1 < len) ? kB64[(triple >> 6) & 0x3Fu] : '=');
        pushCh((i + 2 < len) ? kB64[triple & 0x3Fu] : '=');
    }
    if (col != 0) out.push_back('\n');
    return out;
}

bool matchPrimeBytes(const uint8_t* p, size_t len, const uint8_t* expected, size_t expectedLen) {
    if (len > 1 && p[0] == 0x00) {
        ++p;
        --len;
    }
    return len == expectedLen && std::memcmp(p, expected, expectedLen) == 0;
}

const uint8_t* namedCurveParamTlvForExplicitDomain(const uint8_t* seqData, size_t seqLen, size_t& outTlvLen) {
    // SpecifiedECDomain ::= SEQUENCE { version INTEGER, fieldID FieldID, ... }
    size_t pos = 0;
    uint8_t tag = 0;
    size_t cs = 0, cl = 0;
    if (!readAsn1Tlv(seqData, seqLen, pos, tag, cs, cl) || tag != 0x02u) return nullptr;  // version
    if (!readAsn1Tlv(seqData, seqLen, pos, tag, cs, cl) || tag != 0x30u) return nullptr;  // fieldID

    const uint8_t* fieldData = seqData + cs;
    size_t fieldPos = 0;
    size_t oidS = 0, oidL = 0;
    if (!readAsn1Tlv(fieldData, cl, fieldPos, tag, oidS, oidL) || tag != 0x06u) return nullptr;
    static const uint8_t kPrimeFieldOid[] = {0x2Au, 0x86u, 0x48u, 0xCEu, 0x3Du, 0x01u, 0x01u};
    if (oidL != sizeof(kPrimeFieldOid) || std::memcmp(fieldData + oidS, kPrimeFieldOid, oidL) != 0) {
        return nullptr;
    }

    size_t primeS = 0, primeL = 0;
    if (!readAsn1Tlv(fieldData, cl, fieldPos, tag, primeS, primeL) || tag != 0x02u) return nullptr;
    const uint8_t* primeBytes = fieldData + primeS;

    static const uint8_t kP256Prime[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    static const uint8_t kP384Prime[48] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    static const uint8_t kP521Prime[66] = {
        0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF,
    };

    // Context-specific [0] wrapping the namedCurve OID:
    static const uint8_t kNamedP256Tlv[] = {
        0xA0, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07,
    };
    static const uint8_t kNamedP384Tlv[] = {
        0xA0, 0x07, 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x22,
    };
    static const uint8_t kNamedP521Tlv[] = {
        0xA0, 0x07, 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x23,
    };

    if (matchPrimeBytes(primeBytes, primeL, kP256Prime, sizeof(kP256Prime))) {
        outTlvLen = sizeof(kNamedP256Tlv);
        return kNamedP256Tlv;
    }
    if (matchPrimeBytes(primeBytes, primeL, kP384Prime, sizeof(kP384Prime))) {
        outTlvLen = sizeof(kNamedP384Tlv);
        return kNamedP384Tlv;
    }
    if (matchPrimeBytes(primeBytes, primeL, kP521Prime, sizeof(kP521Prime))) {
        outTlvLen = sizeof(kNamedP521Tlv);
        return kNamedP521Tlv;
    }
    return nullptr;
}

}  // namespace

std::string normalizeEcPrivateKeyPemIfExplicit(const std::string& pem) {
    size_t beginPos = pem.find(kEcBegin);
    if (beginPos == std::string::npos) return pem;
    size_t bodyStart = beginPos + std::strlen(kEcBegin);
    size_t endPos = pem.find(kEcEnd, bodyStart);
    if (endPos == std::string::npos) return pem;

    std::string body = pem.substr(bodyStart, endPos - bodyStart);
    if (body.find("ENCRYPTED") != std::string::npos) return pem;

    std::vector<uint8_t> der;
    if (!decodeBase64(body, der)) return pem;

    size_t pos = 0;
    uint8_t tag = 0;
    size_t outerS = 0, outerL = 0;
    if (!readAsn1Tlv(der.data(), der.size(), pos, tag, outerS, outerL) || tag != 0x30u) {
        return pem;
    }

    const uint8_t* inner = der.data() + outerS;
    size_t innerPos = 0;
    size_t verS = 0, verL = 0;
    if (!readAsn1Tlv(inner, outerL, innerPos, tag, verS, verL) || tag != 0x02u ||
        verL != 1 || inner[verS] != 0x01u) {
        return pem;
    }
    size_t privS = 0, privL = 0;
    if (!readAsn1Tlv(inner, outerL, innerPos, tag, privS, privL) || tag != 0x04u) {
        return pem;
    }
    size_t prefixEnd = innerPos;  // version + privateKey TLVs

    if (innerPos >= outerL) return pem;
    size_t paramOuterStart = innerPos;
    size_t paramS = 0, paramL = 0;
    if (!readAsn1Tlv(inner, outerL, innerPos, tag, paramS, paramL)) return pem;
    if (tag != 0xA0u) return pem;

    size_t paramInnerPos = 0;
    size_t domS = 0, domL = 0;
    if (!readAsn1Tlv(inner + paramS, paramL, paramInnerPos, tag, domS, domL)) return pem;
    if (tag == 0x06u) return pem;  // already namedCurve OID
    if (tag != 0x30u) return pem;  // not explicit SpecifiedECDomain

    size_t namedTlvLen = 0;
    const uint8_t* namedTlv = namedCurveParamTlvForExplicitDomain(inner + paramS + domS, domL, namedTlvLen);
    if (!namedTlv) return pem;
    (void)paramOuterStart;

    size_t tailLen = outerL - innerPos;
    size_t newBodyLen = prefixEnd + namedTlvLen + tailLen;
    std::vector<uint8_t> newDer;
    newDer.reserve(4 + newBodyLen);
    newDer.push_back(0x30u);
    appendDerLength(newDer, newBodyLen);
    newDer.insert(newDer.end(), inner, inner + prefixEnd);
    newDer.insert(newDer.end(), namedTlv, namedTlv + namedTlvLen);
    newDer.insert(newDer.end(), inner + innerPos, inner + outerL);

    std::string out = std::string(kEcBegin) + "\n";
    out += encodeBase64PemLines(newDer.data(), newDer.size());
    out += std::string(kEcEnd) + "\n";
    return out;
}
