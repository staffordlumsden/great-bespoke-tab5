#include "note_model.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Howard Hinnant & Sakamoto date algorithms
int day_of_week(int y, int m, int d) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

void civil_from_days(int z, int& y, unsigned& m, unsigned& d) noexcept {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
}

}  // namespace

int main() {
    std::cout << "Running Tab5 QuickNote host tests...\n";

    // 1. Editor basic edits
    quicknote::Editor editor;
    const std::string text = "Hello Tab5 Terminal QuickNote!";
    for (char c : text) {
        uint8_t b = static_cast<uint8_t>(c);
        assert(quicknote::process_key_bytes(editor, &b, 1, false) == quicknote::Input::changed);
    }
    assert(editor.size() == text.size());
    assert(std::string(editor.data()) == text);

    // Newline and continuation
    uint8_t nl = '\n';
    assert(quicknote::process_key_bytes(editor, &nl, 1, false) == quicknote::Input::changed);
    const std::string line2 = "Second line";
    for (char c : line2) {
        uint8_t b = static_cast<uint8_t>(c);
        assert(quicknote::process_key_bytes(editor, &b, 1, false) == quicknote::Input::changed);
    }

    // Cursor position & stats
    size_t line = 0, col = 0;
    editor.cursor_position(line, col);
    assert(line == 2);
    assert(col == line2.size() + 1);

    size_t words = 0, lines = 0;
    editor.stats(words, lines);
    assert(lines == 2);
    assert(words == 6);  // "Hello", "Tab5", "Terminal", "QuickNote!", "Second", "line"

    // Backspace
    uint8_t bs = 127;
    assert(quicknote::process_key_bytes(editor, &bs, 1, false) == quicknote::Input::changed);
    assert(std::string(editor.data()).find("Second lin") != std::string::npos);

    // Ctrl+S
    uint8_t ctrl_s = 19;
    assert(quicknote::process_key_bytes(editor, &ctrl_s, 1, false) == quicknote::Input::save);

    // Arrows
    uint8_t up_arr[] = {27, '[', 'A'};
    assert(quicknote::process_key_bytes(editor, up_arr, 3, false) == quicknote::Input::changed);
    editor.cursor_position(line, col);
    assert(line == 1);

    uint8_t down_arr[] = {27, '[', 'B'};
    assert(quicknote::process_key_bytes(editor, down_arr, 3, false) == quicknote::Input::changed);
    editor.cursor_position(line, col);
    assert(line == 2);

    // Zoom
    uint8_t zoom_in[] = {27, '+'};
    assert(quicknote::process_key_bytes(editor, zoom_in, 2, false) == quicknote::Input::zoom_in);
    uint8_t zoom_out[] = {27, '-'};
    assert(quicknote::process_key_bytes(editor, zoom_out, 2, false) == quicknote::Input::zoom_out);

    // 2. Storage & Draft Durability
    char temp_dir[] = "/tmp/tab5_qn_test_XXXXXX";
    char* base = mkdtemp(temp_dir);
    assert(base != nullptr);
    std::filesystem::path root = std::filesystem::path(base) / "files/QuickNote";
    char err[128]{};

    assert(quicknote::ensure_note_directories(root.c_str(), err, sizeof(err)));
    assert(quicknote::checkpoint_draft(root.c_str(), editor, err, sizeof(err)));

    // Verify draft file content
    quicknote::Editor restored;
    assert(quicknote::load_draft(root.c_str(), restored, err, sizeof(err)));
    assert(restored.size() == editor.size());
    assert(std::string(restored.data()) == std::string(editor.data()));

    // Verify rotation recovery from .draft.prev
    std::filesystem::rename(root / ".draft", root / ".draft.prev");
    quicknote::Editor recovered_prev;
    assert(quicknote::load_draft(root.c_str(), recovered_prev, err, sizeof(err)));
    assert(recovered_prev.size() == editor.size());

    // Append to daily note
    assert(quicknote::append_note(root.c_str(), "2026-10-31",
                                  "Saturday, 31 October 2026", "15:30",
                                  editor, err, sizeof(err)));

    auto daily_file = root / "Daily/2026-10-31.md";
    assert(std::filesystem::exists(daily_file));
    std::string md = read_file(daily_file);
    assert(md.find("# Saturday, 31 October 2026\n\n") == 0);
    assert(md.find("- 15:30 Hello Tab5 Terminal QuickNote!\n  Second lin\n") != std::string::npos);

    // Append second note on same day
    quicknote::Editor second_note;
    second_note.assign("Another short thought.", 22);
    assert(quicknote::append_note(root.c_str(), "2026-10-31",
                                  "Saturday, 31 October 2026", "15:45",
                                  second_note, err, sizeof(err)));
    md = read_file(daily_file);
    // Heading should only appear once
    assert(md.find("# Saturday", 1) == std::string::npos);
    assert(md.find("- 15:45 Another short thought.\n") != std::string::npos);

    // Load day tail
    char tail[256];
    bool truncated = false;
    size_t tail_len = quicknote::load_day_tail(root.c_str(), "2026-10-31", tail, sizeof(tail), truncated, err, sizeof(err));
    assert(tail_len > 0);
    assert(!truncated);
    assert(std::string(tail).find("Another short thought") != std::string::npos);

    // Discard draft
    assert(quicknote::discard_draft(root.c_str(), err, sizeof(err)));
    quicknote::Editor empty_draft;
    assert(quicknote::load_draft(root.c_str(), empty_draft, err, sizeof(err)));
    assert(empty_draft.size() == 0);

    // Clean up temporary test files
    std::filesystem::remove_all(base);

    // 3. Calendar Math
    // 2026-10-31 should be Saturday (6)
    assert(day_of_week(2026, 10, 31) == 6);
    int days_31 = days_from_civil(2026, 10, 31);
    int y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days_31, y, m, d);
    assert(y == 2026 && m == 10 && d == 31);

    // Offset -1 day: should be 2026-10-30 (Friday = 5)
    civil_from_days(days_31 - 1, y, m, d);
    assert(y == 2026 && m == 10 && d == 30);
    assert(day_of_week(y, m, d) == 5);

    // Offset +1 day: should be 2026-11-01 (Sunday = 0)
    civil_from_days(days_31 + 1, y, m, d);
    assert(y == 2026 && m == 11 && d == 1);
    assert(day_of_week(y, m, d) == 0);

    std::cout << "All Tab5 QuickNote host tests passed!\n";
    return 0;
}
