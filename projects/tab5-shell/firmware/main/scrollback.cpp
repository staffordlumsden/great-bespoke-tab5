#include "scrollback.h"
#include <algorithm>
#include <cstdlib>

namespace tab5_shell {
ScrollAction scroll_key(std::uint8_t usage, std::uint8_t modifiers) {
    constexpr auto shift = 0x02 | 0x20;
    if (!(modifiers & shift) || (modifiers & ~shift)) return ScrollAction::none;
    switch (usage) {
        case 0x4b: return ScrollAction::older;
        case 0x4e: return ScrollAction::newer;
        case 0x4a: return ScrollAction::oldest;
        case 0x4d: return ScrollAction::live;
        default: return ScrollAction::none;
    }
}
ScrollAction scroll_button(int x, int y) {
    if (y < 688 || y >= 716) return ScrollAction::none;
    if (x >= 624 && x < 704) return ScrollAction::older;
    if (x >= 712 && x < 800) return ScrollAction::newer;
    if (x >= 808 && x < 912) return ScrollAction::live;
    return ScrollAction::none;
}
ScrollAction scroll_swipe(int start_x, int start_y, int end_x, int end_y,
                          int terminal_bottom) {
    if (start_x < 0 || start_x >= 1280 || start_y < 36 || start_y >= terminal_bottom)
        return ScrollAction::none;
    const std::int64_t dy = static_cast<std::int64_t>(end_y) - start_y;
    const std::int64_t dx = static_cast<std::int64_t>(end_x) - start_x;
    if (std::abs(dy) < 48 || std::abs(dy) < 2 * std::abs(dx)) return ScrollAction::none;
    return dy > 0 ? ScrollAction::older : ScrollAction::newer;
}
std::string_view scroll_navigation(ScrollAction action) {
    switch (action) {
        case ScrollAction::older: return "\x1b[5~";
        case ScrollAction::newer: return "\x1b[6~";
        case ScrollAction::oldest: return "\x1b[H";
        case ScrollAction::live: return "\x1b[F";
        default: return {};
    }
}
void Scrollback::reset() {
    offset_ = 0; observed_total_ = 0; initialized_ = false;
}
bool Scrollback::observe(const term::Screen& screen) {
    const auto total = screen.scrollback_total_rows();
    bool redraw = false;
    if (active()) {
        if (screen.alt_active() || screen.scrollback_rows() == 0 || total < observed_total_) {
            offset_ = 0; redraw = true;
        } else if (initialized_) {
            const auto added = total - observed_total_;
            const auto available = screen.scrollback_rows();
            // Do not overflow size_t or uint64_t when a long-running session
            // wraps the bounded ring. Only retained rows can be displayed.
            if (added > available || offset_ > available - static_cast<std::size_t>(added)) {
                offset_ = available; redraw = true;
            } else offset_ += static_cast<std::size_t>(added);
        }
    }
    observed_total_ = total; initialized_ = true;
    return redraw;
}
bool Scrollback::move(ScrollAction action, const term::Screen& screen, std::size_t amount) {
    bool changed = observe(screen);
    if (screen.alt_active()) return changed;
    const auto previous = offset_;
    const auto maximum = screen.scrollback_rows();
    amount = std::max<std::size_t>(1, amount);
    switch (action) {
        case ScrollAction::older: offset_ += std::min(amount, maximum - offset_); break;
        case ScrollAction::newer: offset_ -= std::min(amount, offset_); break;
        case ScrollAction::oldest: offset_ = maximum; break;
        case ScrollAction::live: offset_ = 0; break;
        default: break;
    }
    return changed || previous != offset_;
}
bool Scrollback::draw(const term::Screen& screen, term::IDisplay& display,
                      std::uint16_t visible_rows) {
    visible_rows = std::min(visible_rows, screen.rows());
    if (!visible_rows) return false;
    view_.resize(static_cast<std::size_t>(screen.cols()) * screen.rows());
    if (!screen.copy_view(offset_, view_)) return false;
    for (std::uint16_t row = 0; row < visible_rows; ++row) {
        if (!display.draw_cells(row, 0, std::span<const term::Cell>(
                view_.data() + static_cast<std::size_t>(row) * screen.cols(), screen.cols())))
            return false;
    }
    return display.flush({0, 0, visible_rows, screen.cols()}).has_value();
}
} // namespace tab5_shell
