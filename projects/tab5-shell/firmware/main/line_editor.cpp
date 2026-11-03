#include "line_editor.h"

#include <algorithm>

namespace tab5_shell {

LineEditor::LineEditor(std::size_t capacity) : capacity_(capacity) {}

EditResult LineEditor::feed(std::string_view sequence) {
    EditResult result;
    if (sequence.empty()) return result;

    if (sequence == "\x1b[A") {
        const std::string before = line_;
        history_previous();
        result.changed = before != line_;
        return result;
    }
    if (sequence == "\x1b[B") {
        const std::string before = line_;
        history_next();
        result.changed = before != line_;
        return result;
    }
    if (sequence == "\x1b[C" || sequence == "\x1bOC") {
        if (cursor_ < line_.size()) {
            ++cursor_;
            result.changed = true;
        }
        return result;
    }
    if (sequence == "\x1b[D" || sequence == "\x1bOD") {
        if (cursor_ > 0) {
            --cursor_;
            result.changed = true;
        }
        return result;
    }
    if (sequence == "\x1b[H" || sequence == "\x1bOH" || sequence == "\x1b[1~") {
        if (cursor_ != 0) {
            cursor_ = 0;
            result.changed = true;
        }
        return result;
    }
    if (sequence == "\x1b[F" || sequence == "\x1bOF" || sequence == "\x1b[4~") {
        if (cursor_ != line_.size()) {
            cursor_ = line_.size();
            result.changed = true;
        }
        return result;
    }
    if (sequence == "\x1b[3~") {
        if (cursor_ < line_.size()) {
            line_.erase(cursor_, 1);
            result.changed = true;
        }
        return result;
    }

    if (sequence.size() != 1) return result;
    const unsigned char value = static_cast<unsigned char>(sequence.front());
    if (value == '\r' || value == '\n') {
        result.execute = true;
        return result;
    }
    if (value == 0x03) {
        result.interrupt = true;
        return result;
    }
    if (value == 0x01) {  // Ctrl+A: beginning of line
        if (cursor_ != 0) {
            cursor_ = 0;
            result.changed = true;
        }
        return result;
    }
    if (value == 0x05) {  // Ctrl+E: end of line
        if (cursor_ != line_.size()) {
            cursor_ = line_.size();
            result.changed = true;
        }
        return result;
    }
    if (value == 0x0b) {  // Ctrl+K: kill to end of line
        if (cursor_ < line_.size()) {
            line_.erase(cursor_);
            result.changed = true;
        }
        return result;
    }
    if (value == 0x15) {  // Ctrl+U: discard to beginning of line
        if (cursor_ > 0) {
            line_.erase(0, cursor_);
            cursor_ = 0;
            result.changed = true;
        }
        return result;
    }
    if (value == 0x17) {  // Ctrl+W: delete previous word
        if (cursor_ > 0) {
            std::size_t end = cursor_;
            while (end > 0 && line_[end - 1] == ' ') --end;
            while (end > 0 && line_[end - 1] != ' ') --end;
            line_.erase(end, cursor_ - end);
            cursor_ = end;
            result.changed = true;
        }
        return result;
    }
    if (value == 0x08 || value == 0x7f) {
        if (cursor_ > 0) {
            line_.erase(cursor_ - 1, 1);
            --cursor_;
            result.changed = true;
        }
        return result;
    }
    if (value == '\t') {
        if (line_.size() + 2 <= capacity_) {
            line_.insert(cursor_, "  ");
            cursor_ += 2;
            result.changed = true;
        }
        return result;
    }
    if (value >= 32 && value <= 126 && line_.size() < capacity_) {
        line_.insert(line_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                     static_cast<char>(value));
        ++cursor_;
        result.changed = true;
    }
    return result;
}

void LineEditor::remember(std::string_view command) {
    if (!command.empty() && (history_.empty() || history_.back() != command)) {
        history_.emplace_back(command);
        if (history_.size() > 64) history_.erase(history_.begin());
    }
    history_position_ = history_.size();
    draft_.clear();
}

void LineEditor::reset() {
    line_.clear();
    cursor_ = 0;
    history_position_ = history_.size();
    draft_.clear();
}

void LineEditor::replace_from_history(std::string_view value) {
    line_.assign(value);
    cursor_ = line_.size();
}

void LineEditor::history_previous() {
    if (history_.empty()) return;
    if (history_position_ == history_.size()) draft_ = line_;
    if (history_position_ > 0) --history_position_;
    replace_from_history(history_[history_position_]);
}

void LineEditor::history_next() {
    if (history_.empty() || history_position_ >= history_.size()) return;
    ++history_position_;
    if (history_position_ == history_.size())
        replace_from_history(draft_);
    else
        replace_from_history(history_[history_position_]);
}

}  // namespace tab5_shell
