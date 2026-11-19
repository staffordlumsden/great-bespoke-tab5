#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "app_config.hpp"
#include "arduino_compat.hpp"
#include "python_runner.hpp"
#include "ssh_client.hpp"
#include "terminal_buffer.hpp"
#include "wifi_runtime.hpp"

enum class SshMenuStep : uint8_t {
    Inactive = 0,
    Menu,
    AddHost,
    AddName,
    AddUser,
    AddPort,
    AddKeyPath,
    AddPassword,
    PingTarget,
};

struct SshCliSession {
    bool active{false};
    SshMenuStep step{SshMenuStep::Inactive};
    SshProfile draft{};
};

struct CliContext {
    AppConfig& config;
    TerminalBuffer& terminal;
    WifiRuntime& wifi;
    SshClient& ssh;
    PythonRunner& python;

    String sdCwd{"/"};
    bool pythonReplMode{false};
    SshCliSession* sshSession{nullptr};

    std::function<void(const String&)> appendLine;
    std::function<void()> clearTerminal;
    std::function<void()> connectSsh;
    std::function<void()> disconnectSsh;
    std::function<void(uint8_t)> setRotation;
    std::function<void(bool)> setDarkTheme;
    std::function<void()> saveConfig;
    std::function<void(bool replace)> syncSdProfiles;
    std::function<bool(const String& path, const String& mode, String& err)> showImage;
    std::function<void()> returnToConsole;
    std::function<bool(const String& host, uint16_t port, uint32_t& latencyMs, String& info)> pingHost;
    std::function<bool()> isSdMounted;
};

String sshSessionPlainPrompt(const SshCliSession& session);
String sshSessionColoredPrompt(const SshCliSession& session);

// Normalizes a thin-client path (relative, "/", "/...", "sd:/...") to a normalized
// SD-relative path like "/demos/hat.py" and a POSIX VFS path "/sdcard/demos/hat.py".
String normalizeSdRelPath(const String& rawPath, const String& cwd);
String sdVfsPath(const String& rawPath, const String& cwd);
std::vector<String> splitCommandTokens(const String& line);

void executeLocalCliCommand(CliContext& ctx, const String& rawLine);
