#include "document_mode.h"
#include <algorithm>
#include <charconv>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace tab5_shell {
bool editable_text(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return c == '\n' || c == '\t' || (c >= 32 && c <= 126);
    });
}
std::string display_text(std::string_view text) {
    std::string result;
    for (unsigned char c : text) {
        if (c == '\t') result += "    ";
        else if (c >= 32 && c <= 126) result += static_cast<char>(c);
        else result += '?'; // no document-supplied terminal escape sequences
    }
    return result;
}
std::vector<ViewRow> markdown_rows(std::string_view text, std::size_t width) {
    std::vector<ViewRow> rows;
    width = std::clamp<std::size_t>(width, 8, 160);
    bool code = false;
    std::size_t at = 0;
    while (at <= text.size()) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string line = display_text(text.substr(at, end - at));
        if (!line.empty() && line.back() == '?') { // strip only actual CR in CRLF
            if (end > at && text[end - 1] == '\r') line.pop_back();
        }
        int style = code ? 36 : 0;
        if (line.starts_with("```") || line.starts_with("~~~")) {
            code = !code; line = code ? "--- code " + line.substr(3) : "---"; style = 36;
        } else if (!code) {
            if (line.starts_with('#')) {
                auto start = line.find_first_not_of('#');
                if (start != std::string::npos && start <= 6 && line[start] == ' ') {
                    line.erase(0, start + 1); style = 33;
                }
            } else {
                auto first = line.find_first_not_of(' ');
                if (first != std::string::npos && first + 1 < line.size() &&
                    (line[first] == '-' || line[first] == '*' || line[first] == '+') && line[first+1] == ' ')
                    line[first] = '*';
            }
        }
        if (line.empty()) rows.push_back({"", style});
        while (!line.empty()) {
            std::size_t count = std::min(width, line.size());
            if (!code && count < line.size()) {
                auto space = line.rfind(' ', count);
                if (space != std::string::npos && space > width / 3) count = space;
            }
            rows.push_back({line.substr(0, count), style});
            line.erase(0, count);
            if (!code && !line.empty() && line.front() == ' ') line.erase(0, 1);
        }
        if (end == text.size()) break;
        at = end + 1;
    }
    return rows;
}

bool DocumentMode::open(std::string_view path, bool markdown, bool recover, std::string& error) {
    active_ = false;
    auto normalized = normalize_path(shell_.cwd(), path);
    if (!normalized.ok) { error = normalized.error; return false; }
    path_ = normalized.logical;
    if (!markdown && path_.find(".tab5-") != std::string::npos) {
        error = "recovery files: read with cat; recover using nano --recover ORIGINAL"; return false;
    }
    std::string physical;
    if (!shell_.file_path(path_, !markdown, physical, error)) return false;
    struct stat info{};
    original_exists_ = stat(physical.c_str(), &info) == 0;
    if (!original_exists_ && (errno != ENOENT || markdown)) { error = "cannot read file"; return false; }
    original_.clear();
    if (original_exists_ && !shell_.read_file(path_, original_, error)) return false;
    if (!markdown && !recover) {
        for (const auto* suffix : {".tab5-draft", ".tab5-draft.tab5-new", ".tab5-draft.tab5-prev", ".tab5-new", ".tab5-prev"}) {
            std::string recovery;
            if (!shell_.file_path(path_ + suffix, true, recovery, error)) return false;
            struct stat saved{};
            if (stat(recovery.c_str(), &saved) == 0 || errno != ENOENT) {
                error = "Recovery copy exists; nano --recover FILE (inspect/remove sidecars before saving)";
                return false;
            }
        }
    }
    text_ = original_;
    if (recover) {
        bool loaded = false;
        for (const auto* suffix : {".tab5-draft", ".tab5-draft.tab5-new", ".tab5-draft.tab5-prev", ".tab5-new", ".tab5-prev"}) {
            if (shell_.read_file(path_ + suffix, text_, error)) { loaded = true; break; }
        }
        if (!loaded) { error = "no readable recovery copy"; return false; }
    }
    if (!markdown && !editable_text(text_)) {
        error = "nano supports ASCII/LF text only (64 KiB); binary/UTF-8/CRLF left unchanged";
        return false;
    }
    active_ = true; markdown_ = markdown; dirty_ = recover;
    cursor_ = top_ = left_ = 0; prompt_ = Prompt::none; query_.clear();
    draft_pending_ = recover;
    markdown_lines_.clear();
    message_ = markdown ? "Arrows/PgUp/PgDn scroll; q exits" : "^O/^S Save  ^W Search  ^G Line  ^X Exit; --recover restores draft";
    return true;
}
std::size_t DocumentMode::line_start(std::size_t at) const {
    if (!at) return 0;
    auto pos = text_.rfind('\n', at - 1);
    return pos == std::string::npos ? 0 : pos + 1;
}
std::size_t DocumentMode::line_end(std::size_t at) const {
    auto pos = text_.find('\n', at);
    return pos == std::string::npos ? text_.size() : pos;
}
void DocumentMode::move_vertical(int delta) {
    auto start = line_start(cursor_);
    auto column = cursor_ - start;
    if (delta < 0 && start) cursor_ = std::min(line_start(start - 1) + column, start - 1);
    if (delta > 0) {
        auto end = line_end(cursor_);
        if (end < text_.size()) cursor_ = std::min(end + 1 + column, line_end(end + 1));
    }
}
bool DocumentMode::checkpoint() {
    if (!active_ || markdown_ || !draft_pending_) return true;
    std::string error;
    if (!shell_.write_file(path_ + ".tab5-draft", text_, error)) {
        message_ = "Draft failed: " + error; return false;
    }
    draft_pending_ = false;
    return true;
}
bool DocumentMode::save() {
    std::string current, error, physical;
    if (!shell_.file_path(path_, true, physical, error)) { message_ = error; return false; }
    struct stat info{};
    bool exists = stat(physical.c_str(), &info) == 0;
    if ((exists != original_exists_) || (exists &&
        (!shell_.read_file(path_, current, error) || current != original_))) {
        message_ = "File changed outside editor; draft retained"; return false;
    }
    if (!shell_.write_file(path_, text_, error)) { message_ = error; return false; }
    original_ = text_; original_exists_ = true; dirty_ = draft_pending_ = false;
    if (shell_.file_path(path_ + ".tab5-draft", true, physical, error)) {
        if (unlink(physical.c_str()) && errno != ENOENT) {
            message_ = "Saved; draft cleanup failed"; return true;
        }
    }
    message_ = "Saved " + path_; return true;
}
void DocumentMode::find() {
    if (query_.empty()) return;
    auto pos = text_.find(query_, std::min(cursor_ + 1, text_.size()));
    if (pos == std::string::npos) pos = text_.find(query_);
    if (pos == std::string::npos) message_ = "Not found";
    else { cursor_ = pos; message_ = "Found: " + query_; }
}
bool DocumentMode::feed(std::string_view key) {
    if (!active_) return false;
    if (markdown_) {
        std::size_t last = markdown_lines_.size() > page_rows_ ? markdown_lines_.size() - page_rows_ : 0;
        if (key == "q" || key == "\x03" || key == "\x18") active_ = false;
        else if (key == "\x1b[A" || key == "k") { if (top_) --top_; }
        else if (key == "\x1b[B" || key == "j") top_ = std::min(top_ + 1, last);
        else if (key == "\x1b[5~") top_ = top_ > page_rows_ ? top_ - page_rows_ : 0;
        else if (key == "\x1b[6~" || key == " ") top_ = std::min(top_ + page_rows_, last);
        else if (key == "\x1b[H") top_ = 0;
        else if (key == "\x1b[F") top_ = last;
        return active_;
    }
    if (prompt_ != Prompt::none) {
        if (key == "\x03" || key == "\x1b") { prompt_ = Prompt::none; return true; }
        if (prompt_ == Prompt::quit) {
            if (key == "y" || key == "Y") { if (save()) active_ = false; prompt_ = Prompt::none; }
            else if (key == "n" || key == "N") { if (checkpoint()) active_ = false; prompt_ = Prompt::none; }
            return active_;
        }
        if (key == "\r" || key == "\n") {
            if (prompt_ == Prompt::search) find();
            else {
                std::size_t target = 0;
                auto result = std::from_chars(query_.data(), query_.data() + query_.size(), target);
                if (result.ec == std::errc{} && target > 0) {
                    cursor_ = 0;
                    for (std::size_t i = 1; i < target && cursor_ < text_.size(); ++i) {
                        cursor_ = line_end(cursor_); if (cursor_ < text_.size()) ++cursor_;
                    }
                }
            }
            prompt_ = Prompt::none;
        } else if (key == "\x7f" || key == "\x08") { if (!query_.empty()) query_.pop_back(); }
        else if (key.size() == 1 && key[0] >= 32 && key[0] <= 126 && query_.size() < 80) query_ += key;
        return true;
    }
    if (key == "\x18") {
        if (dirty_) { prompt_ = Prompt::quit; message_ = "Save changes? y=yes n=exit with draft ^C=cancel"; }
        else active_ = false;
    } else if (key == "\x0f" || key == "\x13") (void)save();
    else if (key == "\x17" || key == "\x07") {
        prompt_ = key == "\x17" ? Prompt::search : Prompt::line; query_.clear();
    } else if (key == "\x1b[D") { if (cursor_) --cursor_; }
    else if (key == "\x1b[C") { if (cursor_ < text_.size()) ++cursor_; }
    else if (key == "\x1b[A") move_vertical(-1);
    else if (key == "\x1b[B") move_vertical(1);
    else if (key == "\x1b[5~" || key == "\x1b[6~") {
        for (std::size_t i=0; i<page_rows_; ++i) move_vertical(key == "\x1b[5~" ? -1 : 1);
    } else if (key == "\x01" || key == "\x1b[H") cursor_ = line_start(cursor_);
    else if (key == "\x05" || key == "\x1b[F") cursor_ = line_end(cursor_);
    else {
        std::string insert;
        auto before = text_.size();
        if (key == "\x7f" || key == "\x08") { if (cursor_) text_.erase(--cursor_, 1); }
        else if (key == "\x1b[3~") { if (cursor_ < text_.size()) text_.erase(cursor_, 1); }
        else if (key == "\x0b") {
            auto end = line_end(cursor_); if (end < text_.size()) ++end;
            cut_ = text_.substr(line_start(cursor_), end - line_start(cursor_));
            cursor_ = line_start(cursor_); text_.erase(cursor_, cut_.size());
        } else if (key == "\x15") insert = cut_;
        else if (key == "\r" || key == "\n") insert = "\n";
        else if (key == "\t") insert = "    ";
        else if (key.size() == 1 && key[0] >= 32 && key[0] <= 126) insert = key;
        if (text_.size() + insert.size() <= 65536) { text_.insert(cursor_, insert); cursor_ += insert.size(); }
        else message_ = "64 KiB editor limit";
        if (text_.size() != before || !insert.empty()) dirty_ = draft_pending_ = true;
    }
    return active_;
}
std::vector<ViewRow> DocumentMode::screen(std::size_t width, std::size_t height) {
    width = std::clamp<std::size_t>(width, 8, 160); height = std::max<std::size_t>(height, 4);
    page_rows_ = height - 3;
    std::vector<ViewRow> rows(height);
    rows[0] = {display_text((markdown_ ? " GLOW " : " NANO ") + path_ + (dirty_ ? " *" : "")), 7};
    if (markdown_) {
        if (markdown_lines_.empty()) markdown_lines_ = markdown_rows(text_, width);
        for (std::size_t i=0; i<page_rows_ && top_+i<markdown_lines_.size(); ++i) rows[i+1] = markdown_lines_[top_+i];
        cursor_row_ = height - 1; cursor_col_ = 0;
    } else {
        std::vector<std::size_t> starts{0};
        for (std::size_t i=0; i<text_.size(); ++i) if (text_[i] == '\n') starts.push_back(i+1);
        auto row = static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), cursor_) - starts.begin() - 1);
        auto column = display_text(std::string_view(text_).substr(starts[row], cursor_-starts[row])).size();
        if (row < top_) top_ = row;
        if (row >= top_ + page_rows_) top_ = row - page_rows_ + 1;
        if (column < left_) left_ = column;
        if (column >= left_ + width) left_ = column - width + 1;
        for (std::size_t i=0; i<page_rows_ && top_+i<starts.size(); ++i) {
            auto start = starts[top_+i];
            auto line = display_text(std::string_view(text_).substr(start, line_end(start)-start));
            rows[i+1].text = left_ < line.size() ? line.substr(left_, width) : "";
        }
        cursor_row_ = row - top_ + 1; cursor_col_ = column - left_;
    }
    rows[height-2] = {display_text(message_), 33};
    rows[height-1] = {markdown_ ? "q Exit  Arrows Scroll  PgUp/PgDn Page  Home/End" : prompt_ == Prompt::search ? "Search: " + query_ : prompt_ == Prompt::line ? "Line: " + query_ : "^O Save  ^W Search  ^G Line  ^K Cut  ^U Paste  ^X Exit", 7};
    for (auto& row : rows) if (row.text.size() > width) row.text.resize(width);
    return rows;
}
} // namespace tab5_shell
