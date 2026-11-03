// Adapted from Tab5 Terminal; see PROVENANCE.md. Shell uses strict SD pins, no TOFU/NVS writes.
#include "ssh_connection.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "mbedtls/sha256.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "libssh2.h"

#include "shell_network.h"
#include <fcntl.h>

namespace tab5 {

namespace {

constexpr const char* kTag       = "ssh";
constexpr size_t kTxStreamBytes  = 4096;
constexpr size_t kRxChunk        = 1024;

// libssh2 session state owned by ssh_task once start() succeeds.
struct Session {
    LIBSSH2_SESSION* sess = nullptr;
    LIBSSH2_CHANNEL* chan = nullptr;
    int sock = -1;
    StreamBufferHandle_t tx = nullptr;
    ByteSink rx_apply;
    std::atomic<bool> running{false};
    // True from ssh_task entry until ssh_task has fully torn down its
    // resources. start() refuses to re-enter while this is true so that
    // a reconnect attempt from the supervisor can't race with the old
    // task's shutdown_session().
    std::atomic<bool> task_alive{false};
    // Pending pty resize. Producer: any thread via SshConnection::resize.
    // Consumer: ssh_task on its next poll. cols==0 means "no pending".
    std::atomic<uint16_t> pending_cols{0};
    std::atomic<uint16_t> pending_rows{0};
};

Session g_sess;
std::atomic<SshFailure> g_failure{SshFailure::none};
std::atomic<bool> g_first_use{false};

void fail(SshFailure failure) {
    g_failure.store(failure);
}

void wipe(void* data, size_t size) {
    volatile uint8_t* bytes = static_cast<volatile uint8_t*>(data);
    while (size--) *bytes++ = 0;
}

void close_sock_locked() {
    if (g_sess.sock >= 0) {
        close(g_sess.sock);
        g_sess.sock = -1;
    }
}

void shutdown_session() {
    g_sess.running = false;
    if (g_sess.chan) {
        libssh2_channel_free(g_sess.chan);
        g_sess.chan = nullptr;
    }
    if (g_sess.sess) {
        libssh2_session_disconnect(g_sess.sess, "shutdown");
        libssh2_session_free(g_sess.sess);
        g_sess.sess = nullptr;
    }
    close_sock_locked();

}

bool dial(const char* host, uint16_t port) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    const char* dial_host = host;

    struct addrinfo hints = {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    int rc = getaddrinfo(dial_host, port_str, &hints, &res);
    if (rc != 0 || !res) {
        ESP_LOGE(kTag, "getaddrinfo failed: %d target=REDACTED", rc);
        return false;
    }

    int s = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) continue;
        int flags = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, flags | O_NONBLOCK);
        int connected = connect(s, ai->ai_addr, ai->ai_addrlen);
        if (connected != 0 && errno == EINPROGRESS) {
            fd_set writes; FD_ZERO(&writes); FD_SET(s, &writes);
            timeval timeout{10,0};
            if (select(s+1, nullptr, &writes, nullptr, &timeout) > 0) {
                int error=0; socklen_t length=sizeof(error);
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0) connected=0;
            }
        }
        if (connected == 0) { fcntl(s, F_SETFL, flags); break; }
        close(s);
        s = -1;
    }
    freeaddrinfo(res);

    if (s < 0) {
        ESP_LOGE(kTag, "connect failed: errno=%d target=REDACTED", errno);
        return false;
    }

    // TCP_NODELAY for low-latency keystrokes; SO_KEEPALIVE + tight
    // intervals so a dead remote / Wi-Fi outage is detected in ~60 s
    // (30 s idle + 3*10 s of unacked probes) and the supervisor can
    // reconnect.
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    int idle = 30;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE,  &idle, sizeof(idle));
    int intv = 10;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &intv, sizeof(intv));
    int cnt = 3;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT,   &cnt, sizeof(cnt));

    g_sess.sock = s;
    return true;
}

bool load_private_key(const char* path, std::vector<char>& key) {
    key.clear();
    if (!path || !path[0] || std::strncmp(path, "/private/", 9) != 0 ||
        std::strstr(path, "..") || std::strchr(path, '\\')) return false;
    char full_path[192];
    const int written = std::snprintf(full_path, sizeof(full_path), "/sdcard%s", path);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(full_path)) return false;
    std::unique_ptr<FILE, decltype(&std::fclose)> file(
        std::fopen(full_path, "rb"), &std::fclose);
    if (!file || std::fseek(file.get(), 0, SEEK_END) != 0) return false;
    const long length = std::ftell(file.get());
    if (length < 64 || length > 16384 || std::fseek(file.get(), 0, SEEK_SET) != 0)
        return false;
    key.resize(static_cast<size_t>(length) + 1, 0);
    if (std::fread(key.data(), 1, static_cast<size_t>(length), file.get()) !=
        static_cast<size_t>(length)) {
        wipe(key.data(), key.size());
        key.clear();
        return false;
    }
    return true;
}

// SHA-256 over the host key bytes. libssh2_session_hostkey() returns the
// raw key blob — what we hash for TOFU.
bool hostkey_fingerprint_sha256(LIBSSH2_SESSION* s, uint8_t out[32]) {
    size_t len = 0;
    int type = 0;
    const char* key = libssh2_session_hostkey(s, &len, &type);
    if (!key || !len) return false;
    int rc = mbedtls_sha256(reinterpret_cast<const uint8_t*>(key), len, out, 0);
    return rc == 0;
}

bool configure_algorithms(LIBSSH2_SESSION* session) {
    // Keep the proven Contextual Micro policy: modern SHA-2 negotiation,
    // bounded AES-CTR transport, no compression, and RSA SHA-2 signatures.
    // Reject the session rather than silently falling back to SHA-1/CBC.
    return !libssh2_session_method_pref(
               session, LIBSSH2_METHOD_KEX,
               "ecdh-sha2-nistp256,ecdh-sha2-nistp384,diffie-hellman-group14-sha256") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY,
                                        "ecdsa-sha2-nistp256") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS,
                                        "aes128-ctr,aes256-ctr") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_SC,
                                        "aes128-ctr,aes256-ctr") &&
           !libssh2_session_method_pref(
               session, LIBSSH2_METHOD_MAC_CS,
               "hmac-sha2-256-etm@openssh.com,hmac-sha2-256") &&
           !libssh2_session_method_pref(
               session, LIBSSH2_METHOD_MAC_SC,
               "hmac-sha2-256-etm@openssh.com,hmac-sha2-256") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_CS, "none") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_SC, "none") &&
           !libssh2_session_method_pref(session, LIBSSH2_METHOD_SIGN_ALGO,
                                        "rsa-sha2-512,rsa-sha2-256");
}

// TOFU storage lives in main/tofu.{hpp,cpp}; this function is just a
// thin alias kept for diff readability against earlier history.
int tofu_check_or_record(const char* host, uint16_t port,
                         const uint8_t fp[32], bool* first_use_out) {
    if (first_use_out) *first_use_out = false;
    return tab5_shell::verify_host_key(host, port, fp) ? 0 : -1;
}

void ssh_task(void* /*arg*/) {
    g_sess.task_alive.store(true);
    uint8_t rxbuf[kRxChunk];
    uint8_t txbuf[kRxChunk];
    int64_t next_ka_us = esp_timer_get_time();

    while (g_sess.running) {
        // libssh2 doesn't fire keepalives on its own — poke it once a
        // second and let it decide when to actually send a packet.
        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_ka_us) {
            int seconds_to_next = 0;
            int kr = libssh2_keepalive_send(g_sess.sess, &seconds_to_next);
            if (kr != 0 && kr != LIBSSH2_ERROR_EAGAIN) {
                ESP_LOGW(kTag, "keepalive_send -> %d; treating as dropped", kr);
                g_sess.running = false;
                break;
            }
            next_ka_us = now_us +
                (seconds_to_next > 0 ? seconds_to_next : 1) * 1000000LL;
        }
        // Honour any pending pty resize before the read loop. exchange()
        // hands us a single resize request and clears the slot.
        uint16_t pc = g_sess.pending_cols.exchange(0);
        if (pc != 0) {
            uint16_t pr = g_sess.pending_rows.load();
            int rc = libssh2_channel_request_pty_size(g_sess.chan, pc, pr);
            if (rc != 0) {
                ESP_LOGW(kTag, "pty_size(%u,%u) -> %d", pc, pr, rc);
            }
        }
        // Drain RX until EAGAIN / EOF.
        for (;;) {
            ssize_t n = libssh2_channel_read(g_sess.chan,
                                             reinterpret_cast<char*>(rxbuf),
                                             sizeof(rxbuf));
            if (n > 0) {
                if (g_sess.rx_apply) {
                    g_sess.rx_apply(std::span<const uint8_t>(rxbuf,
                                                             static_cast<size_t>(n)));
                }
                continue;
            }
            if (n == LIBSSH2_ERROR_EAGAIN || (n == 0 && !libssh2_channel_eof(g_sess.chan))) break;
            if (n == 0 || libssh2_channel_eof(g_sess.chan)) {
                ESP_LOGW(kTag, "remote closed channel");
                g_sess.running = false;
                break;
            }
            ESP_LOGE(kTag, "channel_read -> %d", static_cast<int>(n));
            g_sess.running = false;
            break;
        }
        if (!g_sess.running) break;

        // Drain TX stream buffer.
        size_t got = xStreamBufferReceive(g_sess.tx, txbuf, sizeof(txbuf), 0);
        size_t off = 0;
        while (got > 0 && g_sess.running) {
            ssize_t w = libssh2_channel_write(g_sess.chan,
                                              reinterpret_cast<const char*>(txbuf + off),
                                              got);
            if (w == LIBSSH2_ERROR_EAGAIN) {
                // Push the unsent bytes back to the front for the next pass.
                // Simpler: just loop with a tiny yield.
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            if (w <= 0) {
                ESP_LOGE(kTag, "channel_write -> %d", static_cast<int>(w));
                g_sess.running = false;
                break;
            }
            got -= static_cast<size_t>(w);
            off += static_cast<size_t>(w);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(kTag, "ssh_task exiting");
    shutdown_session();
    g_sess.task_alive.store(false);
    vTaskDelete(nullptr);
}

}  // namespace

SshConnection::SshConnection(const SshConfig& cfg) : cfg_(cfg) {
    snprintf(label_, sizeof(label_), "%s@%s",
             cfg.user ? cfg.user : "?",
             cfg.host ? cfg.host : "?");
}

SshConnection::~SshConnection() { stop(); }

bool SshConnection::start(ByteSink rx_sink) {
    const SshConfig& cfg = cfg_;
    g_failure.store(SshFailure::none);
    g_first_use.store(false);
    if (g_sess.task_alive.load()) {
        ESP_LOGD(kTag, "previous task still tearing down — caller should retry");
        fail(SshFailure::resources);
        return false;
    }
    if (g_sess.running) {
        ESP_LOGW(kTag, "already running");
        fail(SshFailure::resources);
        return false;
    }

    if (libssh2_init(0) != 0) {
        ESP_LOGE(kTag, "libssh2_init failed");
        fail(SshFailure::session);
        return false;
    }

    if (!dial(cfg.host, cfg.port)) {
        fail(SshFailure::network);
        return false;
    }

    g_sess.sess = libssh2_session_init();
    if (!g_sess.sess) {
        ESP_LOGE(kTag, "session_init failed");
        fail(SshFailure::session);
        close_sock_locked();
        return false;
    }

    if (!configure_algorithms(g_sess.sess)) {
        ESP_LOGE(kTag, "secure SSH algorithm policy unavailable");
        fail(SshFailure::handshake);
        shutdown_session();
        return false;
    }

    // Blocking mode for handshake / auth — simpler and the user is already
    // waiting on this call.
    libssh2_session_set_blocking(g_sess.sess, 1);
    libssh2_session_set_timeout(g_sess.sess, 10000);

    int rc = libssh2_session_handshake(g_sess.sess, g_sess.sock);
    if (rc != 0) {
        ESP_LOGE(kTag, "handshake -> %d", rc);
        fail(SshFailure::handshake);
        shutdown_session();
        return false;
    }

    // SSH-level keepalive: ask the remote to reply, every 30 s.
    libssh2_keepalive_config(g_sess.sess, /*want_reply=*/1, 30);

    // TOFU host key check.
    uint8_t fp[32];
    if (!hostkey_fingerprint_sha256(g_sess.sess, fp)) {
        ESP_LOGE(kTag, "could not read host key");
        fail(SshFailure::host_key);
        shutdown_session();
        return false;
    }
    bool first_use = false;
    int tofu = tofu_check_or_record(cfg.host, cfg.port, fp, &first_use);
    if (tofu == -1) {
        ESP_LOGE(kTag, "HOST KEY MISMATCH target=REDACTED");
        fail(SshFailure::host_key_changed);
        shutdown_session();
        return false;
    }
    if (tofu == -2) {
        ESP_LOGE(kTag, "TOFU NVS unavailable; refusing unverified connection");
        fail(SshFailure::host_key_store);
        shutdown_session();
        return false;
    }
    g_first_use.store(first_use);
    ESP_LOGI(kTag, "host key verified first_use=%d fingerprint=WITHHELD", first_use);

    // Ask the server which userauth methods it advertises — useful when
    // diagnosing why publickey or password is silently rejected.
    {
        size_t ulen = std::strlen(cfg.user);
        char* methods = libssh2_userauth_list(g_sess.sess, cfg.user, ulen);
        if (methods) {
            ESP_LOGI(kTag, "server userauth methods: %s", methods);
        } else {
            ESP_LOGW(kTag, "userauth_list returned null (server may have "
                           "already authenticated as 'none')");
        }
    }

    rc = -1;  // start at "no auth attempted yet"
    std::vector<char> private_key;
    if (cfg.private_key_file && cfg.private_key_file[0]) {
        if (!load_private_key(cfg.private_key_file, private_key)) {
            fail(SshFailure::private_key);
            shutdown_session();
            return false;
        }
        ESP_LOGI(kTag, "publickey auth: loaded private SD key bytes=WITHHELD");
        rc = libssh2_userauth_publickey_frommemory(
            g_sess.sess, cfg.user, std::strlen(cfg.user),
            nullptr, 0,
            private_key.data(), private_key.size() - 1,
            cfg.private_key_passphrase ? cfg.private_key_passphrase : "");
        wipe(private_key.data(), private_key.size());
        private_key.clear();
        if (rc == 0) {
            ESP_LOGI(kTag, "publickey auth OK");
        } else {
            int err_code = libssh2_session_last_error(g_sess.sess,
                                                      nullptr, nullptr, 0);
            ESP_LOGW(kTag,
                     "publickey auth failed: rc=%d session_last_error=%d; "
                     "falling back to password if configured",
                     rc, err_code);
        }
    }
    if (rc != 0 && cfg.password && cfg.password[0]) {
        rc = libssh2_userauth_password(g_sess.sess, cfg.user, cfg.password);
        if (rc != 0) {
            int err_code = libssh2_session_last_error(g_sess.sess,
                                                      nullptr, nullptr, 0);
            ESP_LOGE(kTag, "password auth failed: rc=%d session_last_error=%d",
                     rc, err_code);
        }
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "auth failed: %d", rc);
        fail(SshFailure::authentication);
        shutdown_session();
        return false;
    }

    g_sess.chan = libssh2_channel_open_session(g_sess.sess);
    if (!g_sess.chan) {
        ESP_LOGE(kTag, "open_session failed");
        fail(SshFailure::channel);
        shutdown_session();
        return false;
    }

    if (libssh2_channel_request_pty_ex(g_sess.chan, "xterm-256color", 14,
                                       nullptr, 0,
                                       cfg.cols ? cfg.cols : 80,
                                       cfg.rows ? cfg.rows : 24,
                                       LIBSSH2_TERM_WIDTH_PX,
                                       LIBSSH2_TERM_HEIGHT_PX) != 0) {
        ESP_LOGE(kTag, "request_pty failed");
        fail(SshFailure::pty);
        shutdown_session();
        return false;
    }

    if (libssh2_channel_shell(g_sess.chan) != 0) {
        ESP_LOGE(kTag, "channel_shell failed");
        fail(SshFailure::shell);
        shutdown_session();
        return false;
    }

    // Switch to non-blocking for the steady-state read/write loop.
    libssh2_session_set_blocking(g_sess.sess, 0);
    int flags = fcntl(g_sess.sock, F_GETFL, 0);
    fcntl(g_sess.sock, F_SETFL, flags | O_NONBLOCK);

    if (g_sess.tx) { vStreamBufferDelete(g_sess.tx); g_sess.tx = nullptr; }
    g_sess.tx = xStreamBufferCreate(kTxStreamBytes, 1);
    if (!g_sess.tx) {
        ESP_LOGE(kTag, "stream buffer alloc failed");
        fail(SshFailure::resources);
        shutdown_session();
        return false;
    }
    g_sess.rx_apply = std::move(rx_sink);
    g_sess.running = true;

    g_sess.task_alive.store(true);
    BaseType_t ok = xTaskCreate(&ssh_task, "ssh_task",
                                cfg.task_stack_bytes ? cfg.task_stack_bytes : 8192,
                                nullptr,
                                tskIDLE_PRIORITY + 5, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(kTag, "task spawn failed");
        fail(SshFailure::resources);
        g_sess.task_alive.store(false);
        g_sess.running = false;
        shutdown_session();
        return false;
    }
    ESP_LOGI(kTag, "shell up");
    g_failure.store(SshFailure::none);
    return true;
}

void SshConnection::send(std::span<const uint8_t> bytes) {
    if (!g_sess.running || !g_sess.tx) return;
    xStreamBufferSend(g_sess.tx, bytes.data(), bytes.size(), 0);
}

void SshConnection::resize(uint16_t cols, uint16_t rows) {
    if (!g_sess.running || cols == 0 || rows == 0) return;
    // Rows first so the consumer's exchange(cols=0) check never sees a
    // half-published pair.
    g_sess.pending_rows.store(rows);
    g_sess.pending_cols.store(cols);
}

bool SshConnection::is_connected() const {
    return g_sess.running;
}

void SshConnection::stop() {
    if (!g_sess.running) return;
    g_sess.running = false;
    // ssh_task observes !running on its next tick, exits its loop, and
    // calls shutdown_session() before vTaskDelete. We don't join here —
    // teardown is cooperative.
}

SshFailure ssh_last_failure() { return g_failure.load(); }

bool ssh_host_was_trusted_on_first_use() { return g_first_use.load(); }

bool ssh_transport_idle() { return !g_sess.task_alive.load() && !g_sess.running.load(); }

const char* ssh_failure_label(SshFailure failure) {
    switch (failure) {
        case SshFailure::none: return "--";
        case SshFailure::network: return "Network connection failed";
        case SshFailure::session: return "SSH session unavailable";
        case SshFailure::handshake: return "SSH handshake failed";
        case SshFailure::host_key: return "Host key unavailable";
        case SshFailure::host_key_changed: return "HOST KEY CHANGED";
        case SshFailure::host_key_store: return "Host trust store unavailable";
        case SshFailure::private_key: return "Private key could not be loaded";
        case SshFailure::authentication: return "Authentication failed";
        case SshFailure::channel: return "SSH channel failed";
        case SshFailure::pty: return "PTY request failed";
        case SshFailure::shell: return "Remote shell failed";
        case SshFailure::resources: return "SSH resources unavailable";
    }
    return "SSH failed";
}

}  // namespace tab5
