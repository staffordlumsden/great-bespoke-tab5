#pragma once

#include <cstddef>
#include <cstdint>
#include "app_config.hpp"

class SshClient {
public:
    ~SshClient() { disconnect(); }

    bool connect(const SshProfile& profile, int cols, int rows, String& error);
    void disconnect();

    bool connected() const { return connected_; }
    const String& hostFingerprintHex() const { return fingerprintHex_; }
    bool lastHostFirstUse() const { return firstUse_; }
    const String& lastError() const { return lastError_; }

    static bool probeHost(const String& host, uint16_t port, int timeoutMs, uint32_t& latencyMs, String& info);

    int read(uint8_t* buffer, size_t maxLen);
    bool write(const uint8_t* data, size_t len);
    bool writeChar(char c) { return write(reinterpret_cast<const uint8_t*>(&c), 1); }
    bool writeText(const String& text) {
        return write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
    }
    void resizePty(int cols, int rows);

    bool execCommand(const String& command, String& output, String& error, uint32_t timeoutMs = 8000);

    // SCP transfers using /sdcard POSIX files or raw memory buffers.
    bool scpDownloadFile(const String& remotePath, const String& localFullPath, String& error);
    bool scpUploadFile(const String& localFullPath, const String& remotePath, String& error);
    bool scpUploadMemory(const uint8_t* data, size_t len, const String& remotePath, String& error, int mode = 0644);

    // Secondary SSH channel for tab5-fuse-server.py RPC bridge.
    bool startBridge(const String& command, String& error);
    void stopBridge();
    bool bridgeActive() const { return bridgeChannel_ != nullptr; }
    int readBridge(uint8_t* buffer, size_t maxLen);
    bool writeBridge(const uint8_t* data, size_t len);

private:
    int sock_{-1};
    void* session_{nullptr};
    void* channel_{nullptr};
    void* bridgeChannel_{nullptr};
    bool connected_{false};
    bool firstUse_{false};
    String fingerprintHex_;
    String lastError_;
    uint32_t lastKeepaliveMs_{0};
    int lastCols_{0};
    int lastRows_{0};
    // 0: no fatal outcome; -1: TCP EOF; -2: zero-length receive; >0: errno.
    int lastReceiveStatus_{0};

    bool sendChannelAll(void* ch, const uint8_t* data, size_t len, uint32_t timeoutMs = 5000);
};
