#include "power_layout.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
    for (bool portrait : {false, true}) {
        const int width = portrait ? 720 : 1280;
        const int footer = portrait ? 1210 : 650;
        for (auto action : tab5::power_actions) {
            const auto b = tab5::power_button(portrait, action);
            assert(b.x >= 0 && b.y >= (portrait ? 840 : 512));
            assert(b.x + b.width <= width && b.y + b.height <= footer);
            assert(b.width >= 100 && b.height >= 80);
            assert(tab5::power_hit(portrait, b.x, b.y) == action);
            assert(tab5::power_hit(portrait, b.x + b.width - 1,
                                   b.y + b.height - 1) == action);
            assert(tab5::power_hit(portrait, b.x + b.width / 2,
                                   b.y + b.height / 2) == action);
            for (auto other : tab5::power_actions) {
                if (other == action) continue;
                const auto c = tab5::power_button(portrait, other);
                assert(b.x + b.width <= c.x || c.x + c.width <= b.x ||
                       b.y + b.height <= c.y || c.y + c.height <= b.y);
            }
        }
        assert(tab5::power_hit(portrait, 0, 0) == tab5::PowerAction::none);
    }
    std::puts("PASS: Sleep/Lock/Rotate/Power hit areas, bounds and separation");
}
