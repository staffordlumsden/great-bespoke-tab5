#pragma once

namespace tab5 {
enum class PowerAction { none, sleep, lock, rotate, power };
struct PowerButton {
    int x, y, width, height;
    constexpr bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};
constexpr PowerAction power_actions[] = {
    PowerAction::sleep, PowerAction::lock, PowerAction::rotate, PowerAction::power
};
constexpr PowerButton power_button(bool portrait, PowerAction action) {
    const int i = static_cast<int>(action) - 1;
    return portrait ? PowerButton{24 + (i % 2) * 340, 852 + (i / 2) * 112, 332, 100}
                    : PowerButton{500 + i * 192, 512, 180, 126};
}
constexpr PowerAction power_hit(bool portrait, int x, int y) {
    for (auto action : power_actions)
        if (power_button(portrait, action).contains(x, y)) return action;
    return PowerAction::none;
}
} // namespace tab5
