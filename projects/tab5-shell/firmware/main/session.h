#pragma once
#include "shell_core.h"
#include <array>

namespace tab5_shell {
struct SshRequest {
    std::string host, user, key, profile;
    std::uint16_t port{22};
};
bool parse_ssh(const std::vector<std::string>& args, SshRequest& request, std::string& error);
bool select_profile(std::string_view contents, std::string_view name, SshRequest& request, std::string& error);
bool known_host_matches(std::string_view contents, const SshRequest& request,
                        const std::array<std::uint8_t,32>& fingerprint);
enum class SessionState { local, connecting, remote, stopping };
class SessionFlow {
public:
    SessionState state() const { return state_; }
    bool begin() { if (state_ != SessionState::local) return false; state_ = SessionState::connecting; return true; }
    void finish_connect(bool success) { if (state_ == SessionState::connecting) state_ = success ? SessionState::remote : SessionState::local; }
    void stop() { if (state_ != SessionState::local) state_ = SessionState::stopping; }
    void transport_idle() { state_ = SessionState::local; }
private:
    SessionState state_{SessionState::local};
};
} // namespace tab5_shell
