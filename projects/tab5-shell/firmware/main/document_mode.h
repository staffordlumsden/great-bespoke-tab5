#pragma once
#include "shell_core.h"
#include <string>
#include <vector>

namespace tab5_shell {
struct ViewRow { std::string text; int style{0}; };
// ASCII text is explicit: reject binary/Unicode in nano instead of corrupting it.
bool editable_text(std::string_view text);
std::string display_text(std::string_view text);
std::vector<ViewRow> markdown_rows(std::string_view text, std::size_t width);

class DocumentMode {
public:
    explicit DocumentMode(Shell& shell) : shell_(shell) {}
    bool open(std::string_view path, bool markdown, bool recover, std::string& error);
    bool feed(std::string_view key); // true while modal; false returns local prompt
    bool checkpoint();
    std::vector<ViewRow> screen(std::size_t width, std::size_t height);
    bool active() const { return active_; }
    bool dirty() const { return dirty_; }
    const std::string& text() const { return text_; }
    const std::string& message() const { return message_; }
    std::size_t cursor_row() const { return cursor_row_; }
    std::size_t cursor_col() const { return cursor_col_; }
private:
    bool save();
    void find();
    std::size_t line_start(std::size_t at) const;
    std::size_t line_end(std::size_t at) const;
    void move_vertical(int delta);
    Shell& shell_;
    bool active_{false}, markdown_{false}, dirty_{false}, original_exists_{false};
    bool draft_pending_{false};
    std::string path_, text_, original_, message_, query_, cut_;
    enum class Prompt { none, search, line, quit } prompt_{Prompt::none};
    std::size_t cursor_{0}, top_{0}, left_{0}, cursor_row_{0}, cursor_col_{0};
    std::size_t page_rows_{20};
    std::vector<ViewRow> markdown_lines_;
};
} // namespace tab5_shell
