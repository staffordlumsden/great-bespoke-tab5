#pragma once

#include <cstdint>
#include <vector>
#include "arduino_compat.hpp"

enum class TerminalFontId : uint8_t {
    Medium = 0,
    Large = 1,
    Small = 2,
    ExtraLarge = 3,
    WideLarge = 4,
    AsciiLarge = 5,
    AsciiHuge = 6,
};

struct WifiProfile {
    String name;
    String ssid;
    String password;
};

struct SshProfile {
    String name;
    String host;
    uint16_t port{22};
    String user;
    String password;
    String keyPath;        // Optional /private/... path on microSD
    String keyPassphrase;  // Optional passphrase for keyPath
    String terminal{"xterm-256color"};
};

struct KeyboardConfig {
    String layout{"us"};
    bool usbKeyboardEnabled{true};
    bool ctrlCapsSwap{false};
    bool repeatEnabled{true};
    bool autoConnectWifi{false};
    bool autoConnectSsh{false};
    uint8_t activeWifi{0};
    uint8_t activeSsh{0};
    uint8_t terminalFont{static_cast<uint8_t>(TerminalFontId::Medium)};
    uint8_t terminalLineStep{0};
    uint8_t displayRotation{0};  // 0..3 matching tab5_display_rotation_t
    bool darkTheme{true};        // Console Dark theme by default
    uint32_t sdSyncRevision{0};
};

struct SystemConfig {
    String deviceName{"tab5"};
    String region{"UTC+0"};
    String ntpServer{"pool.ntp.org"};
    int32_t utcOffsetMinutes{0};
};

struct AppConfig {
    std::vector<WifiProfile> wifi;
    std::vector<SshProfile> ssh;
    KeyboardConfig keyboard;
    SystemConfig system;
};
