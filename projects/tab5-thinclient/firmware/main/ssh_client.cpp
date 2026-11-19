#include "ssh_client.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libssh2.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ssh_key_utils.hpp"
#include "ssh_socket.hpp"
#include "tofu.hpp"

namespace {

bool s_libssh2Inited = false;
const char* s_kbdInteractivePassword = nullptr;

// libssh2 collapses EOF and all fatal recv() errors to SOCKET_RECV (-43).
// Preserve the underlying reason so the local prompt can distinguish them.
LIBSSH2_RECV_FUNC(recordingRecv) {
    int* status = abstract ? static_cast<int*>(*abstract) : nullptr;
    // A zero-byte recv returns zero even when the TCP connection is open.
    if (length == 0) {
        if (status) *status = -2;
        return 0;
    }
    ssize_t result = recv(socket, buffer, length, flags);
    if (result > 0) return result;
    if (result == 0) {
        if (status) *status = -1;
        return 0;
    }
    const int code = errno;
    if (code == EINTR || code == EAGAIN || code == EWOULDBLOCK || code == ENOENT) {
        return -EAGAIN;
    }
    if (status) *status = code;
    return code ? -code : -EIO;
}

uint32_t nowMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void kbdInteractiveCallback(const char* name, int nameLen,
                            const char* instruction, int instructionLen,
                            int numPrompts,
                            const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                            LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses,
                            void** abstract) {
    (void)name;
    (void)nameLen;
    (void)instruction;
    (void)instructionLen;
    (void)prompts;
    (void)abstract;
    if (numPrompts == 1 && s_kbdInteractivePassword) {
        responses[0].text = strdup(s_kbdInteractivePassword);
        responses[0].length = static_cast<unsigned int>(strlen(s_kbdInteractivePassword));
    }
}

int waitSocket(int sock, LIBSSH2_SESSION* session, int timeoutMs) {
    struct timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    fd_set readSet, writeSet;
    FD_ZERO(&readSet);
    FD_ZERO(&writeSet);
    FD_SET(sock, &readSet);
    FD_SET(sock, &writeSet);
    int dir = libssh2_session_block_directions(session);
    fd_set* readfd = (dir & LIBSSH2_SESSION_BLOCK_INBOUND) ? &readSet : nullptr;
    fd_set* writefd = (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? &writeSet : nullptr;
    return select(sock + 1, readfd, writefd, nullptr, &tv);
}

int connectTcpWithTimeout(const char* host, uint16_t port, int timeoutMs, String& error) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(host, portStr, &hints, &res);
    if (rc != 0 || !res) {
        error = String("DNS lookup failed for ") + host;
        return -1;
    }

    int sock = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;

        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int cr = ::connect(sock, p->ai_addr, p->ai_addrlen);
        if (cr == 0 || errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv{};
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            int sr = select(sock + 1, nullptr, &wfds, nullptr, &tv);
            int soErr = 0;
            socklen_t soLen = sizeof(soErr);
            if (sr > 0 && getsockopt(sock, SOL_SOCKET, SO_ERROR, &soErr, &soLen) == 0 && soErr == 0) {
                fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
                int nodelay = 1;
                setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
                break;
            }
        }
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);
    if (sock < 0) {
        error = String("TCP connect failed to ") + host + ":" + portStr;
    }
    return sock;
}

bool readFileBytes(const char* path, std::string& out) {
    if (!path || !path[0]) return false;
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    out.clear();
    char buf[512];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        out.append(buf, n);
        if (out.size() > 65536) break;
    }
    fclose(fp);
    return !out.empty();
}

String lastSshError(LIBSSH2_SESSION* session, int rc) {
    char* msg = nullptr;
    int msgLen = 0;
    libssh2_session_last_error(session, &msg, &msgLen, 0);
    if (msg && msgLen > 0) {
        return String(std::string(msg, static_cast<size_t>(msgLen)));
    }
    return String("libssh2 rc=") + rc;
}

void finishScpChannel(int sock, LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* ch, bool sendEof) {
    if (!ch || !session) return;
    uint32_t start = nowMs();
    if (sendEof) {
        while (libssh2_channel_send_eof(ch) == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() - start > 2000) break;
            waitSocket(sock, session, 20);
        }
        while (libssh2_channel_wait_eof(ch) == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() - start > 2000) break;
            waitSocket(sock, session, 20);
        }
    }
    while (libssh2_channel_wait_closed(ch) == LIBSSH2_ERROR_EAGAIN) {
        if (nowMs() - start > 2000) break;
        waitSocket(sock, session, 20);
    }
    libssh2_channel_free(ch);
}

}  // namespace

bool SshClient::connect(const SshProfile& profile, int cols, int rows, String& error) {
    disconnect();
    if (!s_libssh2Inited) {
        if (libssh2_init(0) != 0) {
            error = "libssh2_init failed";
            return false;
        }
        s_libssh2Inited = true;
    }

    if (!profile.host.length() || !profile.user.length()) {
        error = "SSH host and user are required";
        return false;
    }

    int sock = connectTcpWithTimeout(profile.host.c_str(), profile.port, 8000, error);
    if (sock < 0) {
        return false;
    }

    lastReceiveStatus_ = 0;
    LIBSSH2_SESSION* session = libssh2_session_init_ex(nullptr, nullptr, nullptr, &lastReceiveStatus_);
    if (!session) {
        close(sock);
        error = "libssh2_session_init failed";
        return false;
    }

    libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV,
                                 reinterpret_cast<libssh2_cb_generic*>(recordingRecv));

    libssh2_session_set_timeout(session, 10000);
    // This shared libssh2 build has a 1 KiB receive staging buffer and a
    // bounded transmit buffer. Its documented supported transport is AES-CTR
    // with SHA-256 MACs and no compression, not full-packet AEAD ciphers.
    if (libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX,
                                    "ecdh-sha2-nistp256,diffie-hellman-group14-sha256,diffie-hellman-group-exchange-sha256") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY,
                                    "ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS, "aes128-ctr,aes256-ctr") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_SC, "aes128-ctr,aes256-ctr") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_CS,
                                    "hmac-sha2-256-etm@openssh.com,hmac-sha2-256") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_SC,
                                    "hmac-sha2-256-etm@openssh.com,hmac-sha2-256") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_CS, "none") ||
        libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_SC, "none")) {
        error = "SSH supported algorithm configuration failed";
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    int rc = libssh2_session_handshake(session, sock);
    if (rc != 0) {
        error = String("SSH handshake failed: ") + lastSshError(session, rc);
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    const char* hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash) {
        error = "Host key fingerprint unavailable";
        libssh2_session_disconnect(session, "no hostkey hash");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    uint8_t fp[32];
    memcpy(fp, hash, sizeof(fp));
    char hexBuf[65];
    for (int i = 0; i < 32; ++i) {
        snprintf(hexBuf + i * 2, 3, "%02x", fp[i]);
    }
    hexBuf[64] = '\0';
    fingerprintHex_ = hexBuf;

    firstUse_ = false;
    int tofuRc = tab5::tofu::check_or_record(profile.host.c_str(), profile.port, fp, &firstUse_);
    if (tofuRc == -1) {
        error = "HOST KEY MISMATCH (possible MITM) for " + profile.host;
        libssh2_session_disconnect(session, "host key mismatch");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    bool authed = false;
    String authReason;

    if (profile.keyPath.length()) {
        std::string privKey;
        if (readFileBytes(profile.keyPath.c_str(), privKey)) {
            privKey = normalizeEcPrivateKeyPemIfExplicit(privKey);
            std::string pubKey;
            String pubPath = profile.keyPath + ".pub";
            readFileBytes(pubPath.c_str(), pubKey);
            const char* pass = profile.keyPassphrase.length() ? profile.keyPassphrase.c_str() : "";
            rc = libssh2_userauth_publickey_frommemory(
                session,
                profile.user.c_str(),
                profile.user.length(),
                pubKey.empty() ? nullptr : pubKey.data(),
                pubKey.size(),
                privKey.data(),
                privKey.size(),
                pass);
            if (rc == 0) {
                authed = true;
            } else {
                authReason = String("Key auth failed: ") + lastSshError(session, rc);
            }
        } else {
            authReason = String("Cannot read key ") + profile.keyPath;
        }
    }

    if (!authed) {
        const char* userauthList = libssh2_userauth_list(session, profile.user.c_str(),
                                                         static_cast<unsigned int>(profile.user.length()));
        if (userauthList && strstr(userauthList, "none")) {
            rc = libssh2_userauth_password(session, profile.user.c_str(), "");
            if (rc == 0) authed = true;
        }
        if (!authed && profile.password.length()) {
            rc = libssh2_userauth_password(session, profile.user.c_str(), profile.password.c_str());
            if (rc == 0) {
                authed = true;
            } else if (userauthList && strstr(userauthList, "keyboard-interactive")) {
                s_kbdInteractivePassword = profile.password.c_str();
                rc = libssh2_userauth_keyboard_interactive(session, profile.user.c_str(), kbdInteractiveCallback);
                s_kbdInteractivePassword = nullptr;
                if (rc == 0) authed = true;
            }
            if (!authed) {
                if (authReason.length()) authReason += "; ";
                authReason += String("Password auth failed: ") + lastSshError(session, rc);
            }
        }
    }

    if (!authed) {
        error = authReason.length() ? authReason : String("Authentication failed");
        libssh2_session_disconnect(session, "auth failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    LIBSSH2_CHANNEL* ch = libssh2_channel_open_session(session);
    if (!ch) {
        error = String("Open SSH channel failed: ") + lastSshError(session, 0);
        libssh2_session_disconnect(session, "channel open failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    const String& term = profile.terminal.length() ? profile.terminal : String("xterm-256color");
    rc = libssh2_channel_request_pty_ex(ch, term.c_str(), static_cast<unsigned int>(term.length()),
                                        nullptr, 0, cols, rows, 0, 0);
    if (rc != 0) {
        error = String("PTY request failed: ") + lastSshError(session, rc);
        libssh2_channel_free(ch);
        libssh2_session_disconnect(session, "pty failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    rc = libssh2_channel_shell(ch);
    if (rc != 0) {
        error = String("Shell request failed: ") + lastSshError(session, rc);
        libssh2_channel_free(ch);
        libssh2_session_disconnect(session, "shell failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }

    // libssh2's non-blocking setting does not change the underlying socket.
    // Its transport drains packets until recv() returns EAGAIN, including
    // packets for the bridge channel. A blocking socket can stall that loop.
    if (!sshSocketSetNonblocking(sock)) {
        error = "Cannot set SSH socket non-blocking";
        libssh2_channel_free(ch);
        libssh2_session_disconnect(session, "socket setup failed");
        libssh2_session_free(session);
        close(sock);
        return false;
    }
    libssh2_keepalive_config(session, 0, 15);
    libssh2_session_set_blocking(session, 0);

    sock_ = sock;
    session_ = session;
    channel_ = ch;
    bridgeChannel_ = nullptr;
    connected_ = true;
    lastError_.clear();
    lastKeepaliveMs_ = nowMs();
    lastCols_ = cols;
    lastRows_ = rows;
    return true;
}

void SshClient::disconnect() {
    auto* bridge = static_cast<LIBSSH2_CHANNEL*>(bridgeChannel_);
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel_);
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    if (session) {
        libssh2_session_set_blocking(session, 1);
    }
    if (bridge) {
        libssh2_channel_close(bridge);
        libssh2_channel_free(bridge);
        bridgeChannel_ = nullptr;
    }
    if (ch) {
        libssh2_channel_close(ch);
        libssh2_channel_free(ch);
        channel_ = nullptr;
    }
    if (session) {
        libssh2_session_disconnect(session, "Normal Shutdown");
        libssh2_session_free(session);
        session_ = nullptr;
    }
    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }
    connected_ = false;
    lastCols_ = 0;
    lastRows_ = 0;
}

int SshClient::read(uint8_t* buffer, size_t maxLen) {
    if (!connected_ || !channel_ || !session_ || !buffer || maxLen == 0) {
        return 0;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel_);

    ssize_t n = libssh2_channel_read(ch, reinterpret_cast<char*>(buffer), maxLen);
    if (n > 0) {
        return static_cast<int>(n);
    }
    if (n == LIBSSH2_ERROR_EAGAIN || n == 0) {
        if (libssh2_channel_eof(ch)) {
            lastError_ = "remote shell exited (EOF)";
            disconnect();
            return -1;
        }
        uint32_t now = nowMs();
        if (now - lastKeepaliveMs_ >= 10000) {
            int nextSec = 0;
            libssh2_keepalive_send(session, &nextSec);
            lastKeepaliveMs_ = now;
        }
        return 0;
    }
    lastError_ = String("SSH read rc=") + static_cast<int>(n) + ": " +
                 lastSshError(session, static_cast<int>(n));
    if (n == LIBSSH2_ERROR_SOCKET_RECV) {
        if (lastReceiveStatus_ == -1) lastError_ += " (TCP EOF)";
        else if (lastReceiveStatus_ == -2) lastError_ += " (SSH receive buffer full)";
        else if (lastReceiveStatus_ > 0) lastError_ += String(" (socket errno=") + lastReceiveStatus_ + ")";
    }
    disconnect();
    return -1;
}

bool SshClient::sendChannelAll(void* rawCh, const uint8_t* data, size_t len, uint32_t timeoutMs) {
    if (!connected_ || !rawCh || !session_ || (!data && len > 0)) return false;
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(rawCh);
    size_t sent = 0;
    uint32_t start = nowMs();
    while (sent < len) {
        const size_t chunk = std::min<size_t>(len - sent, 512);
        ssize_t n = libssh2_channel_write(ch, reinterpret_cast<const char*>(data + sent), chunk);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN || n == 0) {
            if (nowMs() - start > timeoutMs) {
                lastError_ = "channel write timeout";
                return false;
            }
            waitSocket(sock_, session, 20);
            continue;
        }
        lastError_ = lastSshError(session, static_cast<int>(n));
        return false;
    }
    return true;
}

bool SshClient::write(const uint8_t* data, size_t len) {
    return sendChannelAll(channel_, data, len, 4000);
}

void SshClient::resizePty(int cols, int rows) {
    if (!connected_ || !channel_ || !session_) return;
    if (cols == lastCols_ && rows == lastRows_) return;
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(channel_);
    uint32_t start = nowMs();
    int rc = LIBSSH2_ERROR_EAGAIN;
    while ((rc = libssh2_channel_request_pty_size(ch, cols, rows)) == LIBSSH2_ERROR_EAGAIN) {
        if (nowMs() - start > 1500) break;
        waitSocket(sock_, session, 20);
    }
    if (rc == 0) {
        lastCols_ = cols;
        lastRows_ = rows;
    }
}

bool SshClient::execCommand(const String& command, String& output, String& error, uint32_t timeoutMs) {
    output.clear();
    if (!connected_ || !session_) {
        error = "SSH not connected";
        return false;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    uint32_t start = nowMs();

    LIBSSH2_CHANNEL* ch = nullptr;
    while (!(ch = libssh2_channel_open_session(session))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN || (nowMs() - start > timeoutMs)) {
            error = String("exec channel failed: ") + lastSshError(session, 0);
            return false;
        }
        waitSocket(sock_, session, 20);
    }

    int rc = 0;
    while ((rc = libssh2_channel_exec(ch, command.c_str())) == LIBSSH2_ERROR_EAGAIN) {
        if (nowMs() - start > timeoutMs) {
            libssh2_channel_free(ch);
            error = "exec timeout";
            return false;
        }
        waitSocket(sock_, session, 20);
    }
    if (rc != 0) {
        error = String("exec error: ") + lastSshError(session, rc);
        libssh2_channel_free(ch);
        return false;
    }

    char buf[512];
    for (;;) {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0) {
            output += String(std::string(buf, static_cast<size_t>(n)));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() - start > timeoutMs) {
                libssh2_channel_close(ch);
                libssh2_channel_free(ch);
                error = "exec read timeout";
                return false;
            }
            waitSocket(sock_, session, 20);
            continue;
        }
        break;
    }

    while (libssh2_channel_close(ch) == LIBSSH2_ERROR_EAGAIN && (nowMs() - start <= timeoutMs)) {
        waitSocket(sock_, session, 20);
    }
    int exitCode = libssh2_channel_get_exit_status(ch);
    libssh2_channel_free(ch);
    if (exitCode != 0) {
        error = String("exit ") + exitCode;
        return false;
    }
    return true;
}

bool SshClient::scpDownloadFile(const String& remotePath, const String& localFullPath, String& error) {
    if (!connected_ || !session_) {
        error = "SSH not connected";
        return false;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    libssh2_struct_stat st{};
    LIBSSH2_CHANNEL* ch = nullptr;
    uint32_t start = nowMs();

    while (!(ch = libssh2_scp_recv2(session, remotePath.c_str(), &st))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN || (nowMs() - start > 15000)) {
            error = String("scp recv open failed: ") + lastSshError(session, 0);
            return false;
        }
        waitSocket(sock_, session, 20);
    }

    FILE* fp = fopen(localFullPath.c_str(), "wb");
    if (!fp) {
        libssh2_channel_free(ch);
        error = String("cannot open local file ") + localFullPath;
        return false;
    }

    libssh2_struct_stat_size remaining = st.st_size;
    char buf[2048];
    bool ok = true;
    while (remaining > 0) {
        size_t chunk = static_cast<size_t>(remaining > static_cast<libssh2_struct_stat_size>(sizeof(buf))
                                               ? sizeof(buf)
                                               : remaining);
        ssize_t n = libssh2_channel_read(ch, buf, chunk);
        if (n > 0) {
            if (fwrite(buf, 1, static_cast<size_t>(n), fp) != static_cast<size_t>(n)) {
                error = "SD write error";
                ok = false;
                break;
            }
            remaining -= n;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            waitSocket(sock_, session, 50);
        } else {
            error = String("scp read failed: ") + lastSshError(session, static_cast<int>(n));
            ok = false;
            break;
        }
    }
    fclose(fp);
    libssh2_channel_free(ch);
    return ok;
}

bool SshClient::scpUploadMemory(const uint8_t* data, size_t len, const String& remotePath, String& error, int mode) {
    if (!connected_ || !session_ || (!data && len > 0)) {
        error = "SSH not connected";
        return false;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    LIBSSH2_CHANNEL* ch = nullptr;
    uint32_t start = nowMs();
    while (!(ch = libssh2_scp_send64(session, remotePath.c_str(), mode, static_cast<libssh2_int64_t>(len), 0, 0))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN || (nowMs() - start > 15000)) {
            error = String("scp send open failed: ") + lastSshError(session, 0);
            return false;
        }
        waitSocket(sock_, session, 20);
    }

    bool ok = sendChannelAll(ch, data, len, 15000);
    if (!ok) error = "scp write failed";
    finishScpChannel(sock_, session, ch, ok);
    return ok;
}

bool SshClient::scpUploadFile(const String& localFullPath, const String& remotePath, String& error) {
    FILE* fp = fopen(localFullPath.c_str(), "rb");
    if (!fp) {
        error = String("cannot open ") + localFullPath;
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        error = "file size failed";
        return false;
    }

    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    if (!connected_ || !session) {
        fclose(fp);
        error = "SSH not connected";
        return false;
    }
    LIBSSH2_CHANNEL* ch = nullptr;
    uint32_t start = nowMs();
    while (!(ch = libssh2_scp_send64(session, remotePath.c_str(), 0644, static_cast<libssh2_int64_t>(sz), 0, 0))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN || (nowMs() - start > 15000)) {
            fclose(fp);
            error = String("scp send open failed: ") + lastSshError(session, 0);
            return false;
        }
        waitSocket(sock_, session, 20);
    }

    uint8_t buf[2048];
    bool ok = true;
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        if (!sendChannelAll(ch, buf, n, 15000)) {
            error = "scp stream write failed";
            ok = false;
            break;
        }
    }
    fclose(fp);
    finishScpChannel(sock_, session, ch, ok);
    return ok;
}

bool SshClient::probeHost(const String& host, uint16_t port, int timeoutMs, uint32_t& latencyMs, String& info) {
    info.clear();
    latencyMs = 0;
    if (!host.length()) {
        info = "empty host";
        return false;
    }
    uint16_t targetPort = port ? port : 22;
    uint32_t start = nowMs();
    int sock = connectTcpWithTimeout(host.c_str(), targetPort, timeoutMs, info);
    if (sock < 0) {
        return false;
    }
    latencyMs = nowMs() - start;

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 350000;
    if (select(sock + 1, &rfds, nullptr, nullptr, &tv) > 0) {
        char banner[96]{};
        ssize_t n = recv(sock, banner, sizeof(banner) - 1, 0);
        if (n > 0) {
            banner[n] = '\0';
            info = String(banner);
            info.trim();
        }
    }
    close(sock);
    if (!info.length()) {
        info = String("TCP port ") + static_cast<unsigned int>(targetPort) + " open";
    }
    return true;
}

bool SshClient::startBridge(const String& command, String& error) {
    stopBridge();
    if (!connected_ || !session_) {
        error = "SSH not connected";
        return false;
    }
    auto* session = static_cast<LIBSSH2_SESSION*>(session_);
    uint32_t start = nowMs();
    LIBSSH2_CHANNEL* ch = nullptr;
    while (!(ch = libssh2_channel_open_session(session))) {
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN || (nowMs() - start > 8000)) {
            error = String("bridge channel open failed: ") + lastSshError(session, 0);
            return false;
        }
        waitSocket(sock_, session, 20);
    }
    int rc = 0;
    while ((rc = libssh2_channel_exec(ch, command.c_str())) == LIBSSH2_ERROR_EAGAIN) {
        if (nowMs() - start > 8000) {
            libssh2_channel_free(ch);
            error = "bridge exec timeout";
            return false;
        }
        waitSocket(sock_, session, 20);
    }
    if (rc != 0) {
        error = String("bridge exec error: ") + lastSshError(session, rc);
        libssh2_channel_free(ch);
        return false;
    }
    bridgeChannel_ = ch;
    return true;
}

void SshClient::stopBridge() {
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(bridgeChannel_);
    if (!ch) return;
    libssh2_channel_close(ch);
    libssh2_channel_free(ch);
    bridgeChannel_ = nullptr;
}

int SshClient::readBridge(uint8_t* buffer, size_t maxLen) {
    if (!connected_ || !bridgeChannel_ || !buffer || maxLen == 0) return 0;
    auto* ch = static_cast<LIBSSH2_CHANNEL*>(bridgeChannel_);
    ssize_t n = libssh2_channel_read(ch, reinterpret_cast<char*>(buffer), maxLen);
    if (n > 0) return static_cast<int>(n);
    if (n == LIBSSH2_ERROR_EAGAIN || n == 0) {
        if (libssh2_channel_eof(ch)) {
            stopBridge();
            return -1;
        }
        return 0;
    }
    stopBridge();
    return -1;
}

bool SshClient::writeBridge(const uint8_t* data, size_t len) {
    return sendChannelAll(bridgeChannel_, data, len, 4000);
}
