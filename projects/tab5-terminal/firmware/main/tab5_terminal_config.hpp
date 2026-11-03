#pragma once

#include <cstddef>
#include <cstdint>

namespace tab5_terminal {

inline constexpr const char* kConfigPath =
    "/sdcard/private/tab5-terminal.json";

struct Config {
    char host[128]{};
    char username[64]{};
    char password[256]{};
    char private_key_file[160]{};
    char private_key_passphrase[128]{};
    uint16_t port{22};
    bool ready{false};
};

// Loads the private SD profile without logging any field values.  `message`
// receives a short, display-safe status and is always NUL terminated.
bool load_config(Config& out, char* message, size_t message_capacity);

}  // namespace tab5_terminal
