#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "term_core/idisplay.hpp"

namespace tab5_terminal {

inline constexpr uint16_t kDefaultTerminalColumns = 60;
inline constexpr uint16_t kDefaultTerminalRows = 22;

enum class ZoomLevel : uint8_t { compact, readable, large };

enum class SessionState : uint8_t {
    starting,
    waiting_wifi,
    connecting,
    connected,
    disconnected,
    failed,
};

struct UiStatus {
    SessionState session{SessionState::starting};
    const char* profile_message{"Loading private SD profile"};
    const char* ssid{"--"};
    const char* ip{"--"};
    const char* host_label{"--"};
    const char* failure{"--"};
    int rssi{0};
    bool config_ready{false};
    bool tab5_keyboard_connected{false};
    bool usb_keyboard_connected{false};
    bool tab5_keyboard_input_verified{false};
    bool usb_keyboard_input_verified{false};
    bool c6_known{false};
    bool c6_compatible{false};
    uint32_t c6_major{0};
    uint32_t c6_minor{0};
    uint32_t c6_patch{0};
    uint16_t terminal_columns{kDefaultTerminalColumns};
    uint16_t terminal_rows{kDefaultTerminalRows};
    uint8_t font_points{10};
    size_t scroll_offset{0};
    size_t scrollback_rows{0};
};

enum class TouchAction : uint8_t {
    none,
    reconnect,
    console,
    zoom_out,
    zoom_in,
    toggle_sidebar,
    scroll_older,
    scroll_newer,
};

class Tab5TerminalDisplay final : public term::IDisplay {
public:
    term::Result<void> init() override;
    uint16_t cols() const override { return columns_; }
    uint16_t rows() const override { return rows_; }
    term::Result<void> draw_cells(uint16_t row, uint16_t col,
                                  std::span<const term::Cell> cells) override;
    term::Result<void> flush(term::DamageRect damage) override;

    bool draw_chrome(const UiStatus& status);
    bool draw_status(const UiStatus& status);
    bool present_all();
    bool draw_view(std::span<const term::Cell> cells);
    bool set_zoom(ZoomLevel level);
    ZoomLevel zoom_level() const { return zoom_level_; }
    uint8_t font_points() const { return font_points_; }
    bool set_sidebar_collapsed(bool collapsed);
    bool sidebar_collapsed() const { return sidebar_collapsed_; }
    bool terminal_contains(int x, int y) const;
    TouchAction hit_test(int x, int y) const;

private:
    bool present_enabled_{false};
    ZoomLevel zoom_level_{ZoomLevel::readable};
    uint16_t columns_{kDefaultTerminalColumns};
    uint16_t rows_{kDefaultTerminalRows};
    uint8_t cell_width_{16};
    uint8_t cell_height_{27};
    uint8_t font_points_{10};
    bool sidebar_collapsed_{false};
    bool session_connected_{false};
};

}  // namespace tab5_terminal
