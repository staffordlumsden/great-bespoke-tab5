#include "tab5_terminal_config.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

#include "cJSON.h"

namespace tab5_terminal {
namespace {

constexpr size_t kMaximumConfigBytes = 8192;

void set_message(char* out, size_t capacity, const char* value) {
    if (!out || capacity == 0) return;
    std::snprintf(out, capacity, "%s", value ? value : "Configuration error");
}

bool printable_string(const cJSON* item, char* out, size_t capacity,
                      bool allow_empty) {
    if (!cJSON_IsString(item) || !item->valuestring || capacity == 0) return false;
    const size_t length = std::strlen(item->valuestring);
    if ((!allow_empty && length == 0) || length >= capacity) return false;
    for (size_t i = 0; i < length; ++i) {
        const unsigned char value = static_cast<unsigned char>(item->valuestring[i]);
        if (value < 32 || value == 127) return false;
    }
    std::memcpy(out, item->valuestring, length + 1);
    return true;
}

bool private_key_path(const char* path) {
    if (!path || std::strncmp(path, "/private/", 9) != 0) return false;
    if (std::strstr(path, "..") || std::strchr(path, '\\')) return false;
    return std::strlen(path) > 9;
}

}  // namespace

bool load_config(Config& out, char* message, size_t message_capacity) {
    out = {};
    set_message(message, message_capacity,
                "PROFILE REQUIRED: /private/tab5-terminal.json");

    std::unique_ptr<FILE, decltype(&std::fclose)> file(
        std::fopen(kConfigPath, "rb"), &std::fclose);
    if (!file) return false;
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        set_message(message, message_capacity, "Cannot read terminal profile");
        return false;
    }
    const long size = std::ftell(file.get());
    if (size <= 0 || static_cast<size_t>(size) > kMaximumConfigBytes ||
        std::fseek(file.get(), 0, SEEK_SET) != 0) {
        set_message(message, message_capacity, "Terminal profile size invalid");
        return false;
    }

    std::unique_ptr<char[]> bytes(new (std::nothrow) char[static_cast<size_t>(size) + 1]);
    if (!bytes) {
        set_message(message, message_capacity, "Not enough memory for profile");
        return false;
    }
    if (std::fread(bytes.get(), 1, static_cast<size_t>(size), file.get()) !=
        static_cast<size_t>(size)) {
        set_message(message, message_capacity, "Cannot read terminal profile");
        return false;
    }
    bytes[size] = '\0';

    cJSON* raw = cJSON_ParseWithLengthOpts(bytes.get(), static_cast<size_t>(size) + 1,
                                           nullptr, true);
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(raw, &cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get())) {
        set_message(message, message_capacity, "Terminal profile JSON invalid");
        return false;
    }

    if (!printable_string(cJSON_GetObjectItemCaseSensitive(root.get(), "host"),
                          out.host, sizeof(out.host), false) ||
        !printable_string(cJSON_GetObjectItemCaseSensitive(root.get(), "username"),
                          out.username, sizeof(out.username), false)) {
        set_message(message, message_capacity, "Profile needs host and username");
        return false;
    }

    if (const cJSON* port = cJSON_GetObjectItemCaseSensitive(root.get(), "port")) {
        if (!cJSON_IsNumber(port) || port->valuedouble < 1 || port->valuedouble > 65535 ||
            port->valuedouble != static_cast<double>(port->valueint)) {
            set_message(message, message_capacity, "Terminal profile port invalid");
            return false;
        }
        out.port = static_cast<uint16_t>(port->valueint);
    }

    const cJSON* password = cJSON_GetObjectItemCaseSensitive(root.get(), "password");
    if (password && !printable_string(password, out.password, sizeof(out.password), true)) {
        set_message(message, message_capacity, "Terminal password field invalid");
        return false;
    }

    const cJSON* key = cJSON_GetObjectItemCaseSensitive(root.get(), "private_key_file");
    if (key && !printable_string(key, out.private_key_file,
                                 sizeof(out.private_key_file), false)) {
        set_message(message, message_capacity, "Private key path invalid");
        return false;
    }
    if (out.private_key_file[0] && !private_key_path(out.private_key_file)) {
        set_message(message, message_capacity, "Private key must be under /private");
        return false;
    }

    const cJSON* passphrase =
        cJSON_GetObjectItemCaseSensitive(root.get(), "private_key_passphrase");
    if (passphrase && !printable_string(passphrase, out.private_key_passphrase,
                                        sizeof(out.private_key_passphrase), true)) {
        set_message(message, message_capacity, "Key passphrase field invalid");
        return false;
    }

    if (!out.password[0] && !out.private_key_file[0]) {
        set_message(message, message_capacity, "Profile needs password or private key");
        return false;
    }

    out.ready = true;
    set_message(message, message_capacity,
                out.private_key_file[0] ? "Private-key SSH profile ready"
                                        : "Password SSH profile ready");
    return true;
}

}  // namespace tab5_terminal
