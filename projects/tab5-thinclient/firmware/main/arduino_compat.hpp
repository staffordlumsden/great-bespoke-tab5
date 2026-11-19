#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

// Minimal Arduino/WString-compatible shim so upstream Tab5_thinclient modules
// (TerminalEmulator, TerminalBuffer, KeyboardMapper, AppConfig) compile cleanly
// under both ESP-IDF and host C++17 unit tests without pulling Arduino core.
class String {
public:
    String() = default;
    String(const char* s) : data_(s ? s : "") {}
    String(const std::string& s) : data_(s) {}
    String(std::string&& s) : data_(std::move(s)) {}
    explicit String(char c) : data_(1, c) {}
    explicit String(int v) : data_(std::to_string(v)) {}
    explicit String(unsigned int v) : data_(std::to_string(v)) {}
    explicit String(long v) : data_(std::to_string(v)) {}
    explicit String(unsigned long v) : data_(std::to_string(v)) {}
    explicit String(unsigned long long v) : data_(std::to_string(v)) {}

    size_t length() const { return data_.size(); }
    bool isEmpty() const { return data_.empty(); }
    const char* c_str() const { return data_.c_str(); }
    const std::string& str() const { return data_; }

    char operator[](size_t i) const { return data_[i]; }
    char& operator[](size_t i) { return data_[i]; }

    void reserve(size_t n) { data_.reserve(n); }
    void clear() { data_.clear(); }

    String& operator=(char c) {
        data_.assign(1, c);
        return *this;
    }
    String& operator=(const char* s) {
        data_.assign(s ? s : "");
        return *this;
    }
    String& operator=(const std::string& s) {
        data_ = s;
        return *this;
    }

    String& operator+=(const String& rhs) {
        data_ += rhs.data_;
        return *this;
    }
    String& operator+=(const char* rhs) {
        if (rhs) data_ += rhs;
        return *this;
    }
    String& operator+=(char c) {
        data_.push_back(c);
        return *this;
    }
    String& operator+=(int v) {
        data_ += std::to_string(v);
        return *this;
    }
    String& operator+=(unsigned int v) {
        data_ += std::to_string(v);
        return *this;
    }
    String& operator+=(long v) {
        data_ += std::to_string(v);
        return *this;
    }
    String& operator+=(unsigned long v) {
        data_ += std::to_string(v);
        return *this;
    }
    String& operator+=(unsigned long long v) {
        data_ += std::to_string(v);
        return *this;
    }

    String operator+(const String& rhs) const { return String(data_ + rhs.data_); }
    String operator+(const char* rhs) const { return String(data_ + (rhs ? rhs : "")); }
    String operator+(char c) const {
        std::string s = data_;
        s.push_back(c);
        return String(std::move(s));
    }
    String operator+(int v) const { return String(data_ + std::to_string(v)); }
    String operator+(unsigned int v) const { return String(data_ + std::to_string(v)); }
    String operator+(long v) const { return String(data_ + std::to_string(v)); }
    String operator+(unsigned long v) const { return String(data_ + std::to_string(v)); }
    String operator+(unsigned long long v) const { return String(data_ + std::to_string(v)); }

    bool operator==(const String& rhs) const { return data_ == rhs.data_; }
    bool operator!=(const String& rhs) const { return data_ != rhs.data_; }
    bool operator==(const char* rhs) const { return data_ == (rhs ? rhs : ""); }
    bool operator!=(const char* rhs) const { return data_ != (rhs ? rhs : ""); }

    int indexOf(char c, size_t from = 0) const {
        if (from >= data_.size()) return -1;
        size_t pos = data_.find(c, from);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int indexOf(const char* s, size_t from = 0) const {
        if (!s || from > data_.size()) return -1;
        size_t pos = data_.find(s, from);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        size_t pos = data_.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(const char* s) const {
        if (!s) return -1;
        size_t pos = data_.rfind(s);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }

    bool startsWith(const char* prefix) const {
        if (!prefix) return false;
        size_t n = std::strlen(prefix);
        return data_.size() >= n && data_.compare(0, n, prefix) == 0;
    }
    bool startsWith(const String& prefix) const {
        return startsWith(prefix.c_str());
    }
    bool endsWith(const char* suffix) const {
        if (!suffix) return false;
        size_t n = std::strlen(suffix);
        return data_.size() >= n && data_.compare(data_.size() - n, n, suffix) == 0;
    }
    bool endsWith(const String& suffix) const {
        return endsWith(suffix.c_str());
    }
    bool equalsIgnoreCase(const char* other) const {
        if (!other) return data_.empty();
        size_t n = std::strlen(other);
        if (n != data_.size()) return false;
        for (size_t i = 0; i < n; ++i) {
            if (std::tolower(static_cast<unsigned char>(data_[i])) !=
                std::tolower(static_cast<unsigned char>(other[i]))) {
                return false;
            }
        }
        return true;
    }
    bool equalsIgnoreCase(const String& other) const {
        return equalsIgnoreCase(other.c_str());
    }

    String substring(size_t begin) const {
        if (begin >= data_.size()) return String();
        return String(data_.substr(begin));
    }
    String substring(size_t begin, size_t end) const {
        if (begin >= data_.size() || end <= begin) return String();
        return String(data_.substr(begin, end - begin));
    }

    void remove(size_t index) {
        if (index < data_.size()) data_.erase(index);
    }
    void remove(size_t index, size_t count) {
        if (index < data_.size()) data_.erase(index, count);
    }

    void trim() {
        size_t first = 0;
        while (first < data_.size() && std::isspace(static_cast<unsigned char>(data_[first]))) {
            ++first;
        }
        size_t last = data_.size();
        while (last > first && std::isspace(static_cast<unsigned char>(data_[last - 1]))) {
            --last;
        }
        if (first > 0 || last < data_.size()) {
            data_ = data_.substr(first, last - first);
        }
    }

    void toLowerCase() {
        for (char& c : data_) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    void replace(const char* find, const char* with) {
        if (!find || !*find) return;
        std::string f(find);
        std::string w(with ? with : "");
        size_t pos = 0;
        while ((pos = data_.find(f, pos)) != std::string::npos) {
            data_.replace(pos, f.size(), w);
            pos += w.size();
        }
    }

    long toInt() const { return std::strtol(data_.c_str(), nullptr, 10); }

private:
    std::string data_;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs ? lhs : "") + rhs.str());
}

using std::max;
using std::min;

template <typename T>
inline T constrain(T val, T lo, T hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

inline uint32_t millis() {
#ifdef ESP_PLATFORM
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

inline uint32_t micros() {
#ifdef ESP_PLATFORM
    return static_cast<uint32_t>(esp_timer_get_time());
#else
    return 0;
#endif
}

inline void delay(uint32_t ms) {
#ifdef ESP_PLATFORM
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
#else
    (void)ms;
#endif
}
