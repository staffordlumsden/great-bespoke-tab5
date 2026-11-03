#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "term_core/idisplay.hpp"
#include "touch_keyboard.h"

namespace tab5_shell {

inline constexpr std::uint16_t kShellColumns = 80;
inline constexpr std::uint16_t kShellRows = 24;

struct ShellStatus {
    bool sd_ready{false};
    bool home_ready{false};
    bool tab5_keyboard_ready{false};
    bool usb_keyboard_ready{false};
    int last_status{0};
    const char* message{"LOCAL SHELL"};
    std::size_t scroll_offset{0};
    std::size_t scrollback_rows{0};
};

class ShellDisplay final : public term::IDisplay {
public:
    term::Result<void> init() override;
    std::uint16_t cols() const override { return kShellColumns; }
    std::uint16_t rows() const override { return kShellRows; }
    term::Result<void> draw_cells(std::uint16_t row, std::uint16_t col,
                                  std::span<const term::Cell> cells) override;
    term::Result<void> flush(term::DamageRect damage) override;

    bool draw_chrome(const ShellStatus& status, bool present);
    bool draw_status(const ShellStatus& status);
    bool present_all();
    bool console_hit_test(int x, int y) const;
    bool draw_touch_keyboard(const TouchKeyboard& keyboard);
    void keyboard_visible(bool visible) { keyboard_visible_=visible; }

private:
    bool present_enabled_{false};
    bool keyboard_visible_{false};
};

}  // namespace tab5_shell
