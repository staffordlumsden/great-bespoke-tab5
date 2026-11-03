#pragma once

#include <cstdint>

#include "term_core/idisplay.hpp"
#include "term_core/screen.hpp"

namespace tab5_shell {

class CursorRenderer {
public:
    CursorRenderer(const term::Screen& screen, term::IDisplay& display);

    void erase();
    void draw();
    void sync_after_render();
    void toggle_blink();

private:
    const term::Screen& screen_;
    term::IDisplay& display_;
    bool drawn_{false};
    std::uint16_t drawn_row_{0};
    std::uint16_t drawn_col_{0};
};

}  // namespace tab5_shell
