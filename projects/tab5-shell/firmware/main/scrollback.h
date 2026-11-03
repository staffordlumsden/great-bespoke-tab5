#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "term_core/screen.hpp"

namespace tab5_shell {
enum class ScrollAction : std::uint8_t { none, older, newer, oldest, live };
ScrollAction scroll_key(std::uint8_t usage, std::uint8_t modifiers);
ScrollAction scroll_button(int x, int y);
ScrollAction scroll_swipe(int start_x, int start_y, int end_x, int end_y,
                          int terminal_bottom);
std::string_view scroll_navigation(ScrollAction action);

class Scrollback {
public:
    std::size_t offset() const { return offset_; }
    bool active() const { return offset_ != 0; }
    void reset();
    // True if the visible page must change (evicted anchor, clear, alt screen).
    // Otherwise incoming output advances the offset without repainting history.
    bool observe(const term::Screen& screen);
    bool move(ScrollAction action, const term::Screen& screen, std::size_t amount);
    bool draw(const term::Screen& screen, term::IDisplay& display,
              std::uint16_t visible_rows);
private:
    std::size_t offset_{0};
    std::uint64_t observed_total_{0};
    bool initialized_{false};
    std::vector<term::Cell> view_;
};
} // namespace tab5_shell
