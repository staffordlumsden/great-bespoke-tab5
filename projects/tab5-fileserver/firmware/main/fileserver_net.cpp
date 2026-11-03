#include "fileserver_net.hpp"
#include "fs_core.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"

namespace tab5_fs {

namespace {

constexpr const char* kHotspotSsid = "Tab5-FileServer";
constexpr const char* kMdnsHost = "tab5.local";

SemaphoreHandle_t s_lock = nullptr;
NetStatus s_status{};
ScannedNetwork s_networks[kMaxScannedNetworks]{};
wifi_ap_record_t s_scan_records[kMaxScannedNetworks]{};
esp_netif_t* s_sta_netif = nullptr;
esp_netif_t* s_ap_netif = nullptr;
bool s_wifi_started = false;
char s_passcode[9] = "12345678";
char s_saved_ssid[kSsidMaxLen] = {0};
char s_saved_pass[65] = {0};
uint8_t s_ip_octets[4] = {192, 168, 4, 1};

void lock_state() {
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void unlock_state() {
    if (s_lock) xSemaphoreGive(s_lock);
}

void set_msg_locked(const char* msg) {
    if (msg) std::snprintf(s_status.status_msg, sizeof(s_status.status_msg), "%s", msg);
    ++s_status.revision;
}

void copy_ascii(char* dst, size_t cap, const uint8_t* src, size_t len) {
    if (!dst || cap == 0) return;
    size_t u = 0;
    for (size_t i = 0; i < len && u + 1 < cap; ++i) {
        const uint8_t v = src[i];
        if (v == 0) break;
        dst[u++] = (v >= 32 && v <= 126) ? static_cast<char>(v) : '?';
    }
    dst[u] = '\0';
}

void load_console_nvs_wifi() {
    nvs_handle_t h = 0;
    if (nvs_open("tab5console", NVS_READONLY, &h) == ESP_OK) {
        size_t slen = sizeof(s_saved_ssid);
        if (nvs_get_str(h, "wifi_ssid", s_saved_ssid, &slen) != ESP_OK) {
            s_saved_ssid[0] = '\0';
        }
        size_t plen = sizeof(s_saved_pass);
        if (nvs_get_str(h, "wifi_pass", s_saved_pass, &plen) != ESP_OK) {
            s_saved_pass[0] = '\0';
        }
        nvs_close(h);
    }
    if (!s_saved_ssid[0]) {
        wifi_config_t cfg{};
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0]) {
            copy_ascii(s_saved_ssid, sizeof(s_saved_ssid), cfg.sta.ssid,
                       strnlen(reinterpret_cast<const char*>(cfg.sta.ssid), sizeof(cfg.sta.ssid)));
            copy_ascii(s_saved_pass, sizeof(s_saved_pass), cfg.sta.password,
                       strnlen(reinterpret_cast<const char*>(cfg.sta.password), sizeof(cfg.sta.password)));
        }
    }
}

void save_console_nvs_wifi(const char* ssid, const char* pass) {
    if (!ssid || !ssid[0]) return;
    std::snprintf(s_saved_ssid, sizeof(s_saved_ssid), "%s", ssid);
    std::snprintf(s_saved_pass, sizeof(s_saved_pass), "%s", pass ? pass : "");
    nvs_handle_t h = 0;
    if (nvs_open("tab5console", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "wifi_ssid", s_saved_ssid);
        nvs_set_str(h, "wifi_pass", s_saved_pass);
        nvs_commit(h);
        nvs_close(h);
    }
}

void handle_scan_done() {
    uint16_t count = kMaxScannedNetworks;
    std::memset(s_scan_records, 0, sizeof(s_scan_records));
    const esp_err_t res = esp_wifi_scan_get_ap_records(&count, s_scan_records);

    lock_state();
    s_status.scanning = false;
    s_status.scan_count = 0;
    std::memset(s_networks, 0, sizeof(s_networks));
    if (res == ESP_OK) {
        for (uint16_t i = 0; i < count; ++i) {
            if (!s_scan_records[i].ssid[0]) continue;
            char ssid[kSsidMaxLen] = {0};
            copy_ascii(ssid, sizeof(ssid), s_scan_records[i].ssid,
                       strnlen(reinterpret_cast<const char*>(s_scan_records[i].ssid), 32));
            bool dup = false;
            for (size_t j = 0; j < s_status.scan_count; ++j) {
                if (std::strcmp(s_networks[j].ssid, ssid) == 0) {
                    if (s_scan_records[i].rssi > s_networks[j].rssi) {
                        s_networks[j].rssi = s_scan_records[i].rssi;
                        s_networks[j].authmode = s_scan_records[i].authmode;
                        s_networks[j].secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
                    }
                    dup = true;
                    break;
                }
            }
            if (dup || s_status.scan_count >= kMaxScannedNetworks) continue;
            ScannedNetwork& entry = s_networks[s_status.scan_count++];
            std::snprintf(entry.ssid, sizeof(entry.ssid), "%s", ssid);
            entry.rssi = s_scan_records[i].rssi;
            entry.authmode = s_scan_records[i].authmode;
            entry.secure = s_scan_records[i].authmode != WIFI_AUTH_OPEN;
        }
        for (size_t i = 0; i < s_status.scan_count; ++i) {
            for (size_t j = i + 1; j < s_status.scan_count; ++j) {
                if (s_networks[j].rssi > s_networks[i].rssi) {
                    const ScannedNetwork tmp = s_networks[i];
                    s_networks[i] = s_networks[j];
                    s_networks[j] = tmp;
                }
            }
        }
        char msg[64];
        std::snprintf(msg, sizeof(msg), "FOUND %u WIFI NETWORKS", static_cast<unsigned>(s_status.scan_count));
        set_msg_locked(msg);
    } else {
        set_msg_locked("WIFI SCAN FAILED");
    }
    unlock_state();
    esp_wifi_clear_ap_list();
}

void on_wifi_event(void*, esp_event_base_t, int32_t id, void* data) {
    if (id == WIFI_EVENT_SCAN_DONE) {
        handle_scan_done();
    } else if (id == WIFI_EVENT_STA_CONNECTED) {
        const auto* ev = static_cast<const wifi_event_sta_connected_t*>(data);
        lock_state();
        s_status.sta_connected = true;
        s_status.sta_has_ip = false;
        copy_ascii(s_status.sta_ssid, sizeof(s_status.sta_ssid), ev->ssid, ev->ssid_len);
        set_msg_locked("WIFI ASSOCIATED — OBTAINING IP...");
        unlock_state();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        lock_state();
        s_status.sta_connected = false;
        s_status.sta_has_ip = false;
        s_status.sta_rssi = 0;
        if (s_status.mode == NetMode::lan) {
            s_status.ip_address[0] = '\0';
            set_msg_locked("LAN WIFI DISCONNECTED — TAP SCAN OR HOTSPOT");
        }
        unlock_state();
    } else if (id == WIFI_EVENT_AP_START) {
        lock_state();
        s_status.ap_active = true;
        if (s_status.mode == NetMode::hotspot) {
            std::snprintf(s_status.ip_address, sizeof(s_status.ip_address), "192.168.4.1");
            s_ip_octets[0] = 192;
            s_ip_octets[1] = 168;
            s_ip_octets[2] = 4;
            s_ip_octets[3] = 1;
            set_msg_locked("HOTSPOT ACTIVE — JOIN & OPEN 192.168.4.1");
        }
        unlock_state();
    } else if (id == WIFI_EVENT_AP_STOP) {
        lock_state();
        s_status.ap_active = false;
        s_status.ap_clients = 0;
        unlock_state();
    } else if (id == WIFI_EVENT_AP_STACONNECTED) {
        lock_state();
        ++s_status.ap_clients;
        set_msg_locked("CLIENT JOINED HOTSPOT — OPEN 192.168.4.1");
        unlock_state();
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        lock_state();
        if (s_status.ap_clients > 0) --s_status.ap_clients;
        set_msg_locked("CLIENT LEFT HOTSPOT");
        unlock_state();
    }
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
    if (id != IP_EVENT_STA_GOT_IP) return;
    const auto* ev = static_cast<const ip_event_got_ip_t*>(data);
    wifi_ap_record_t ap_info{};
    lock_state();
    s_status.sta_connected = true;
    s_status.sta_has_ip = true;
    esp_ip4addr_ntoa(&ev->ip_info.ip, s_status.ip_address, sizeof(s_status.ip_address));
    const uint32_t ip_u32 = ntohl(ev->ip_info.ip.addr);
    s_ip_octets[0] = static_cast<uint8_t>((ip_u32 >> 24) & 0xFF);
    s_ip_octets[1] = static_cast<uint8_t>((ip_u32 >> 16) & 0xFF);
    s_ip_octets[2] = static_cast<uint8_t>((ip_u32 >> 8) & 0xFF);
    s_ip_octets[3] = static_cast<uint8_t>(ip_u32 & 0xFF);
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        s_status.sta_rssi = ap_info.rssi;
    }
    char msg[64];
    std::snprintf(msg, sizeof(msg), "LAN READY AT HTTP://%s/ (TAB5.LOCAL)", s_status.ip_address);
    set_msg_locked(msg);
    unlock_state();
}

// Background UDP responder for mDNS (224.0.0.251:5353) and Hotspot DNS (0.0.0.0:53)
void dns_mdns_worker(void*) {
    static uint8_t rx_buf[512];
    static uint8_t tx_buf[512];

    int mdns_sock = -1;
    int dns_sock = -1;

    for (;;) {
        if (mdns_sock < 0) {
            mdns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (mdns_sock >= 0) {
                int reuse = 1;
                setsockopt(mdns_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(5353);
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
                if (bind(mdns_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    ip_mreq mreq{};
                    mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
                    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
                    setsockopt(mdns_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
                    timeval tv{0, 200000};
                    setsockopt(mdns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                } else {
                    close(mdns_sock);
                    mdns_sock = -1;
                }
            }
        }

        if (dns_sock < 0) {
            dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (dns_sock >= 0) {
                int reuse = 1;
                setsockopt(dns_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(53);
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
                if (bind(dns_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    timeval tv{0, 100000};
                    setsockopt(dns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                } else {
                    close(dns_sock);
                    dns_sock = -1;
                }
            }
        }

        uint8_t ip[4];
        bool is_hotspot = false;
        lock_state();
        std::memcpy(ip, s_ip_octets, 4);
        is_hotspot = (s_status.mode == NetMode::hotspot);
        unlock_state();

        if (mdns_sock >= 0) {
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            const ssize_t n = recvfrom(mdns_sock, rx_buf, sizeof(rx_buf), 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n > 12) {
                const size_t resp_len = handle_dns_query(
                    rx_buf, static_cast<size_t>(n),
                    ip[0], ip[1], ip[2], ip[3],
                    false, true, tx_buf, sizeof(tx_buf));
                if (resp_len > 0) {
                    // Reply both to requester and mDNS multicast group
                    sendto(mdns_sock, tx_buf, resp_len, 0,
                           reinterpret_cast<sockaddr*>(&from), from_len);
                    sockaddr_in mcast{};
                    mcast.sin_family = AF_INET;
                    mcast.sin_port = htons(5353);
                    mcast.sin_addr.s_addr = inet_addr("224.0.0.251");
                    sendto(mdns_sock, tx_buf, resp_len, 0,
                           reinterpret_cast<sockaddr*>(&mcast), sizeof(mcast));
                }
            }
        }

        if (dns_sock >= 0) {
            sockaddr_in from{};
            socklen_t from_len = sizeof(from);
            const ssize_t n = recvfrom(dns_sock, rx_buf, sizeof(rx_buf), 0,
                                       reinterpret_cast<sockaddr*>(&from), &from_len);
            if (n > 12 && is_hotspot) {
                const size_t resp_len = handle_dns_query(
                    rx_buf, static_cast<size_t>(n),
                    192, 168, 4, 1,
                    false, false, tx_buf, sizeof(tx_buf));
                if (resp_len > 0) {
                    sendto(dns_sock, tx_buf, resp_len, 0,
                           reinterpret_cast<sockaddr*>(&from), from_len);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t apply_hotspot_mode_locked() {
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) return err;

    wifi_config_t ap_cfg{};
    std::snprintf(reinterpret_cast<char*>(ap_cfg.ap.ssid), sizeof(ap_cfg.ap.ssid), "%s", kHotspotSsid);
    ap_cfg.ap.ssid_len = static_cast<uint8_t>(std::strlen(kHotspotSsid));
    std::snprintf(reinterpret_cast<char*>(ap_cfg.ap.password), sizeof(ap_cfg.ap.password), "%s", s_passcode);
    ap_cfg.ap.channel = 6;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (err != ESP_OK) return err;

    err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_started = true;
        s_status.mode = NetMode::hotspot;
        s_status.sta_connected = false;
        s_status.sta_has_ip = false;
        s_status.ap_active = true;
        std::snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "%s", kHotspotSsid);
        std::snprintf(s_status.ip_address, sizeof(s_status.ip_address), "192.168.4.1");
        s_ip_octets[0] = 192;
        s_ip_octets[1] = 168;
        s_ip_octets[2] = 4;
        s_ip_octets[3] = 1;
        set_msg_locked("HOTSPOT ACTIVE — JOIN & OPEN 192.168.4.1");
    }
    return err;
}

esp_err_t apply_lan_mode_locked() {
    if (s_wifi_started) {
        esp_wifi_stop();
        s_wifi_started = false;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;

    s_status.mode = NetMode::lan;
    s_status.ap_active = false;
    s_status.ap_clients = 0;
    s_status.sta_connected = false;
    s_status.sta_has_ip = false;
    s_status.ip_address[0] = '\0';

    if (s_saved_ssid[0]) {
        wifi_config_t sta_cfg{};
        const size_t ssid_len = std::strlen(s_saved_ssid);
        std::memcpy(sta_cfg.sta.ssid, s_saved_ssid,
                    ssid_len < sizeof(sta_cfg.sta.ssid) ? ssid_len : sizeof(sta_cfg.sta.ssid));
        if (s_saved_pass[0]) {
            const size_t pass_len = std::strlen(s_saved_pass);
            std::memcpy(sta_cfg.sta.password, s_saved_pass,
                        pass_len < sizeof(sta_cfg.sta.password) ? pass_len : sizeof(sta_cfg.sta.password));
            sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        } else {
            sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
        }
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
        esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
        std::snprintf(s_status.sta_ssid, sizeof(s_status.sta_ssid), "%s", s_saved_ssid);
    }

    err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_started = true;
        if (s_saved_ssid[0]) {
            set_msg_locked("CONNECTING TO SAVED LAN WIFI...");
            esp_wifi_connect();
        } else {
            set_msg_locked("LAN MODE READY — TAP WIFI SCAN TO JOIN");
        }
    }
    return err;
}

}  // namespace

esp_err_t net_init(const char initial_passcode[9], NetMode initial_mode) {
    if (s_status.initialised) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;

    std::memset(&s_status, 0, sizeof(s_status));
    std::snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "%s", kHotspotSsid);
    std::snprintf(s_status.mdns_host, sizeof(s_status.mdns_host), "%s", kMdnsHost);
    if (initial_passcode && std::strlen(initial_passcode) == 8) {
        std::snprintf(s_passcode, sizeof(s_passcode), "%s", initial_passcode);
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_sta_netif || !s_ap_netif) return ESP_ERR_NO_MEM;

    esp_netif_set_hostname(s_sta_netif, "tab5");
    esp_netif_set_hostname(s_ap_netif, "tab5");

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, nullptr);
    if (err != ESP_OK) return err;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);

    load_console_nvs_wifi();

    lock_state();
    s_status.initialised = true;
    if (initial_mode == NetMode::hotspot || !s_saved_ssid[0]) {
        err = apply_hotspot_mode_locked();
    } else {
        err = apply_lan_mode_locked();
    }
    unlock_state();

    xTaskCreate(dns_mdns_worker, "tab5_mdns", 6144, nullptr, 4, nullptr);
    return err;
}

esp_err_t net_set_mode(NetMode mode, const char current_passcode[9]) {
    if (!s_status.initialised) return ESP_ERR_INVALID_STATE;
    lock_state();
    if (current_passcode && std::strlen(current_passcode) == 8) {
        std::snprintf(s_passcode, sizeof(s_passcode), "%s", current_passcode);
    }
    esp_err_t err = (mode == NetMode::hotspot) ? apply_hotspot_mode_locked() : apply_lan_mode_locked();
    unlock_state();
    return err;
}

esp_err_t net_update_passcode(const char new_passcode[9]) {
    if (!new_passcode || std::strlen(new_passcode) != 8) return ESP_ERR_INVALID_ARG;
    lock_state();
    std::snprintf(s_passcode, sizeof(s_passcode), "%s", new_passcode);
    esp_err_t err = ESP_OK;
    if (s_status.initialised && s_status.mode == NetMode::hotspot) {
        err = apply_hotspot_mode_locked();
        set_msg_locked("HOTSPOT PASSWORD UPDATED TO NEW 8-DIGIT CODE");
    } else {
        set_msg_locked("NEW 8-DIGIT PASSCODE GENERATED");
    }
    unlock_state();
    return err;
}

esp_err_t net_start_scan() {
    if (!s_status.initialised) return ESP_ERR_INVALID_STATE;
    lock_state();
    if (s_status.scanning) {
        unlock_state();
        return ESP_OK;
    }
    if (s_status.mode == NetMode::hotspot) {
        // Switch to LAN mode to scan 2.4 GHz networks
        apply_lan_mode_locked();
    }
    s_status.scanning = true;
    set_msg_locked("SCANNING 2.4 GHZ WIFI NETWORKS...");
    unlock_state();

    esp_wifi_disconnect();
    const esp_err_t res = esp_wifi_scan_start(nullptr, false);
    if (res != ESP_OK) {
        lock_state();
        s_status.scanning = false;
        set_msg_locked("COULD NOT START WIFI SCAN");
        unlock_state();
    }
    return res;
}

bool net_get_scanned_network(size_t index, ScannedNetwork* out) {
    if (!out) return false;
    lock_state();
    const bool ok = index < s_status.scan_count;
    if (ok) *out = s_networks[index];
    unlock_state();
    return ok;
}

esp_err_t net_connect_lan(const char* ssid, const char* password) {
    if (!s_status.initialised || !ssid || !ssid[0]) return ESP_ERR_INVALID_ARG;
    save_console_nvs_wifi(ssid, password ? password : "");
    lock_state();
    const esp_err_t err = apply_lan_mode_locked();
    unlock_state();
    return err;
}

esp_err_t net_reconnect_lan() {
    if (!s_status.initialised) return ESP_ERR_INVALID_STATE;
    lock_state();
    const esp_err_t err = apply_lan_mode_locked();
    unlock_state();
    return err;
}

void net_get_status(NetStatus* out) {
    if (!out) return;
    lock_state();
    *out = s_status;
    unlock_state();
}

}  // namespace tab5_fs
