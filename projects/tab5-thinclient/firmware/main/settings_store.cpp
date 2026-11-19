#include "settings_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#ifdef ESP_PLATFORM
#include "nvs.h"
#include "nvs_flash.h"
#endif

namespace settings_store {
namespace {

#ifdef ESP_PLATFORM
constexpr const char* kNvsNamespace = "tab5_tc";
constexpr const char* kNvsConfigKey = "cfg_json";
#endif

String trimCopy(const String& s) {
    String out = s;
    out.trim();
    return out;
}

void appendJsonEscaped(String& out, const String& s) {
    out += '"';
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

// Lightweight self-contained JSON parser so config serialization/parsing
// runs identically in both ESP-IDF firmware and host C++ unit tests.
struct JsonVal {
    enum Type { Null, Bool, Number, StringType, Array, Object };
    Type type{Null};
    bool boolVal{false};
    long numVal{0};
    String strVal;
    std::vector<JsonVal> arrayVal;
    std::map<std::string, JsonVal> objVal;

    const JsonVal* get(const char* key) const {
        if (type != Object) return nullptr;
        auto it = objVal.find(key);
        return it == objVal.end() ? nullptr : &it->second;
    }

    String asString(const char* fallback = "") const {
        if (type == StringType) return strVal;
        if (type == Number) return String(numVal);
        if (type == Bool) return boolVal ? "true" : "false";
        return String(fallback);
    }

    long asInt(long fallback = 0) const {
        if (type == Number) return numVal;
        if (type == Bool) return boolVal ? 1 : 0;
        if (type == StringType) return strVal.toInt();
        return fallback;
    }

    bool asBool(bool fallback = false) const {
        if (type == Bool) return boolVal;
        if (type == Number) return numVal != 0;
        if (type == StringType) {
            String low = strVal;
            low.toLowerCase();
            return low == "true" || low == "1" || low == "yes" || low == "on";
        }
        return fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const char* s, size_t n) : s_(s), n_(n) {}

    bool parse(JsonVal& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ == n_;
    }

private:
    const char* s_;
    size_t n_;
    size_t pos_{0};

    void skipWs() {
        while (pos_ < n_ && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool match(char ch) {
        skipWs();
        if (pos_ < n_ && s_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseString(String& out) {
        skipWs();
        if (pos_ >= n_ || s_[pos_] != '"') return false;
        ++pos_;
        std::string buf;
        while (pos_ < n_) {
            char c = s_[pos_++];
            if (c == '"') {
                out = String(std::move(buf));
                return true;
            }
            if (c == '\\') {
                if (pos_ >= n_) return false;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"': buf.push_back('"'); break;
                    case '\\': buf.push_back('\\'); break;
                    case '/': buf.push_back('/'); break;
                    case 'b': buf.push_back('\b'); break;
                    case 'f': buf.push_back('\f'); break;
                    case 'n': buf.push_back('\n'); break;
                    case 'r': buf.push_back('\r'); break;
                    case 't': buf.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > n_) return false;
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else return false;
                        }
                        if (cp < 0x80) {
                            buf.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            buf.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            buf.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            buf.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: buf.push_back(esc); break;
                }
            } else {
                buf.push_back(c);
            }
        }
        return false;
    }

    bool parseValue(JsonVal& out) {
        skipWs();
        if (pos_ >= n_) return false;
        char c = s_[pos_];
        if (c == '"') {
            out = JsonVal{};
            out.type = JsonVal::StringType;
            return parseString(out.strVal);
        }
        if (c == '{') {
            ++pos_;
            out = JsonVal{};
            out.type = JsonVal::Object;
            skipWs();
            if (pos_ < n_ && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            for (;;) {
                String k;
                if (!parseString(k)) return false;
                if (!match(':')) return false;
                JsonVal v;
                if (!parseValue(v)) return false;
                out.objVal[k.str()] = std::move(v);
                skipWs();
                if (pos_ < n_ && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < n_ && s_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            out = JsonVal{};
            out.type = JsonVal::Array;
            skipWs();
            if (pos_ < n_ && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            for (;;) {
                JsonVal item;
                if (!parseValue(item)) return false;
                out.arrayVal.push_back(std::move(item));
                skipWs();
                if (pos_ < n_ && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < n_ && s_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (pos_ + 4 <= n_ && strncmp(s_ + pos_, "true", 4) == 0) {
            pos_ += 4;
            out = JsonVal{};
            out.type = JsonVal::Bool;
            out.boolVal = true;
            return true;
        }
        if (pos_ + 5 <= n_ && strncmp(s_ + pos_, "false", 5) == 0) {
            pos_ += 5;
            out = JsonVal{};
            out.type = JsonVal::Bool;
            out.boolVal = false;
            return true;
        }
        if (pos_ + 4 <= n_ && strncmp(s_ + pos_, "null", 4) == 0) {
            pos_ += 4;
            out = JsonVal{};
            out.type = JsonVal::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = pos_;
            if (s_[pos_] == '-') ++pos_;
            while (pos_ < n_ && (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.' ||
                                 s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) {
                ++pos_;
            }
            std::string numStr(s_ + start, pos_ - start);
            out = JsonVal{};
            out.type = JsonVal::Number;
            out.numVal = strtol(numStr.c_str(), nullptr, 10);
            return true;
        }
        return false;
    }
};

int upsertProfileInternal(AppConfig& config, const SshProfile& candidate, bool matchByName) {
    if (!candidate.host.length() || !candidate.user.length()) {
        return 0;
    }
    for (auto& existing : config.ssh) {
        bool sameEndpoint = (existing.host == candidate.host &&
                             existing.port == candidate.port &&
                             existing.user == candidate.user);
        bool sameName = matchByName && candidate.name.length() &&
                        existing.name.equalsIgnoreCase(candidate.name);
        if (sameEndpoint || sameName) {
            existing.host = candidate.host;
            existing.port = candidate.port;
            existing.user = candidate.user;
            if (candidate.name.length()) existing.name = candidate.name;
            if (candidate.keyPath.length()) existing.keyPath = candidate.keyPath;
            if (candidate.keyPassphrase.length()) existing.keyPassphrase = candidate.keyPassphrase;
            if (candidate.password.length()) existing.password = candidate.password;
            if (candidate.terminal.length()) existing.terminal = candidate.terminal;
            return 0;
        }
    }
    config.ssh.push_back(candidate);
    return 1;
}

std::vector<String> splitTokens(const String& line) {
    std::vector<String> tokens;
    size_t i = 0;
    const size_t n = line.length();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n) break;
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i++];
            size_t start = i;
            while (i < n && line[i] != quote) ++i;
            tokens.push_back(line.substring(start, i));
            if (i < n && line[i] == quote) ++i;
        } else {
            size_t start = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            tokens.push_back(line.substring(start, i));
        }
    }
    return tokens;
}

#ifdef ESP_PLATFORM
bool readTextFile(const char* path, String& out) {
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;
    std::string buf;
    char chunk[512];
    size_t n = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        buf.append(chunk, n);
        if (buf.size() > 65536) break;
    }
    fclose(fp);
    out = String(std::move(buf));
    return true;
}
#endif

}  // namespace

String resolveSdKeyPath(const String& rawPath) {
    String p = trimCopy(rawPath);
    if (!p.length()) return "";
    if (p.startsWith("sd:")) p = p.substring(3);
    if (p.startsWith("/sdcard/")) return p;
    if (p.startsWith("/private/")) return String("/sdcard") + p;
    if (p[0] == '/') return String("/sdcard") + p;
    return String("/sdcard/private/") + p;
}

String serializeConfigJson(const AppConfig& config) {
    String out;
    out.reserve(1024);
    out += "{\"wifi\":[";
    for (size_t i = 0; i < config.wifi.size(); ++i) {
        if (i) out += ',';
        out += "{\"name\":";
        appendJsonEscaped(out, config.wifi[i].name);
        out += ",\"ssid\":";
        appendJsonEscaped(out, config.wifi[i].ssid);
        out += ",\"password\":";
        appendJsonEscaped(out, config.wifi[i].password);
        out += '}';
    }
    out += "],\"ssh\":[";
    for (size_t i = 0; i < config.ssh.size(); ++i) {
        if (i) out += ',';
        out += "{\"name\":";
        appendJsonEscaped(out, config.ssh[i].name);
        out += ",\"host\":";
        appendJsonEscaped(out, config.ssh[i].host);
        out += ",\"port\":" + String(static_cast<unsigned int>(config.ssh[i].port));
        out += ",\"user\":";
        appendJsonEscaped(out, config.ssh[i].user);
        out += ",\"password\":";
        appendJsonEscaped(out, config.ssh[i].password);
        out += ",\"key_path\":";
        appendJsonEscaped(out, config.ssh[i].keyPath);
        out += ",\"key_passphrase\":";
        appendJsonEscaped(out, config.ssh[i].keyPassphrase);
        out += ",\"terminal\":";
        appendJsonEscaped(out, config.ssh[i].terminal);
        out += '}';
    }
    out += "],\"keyboard\":{";
    out += "\"layout\":";
    appendJsonEscaped(out, config.keyboard.layout);
    out += ",\"usb_keyboard\":" + String(config.keyboard.usbKeyboardEnabled ? "true" : "false");
    out += ",\"ctrl_caps_swap\":" + String(config.keyboard.ctrlCapsSwap ? "true" : "false");
    out += ",\"repeat\":" + String(config.keyboard.repeatEnabled ? "true" : "false");
    out += ",\"auto_connect_wifi\":" + String(config.keyboard.autoConnectWifi ? "true" : "false");
    out += ",\"auto_connect_ssh\":" + String(config.keyboard.autoConnectSsh ? "true" : "false");
    out += ",\"active_wifi\":" + String(static_cast<unsigned int>(config.keyboard.activeWifi));
    out += ",\"active_ssh\":" + String(static_cast<unsigned int>(config.keyboard.activeSsh));
    out += ",\"terminal_font\":" + String(static_cast<unsigned int>(config.keyboard.terminalFont));
    out += ",\"terminal_line_step\":" + String(static_cast<unsigned int>(config.keyboard.terminalLineStep));
    out += ",\"display_rotation\":" + String(static_cast<unsigned int>(config.keyboard.displayRotation));
    out += ",\"dark_theme\":" + String(config.keyboard.darkTheme ? "true" : "false");
    out += ",\"sd_sync_revision\":" + String(static_cast<unsigned long>(config.keyboard.sdSyncRevision));
    out += "},\"system\":{";
    out += "\"device_name\":";
    appendJsonEscaped(out, config.system.deviceName);
    out += ",\"region\":";
    appendJsonEscaped(out, config.system.region);
    out += ",\"ntp_server\":";
    appendJsonEscaped(out, config.system.ntpServer);
    out += ",\"utc_offset_minutes\":" + String(config.system.utcOffsetMinutes);
    out += "}}";
    return out;
}

bool deserializeConfigJson(AppConfig& config, const String& jsonText) {
    JsonVal root;
    JsonParser parser(jsonText.c_str(), jsonText.length());
    if (!parser.parse(root) || root.type != JsonVal::Object) {
        return false;
    }

    if (const auto* wifi = root.get("wifi"); wifi && wifi->type == JsonVal::Array) {
        config.wifi.clear();
        for (const auto& item : wifi->arrayVal) {
            if (item.type != JsonVal::Object) continue;
            WifiProfile p;
            if (const auto* v = item.get("name")) p.name = v->asString();
            if (const auto* v = item.get("ssid")) p.ssid = v->asString();
            if (const auto* v = item.get("password")) p.password = v->asString();
            if (p.ssid.length()) {
                if (!p.name.length()) p.name = p.ssid;
                config.wifi.push_back(std::move(p));
            }
        }
    }

    if (const auto* ssh = root.get("ssh"); ssh && ssh->type == JsonVal::Array) {
        config.ssh.clear();
        for (const auto& item : ssh->arrayVal) {
            if (item.type != JsonVal::Object) continue;
            SshProfile p;
            if (const auto* v = item.get("name")) p.name = v->asString();
            if (const auto* v = item.get("host")) p.host = v->asString();
            if (const auto* v = item.get("port")) p.port = static_cast<uint16_t>(std::max(1L, std::min(65535L, v->asInt(22))));
            if (const auto* v = item.get("user")) p.user = v->asString();
            if (const auto* v = item.get("password")) p.password = v->asString();
            if (const auto* v = item.get("key_path")) p.keyPath = resolveSdKeyPath(v->asString());
            if (const auto* v = item.get("key_passphrase")) p.keyPassphrase = v->asString();
            if (const auto* v = item.get("terminal")) p.terminal = v->asString("xterm-256color");
            if (!p.terminal.length()) p.terminal = "xterm-256color";
            if (p.host.length()) {
                if (!p.name.length()) p.name = p.host;
                config.ssh.push_back(std::move(p));
            }
        }
    }

    if (const auto* kb = root.get("keyboard"); kb && kb->type == JsonVal::Object) {
        if (const auto* v = kb->get("layout")) config.keyboard.layout = (v->asString() == "jp") ? "jp" : "us";
        if (const auto* v = kb->get("usb_keyboard")) config.keyboard.usbKeyboardEnabled = v->asBool(true);
        if (const auto* v = kb->get("ctrl_caps_swap")) config.keyboard.ctrlCapsSwap = v->asBool(false);
        if (const auto* v = kb->get("repeat")) config.keyboard.repeatEnabled = v->asBool(true);
        if (const auto* v = kb->get("auto_connect_wifi")) config.keyboard.autoConnectWifi = v->asBool(false);
        if (const auto* v = kb->get("auto_connect_ssh")) config.keyboard.autoConnectSsh = v->asBool(false);
        if (const auto* v = kb->get("active_wifi")) config.keyboard.activeWifi = static_cast<uint8_t>(v->asInt(0));
        if (const auto* v = kb->get("active_ssh")) config.keyboard.activeSsh = static_cast<uint8_t>(v->asInt(0));
        if (const auto* v = kb->get("terminal_font")) config.keyboard.terminalFont = static_cast<uint8_t>(v->asInt(0));
        if (const auto* v = kb->get("terminal_line_step")) config.keyboard.terminalLineStep = static_cast<uint8_t>(v->asInt(0));
        if (const auto* v = kb->get("display_rotation")) config.keyboard.displayRotation = static_cast<uint8_t>(v->asInt(0) & 3);
        if (const auto* v = kb->get("dark_theme")) config.keyboard.darkTheme = v->asBool(true);
        if (const auto* v = kb->get("sd_sync_revision")) {
            long rev = v->asInt(0);
            config.keyboard.sdSyncRevision = rev > 0 ? static_cast<uint32_t>(rev) : 0u;
        }
    }

    if (const auto* sys = root.get("system"); sys && sys->type == JsonVal::Object) {
        if (const auto* v = sys->get("device_name")) config.system.deviceName = v->asString("tab5");
        if (const auto* v = sys->get("region")) config.system.region = v->asString("UTC+0");
        if (const auto* v = sys->get("ntp_server")) config.system.ntpServer = v->asString("pool.ntp.org");
        if (const auto* v = sys->get("utc_offset_minutes")) {
            config.system.utcOffsetMinutes = static_cast<int32_t>(std::max(-720L, std::min(840L, v->asInt(0))));
        }
    }

    clampSelection(config);
    return true;
}

int importTerminalJsonObject(AppConfig& config, const JsonVal& obj, bool matchByName) {
    if (obj.type != JsonVal::Object) return 0;
    SshProfile p;
    if (const auto* v = obj.get("name")) p.name = trimCopy(v->asString());
    if (const auto* v = obj.get("host")) p.host = trimCopy(v->asString());
    if (const auto* v = obj.get("username")) p.user = trimCopy(v->asString());
    if (!p.user.length()) {
        if (const auto* v = obj.get("user")) p.user = trimCopy(v->asString());
    }
    if (const auto* v = obj.get("port")) {
        p.port = static_cast<uint16_t>(std::max(1L, std::min(65535L, v->asInt(22))));
    }
    if (const auto* v = obj.get("password")) p.password = v->asString();
    if (const auto* v = obj.get("private_key_file")) p.keyPath = resolveSdKeyPath(v->asString());
    if (!p.keyPath.length()) {
        if (const auto* v = obj.get("key_path")) p.keyPath = resolveSdKeyPath(v->asString());
        else if (const auto* v2 = obj.get("keyPath")) p.keyPath = resolveSdKeyPath(v2->asString());
    }
    if (const auto* v = obj.get("private_key_passphrase")) p.keyPassphrase = v->asString();
    if (!p.host.length() || !p.user.length()) {
        return 0;
    }
    bool explicitName = p.name.length() > 0;
    if (!explicitName) {
        p.name = String("SD: ") + p.user + "@" + p.host;
    }
    return upsertProfileInternal(config, p, matchByName && explicitName);
}

int importTerminalJsonText(AppConfig& config, const String& jsonText) {
    JsonVal root;
    JsonParser parser(jsonText.c_str(), jsonText.length());
    if (!parser.parse(root) || root.type != JsonVal::Object) {
        return 0;
    }
    if (const auto* revVal = root.get("revision")) {
        long revLong = revVal->asInt(0);
        if (revLong > 0 && static_cast<uint32_t>(revLong) > config.keyboard.sdSyncRevision) {
            clearSshProfiles(config);
            config.keyboard.sdSyncRevision = static_cast<uint32_t>(revLong);
        }
    }
    int added = 0;
    if (const auto* arr = root.get("profiles"); arr && arr->type == JsonVal::Array) {
        for (const auto& item : arr->arrayVal) {
            added += importTerminalJsonObject(config, item, true);
        }
    } else {
        added += importTerminalJsonObject(config, root, true);
    }
    clampSelection(config);
    return added;
}

int importShellProfilesText(AppConfig& config, const String& text) {
    int added = 0;
    size_t start = 0;
    const size_t n = text.length();
    while (start < n) {
        int nl = text.indexOf('\n', start);
        size_t end = nl < 0 ? n : static_cast<size_t>(nl);
        String line = trimCopy(text.substring(start, end));
        start = end + 1;
        if (!line.length() || line[0] == '#') continue;

        auto tokens = splitTokens(line);
        if (tokens.size() < 2) continue;

        SshProfile p;
        p.name = tokens[0];
        const String& target = tokens[1];
        int at = target.indexOf('@');
        if (at <= 0 || at + 1 >= static_cast<int>(target.length())) continue;
        p.user = target.substring(0, static_cast<size_t>(at));
        p.host = target.substring(static_cast<size_t>(at + 1));

        for (size_t i = 2; i + 1 < tokens.size(); ++i) {
            if (tokens[i] == "-p") {
                long port = tokens[i + 1].toInt();
                if (port >= 1 && port <= 65535) p.port = static_cast<uint16_t>(port);
                ++i;
            } else if (tokens[i] == "-i") {
                p.keyPath = resolveSdKeyPath(tokens[i + 1]);
                ++i;
            }
        }
        added += upsertProfileInternal(config, p, true);
    }
    clampSelection(config);
    return added;
}

void clampSelection(AppConfig& config) {
    if (config.wifi.empty()) config.keyboard.activeWifi = 0;
    else if (config.keyboard.activeWifi >= config.wifi.size()) {
        config.keyboard.activeWifi = static_cast<uint8_t>(config.wifi.size() - 1);
    }
    if (config.ssh.empty()) config.keyboard.activeSsh = 0;
    else if (config.keyboard.activeSsh >= config.ssh.size()) {
        config.keyboard.activeSsh = static_cast<uint8_t>(config.ssh.size() - 1);
    }
    config.keyboard.displayRotation &= 3u;
}

int upsertSshProfile(AppConfig& config, const SshProfile& profile, bool selectAsActive) {
    if (!profile.host.length() || !profile.user.length()) return -1;
    upsertProfileInternal(config, profile, profile.name.length() > 0);
    int matchedIdx = -1;
    for (size_t i = 0; i < config.ssh.size(); ++i) {
        const auto& e = config.ssh[i];
        if ((e.host == profile.host && e.port == profile.port && e.user == profile.user) ||
            (profile.name.length() && e.name.equalsIgnoreCase(profile.name))) {
            matchedIdx = static_cast<int>(i);
            break;
        }
    }
    if (matchedIdx >= 0 && selectAsActive) {
        config.keyboard.activeSsh = static_cast<uint8_t>(matchedIdx);
    }
    clampSelection(config);
    return matchedIdx;
}

bool removeSshProfile(AppConfig& config, const String& selector) {
    String sel = trimCopy(selector);
    if (!sel.length() || config.ssh.empty()) return false;
    bool allDigits = true;
    for (size_t i = 0; i < sel.length(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(sel[i]))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits) {
        size_t idx = static_cast<size_t>(sel.toInt());
        if (idx < config.ssh.size()) {
            config.ssh.erase(config.ssh.begin() + static_cast<long>(idx));
            clampSelection(config);
            return true;
        }
        return false;
    }
    for (size_t i = 0; i < config.ssh.size(); ++i) {
        const auto& p = config.ssh[i];
        String userHost = p.user + "@" + p.host;
        if (p.name.equalsIgnoreCase(sel) || p.host.equalsIgnoreCase(sel) || userHost.equalsIgnoreCase(sel)) {
            config.ssh.erase(config.ssh.begin() + static_cast<long>(i));
            clampSelection(config);
            return true;
        }
    }
    return false;
}

void clearSshProfiles(AppConfig& config) {
    config.ssh.clear();
    clampSelection(config);
}

#ifdef ESP_PLATFORM
bool loadConfig(AppConfig& config, String& sourceNote) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(handle, kNvsConfigKey, nullptr, &len) == ESP_OK && len > 1) {
            std::string raw(len, '\0');
            if (nvs_get_str(handle, kNvsConfigKey, raw.data(), &len) == ESP_OK) {
                raw.resize(len > 0 ? len - 1 : 0);
                if (deserializeConfigJson(config, String(raw))) {
                    nvs_close(handle);
                    sourceNote = "NVS";
                    return true;
                }
            }
        }
        nvs_close(handle);
    }

    String sdJson;
    if (readTextFile("/sdcard/private/tab5-thinclient.json", sdJson) &&
        deserializeConfigJson(config, sdJson)) {
        sourceNote = "/sdcard/private/tab5-thinclient.json";
        return true;
    }

    sourceNote = "defaults";
    return false;
}

bool saveConfig(const AppConfig& config, String& error) {
    String json = serializeConfigJson(config);
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        error = String("NVS open failed: ") + static_cast<int>(err);
        return false;
    }
    err = nvs_set_str(handle, kNvsConfigKey, json.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        error = String("NVS save failed: ") + static_cast<int>(err);
        return false;
    }
    return true;
}

int syncSdSshProfiles(AppConfig& config, bool replaceExisting) {
    if (replaceExisting) {
        clearSshProfiles(config);
    }
    int total = 0;
    String termJson;
    if (readTextFile("/sdcard/private/tab5-terminal.json", termJson)) {
        total += importTerminalJsonText(config, termJson);
    }
    String shellProfiles;
    if (readTextFile("/sdcard/private/tab5-shell-profiles.txt", shellProfiles)) {
        total += importShellProfilesText(config, shellProfiles);
    }
    return total;
}
#endif

}  // namespace settings_store
