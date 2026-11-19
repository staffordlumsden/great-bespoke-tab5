#pragma once

#include <functional>
#include "arduino_compat.hpp"
#include "ssh_client.hpp"

class StorageBridge {
public:
    using LogFn = std::function<void(const String&)>;

    void reset();
    bool start(SshClient& ssh, const String& remoteHome, const LogFn& log);
    void poll(SshClient& ssh);
    bool running() const { return running_; }

private:
    bool running_{false};
    String lineBuf_;
    String remoteHome_;

    void handleRequest(SshClient& ssh, const String& line);
    void sendReply(SshClient& ssh, const String& id, const String& payload);
    void sendError(SshClient& ssh, const String& id, int code);
};
