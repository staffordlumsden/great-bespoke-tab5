#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

namespace tab5_fs {

constexpr size_t kMaxScannedNetworks = 16;
constexpr size_t kSsidMaxLen = 33;

enum class NetMode : uint8_t {
    lan = 0,
    hotspot = 1,
};

struct ScannedNetwork {
    char ssid[kSsidMaxLen];
    int8_t rssi;
    uint8_t authmode;
    bool secure;
};

struct NetStatus {
    uint32_t revision;
    bool initialised;
    NetMode mode;
    bool scanning;
    bool sta_connected;
    bool sta_has_ip;
    bool ap_active;
    int8_t sta_rssi;
    uint8_t ap_clients;
    size_t scan_count;
    char sta_ssid[kSsidMaxLen];
    char ap_ssid[kSsidMaxLen];
    char ip_address[16];
    char mdns_host[32];
    char status_msg[64];
};

// Initialize hosted C6 Wi-Fi netifs (both STA and AP) and start background mDNS/DNS responder.
// Must only be called AFTER tab5_radio_power_set(true) + settle delay.
esp_err_t net_init(const char initial_passcode[9], NetMode initial_mode);

// Switch between LAN Mode (Station) and Hotspot Mode (SoftAP).
esp_err_t net_set_mode(NetMode mode, const char current_passcode[9]);

// Update the 8-digit passcode (reconfigures SoftAP WPA2 password if in Hotspot mode).
esp_err_t net_update_passcode(const char new_passcode[9]);

// Trigger a 2.4 GHz Wi-Fi scan (switches to STA or APSTA if needed to scan).
esp_err_t net_start_scan();

// Retrieve a scanned network by index.
bool net_get_scanned_network(size_t index, ScannedNetwork* out);

// Connect to a LAN Wi-Fi network and persist credentials to shared NVS.
esp_err_t net_connect_lan(const char* ssid, const char* password);

// Reconnect to the saved LAN Wi-Fi network.
esp_err_t net_reconnect_lan();

// Copy current network status snapshot.
void net_get_status(NetStatus* out);

}  // namespace tab5_fs
