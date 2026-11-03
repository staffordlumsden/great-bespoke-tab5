#pragma once

namespace tab5_shell {

struct BootSignals {
    bool nvs_ready{false};
    bool display_ready{false};
    bool console_available{false};

    // Recorded separately so host tests can prove they are not startup gates.
    bool radio_ready{false};
    bool sd_ready{false};
};

inline bool can_confirm_local_shell(const BootSignals& signals) {
    return signals.nvs_ready && signals.display_ready && signals.console_available;
}

}  // namespace tab5_shell
