#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tab5_shell {

struct EditResult {
    bool changed{false};
    bool execute{false};
    bool interrupt{false};
};

class LineEditor {
public:
    explicit LineEditor(std::size_t capacity = 512);

    EditResult feed(std::string_view sequence);
    void remember(std::string_view command);
    void reset();

    const std::string& line() const { return line_; }
    std::size_t cursor() const { return cursor_; }

private:
    void replace_from_history(std::string_view value);
    void history_previous();
    void history_next();

    std::size_t capacity_;
    std::string line_;
    std::size_t cursor_{0};
    std::vector<std::string> history_;
    std::size_t history_position_{0};
    std::string draft_;
};

}  // namespace tab5_shell
