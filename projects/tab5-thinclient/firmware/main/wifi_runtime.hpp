#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "arduino_compat.hpp"

struct WifiScanEntry {
    String ssid;
    int rssi{0};
    bool secure{false};
};

class WifiRuntime {
public:
    bool init();
    bool start();
    bool connect(const String& ssid, const String& password);
    bool disconnect();
    bool retry();
    bool scan();

    void poll();

    bool isConnected() const { return connected_; }
    bool hasIp() const { return hasIp_; }
    bool isScanning() const { return scanning_; }
    bool hasSavedNetwork() const { return hasSaved_; }
    const String& ssid() const { return ssid_; }
    const String& ipAddress() const { return ip_; }
    const String& statusMessage() const { return message_; }
    int rssi() const { return rssi_; }
    const std::vector<WifiScanEntry>& scannedNetworks() const { return scanResults_; }

private:
    bool initialized_{false};
    bool started_{false};
    bool connected_{false};
    bool hasIp_{false};
    bool scanning_{false};
    bool hasSaved_{false};
    int rssi_{0};
    uint32_t lastRevision_{0};
    String ssid_;
    String ip_;
    String message_{"Wi-Fi idle"};
    std::vector<WifiScanEntry> scanResults_;
};
