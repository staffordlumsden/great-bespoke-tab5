#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "term_core/cell.hpp"
#include "term_core/error.hpp"
#include "term_core/idisplay.hpp"
#include "term_core/parser.hpp"

namespace term {

// Terminal → host back-channel for query responses (DSR etc.). Wired to the
// active connection's send() on hardware; unset (responses dropped) is fine
// for local-echo use.
using ResponseSink = std::function<void(std::span<const uint8_t>)>;

class Screen : public IParserSink {
public:
    Screen(uint16_t cols, uint16_t rows);

    uint16_t cols() const { return cols_; }
    uint16_t rows() const { return rows_; }

    // Read-only cell access. Caller must keep row<rows() and col<cols().
    const Cell& at(uint16_t row, uint16_t col) const;

    uint16_t cursor_row() const { return cursor_row_; }
    uint16_t cursor_col() const { return cursor_col_; }

    bool cursor_visible() const { return cursor_visible_; }
    bool autowrap() const { return autowrap_; }
    bool alt_active() const { return alt_active_; }

    // Damage region (smallest bounding box of all cells modified since
    // the last take_damage() call). After taking, the region is reset.
    DamageRect take_damage();
    bool dirty() const { return !damage_.empty(); }

    // Mark a rectangular region [r0, r1) x [c0, c1) dirty without changing
    // cell contents — used when external overlays (e.g. soft keyboard) hide
    // some rows and need them re-rendered when revealed.
    void mark_region(uint16_t r0, uint16_t c0, uint16_t r1, uint16_t c1) {
        mark_rect(r0, c0, r1, c1);
    }

    // BEL (0x07) counter. Incremented by execute(); cleared by
    // take_bell_pending(). Caller is expected to ring once per drain
    // regardless of how many BELs accumulated.
    bool take_bell_pending() {
        bool b = bell_pending_;
        bell_pending_ = false;
        return b;
    }

    void reset();

    // Install the terminal → host response channel. May be called from the
    // parser sink's own task; the callback runs inside csi() dispatch.
    void set_response_sink(ResponseSink sink) { response_ = std::move(sink); }

    // Change the grid dimensions. Cells in the surviving rectangle keep
    // their contents; new cells are initialised to the default Cell. The
    // cursor, scroll region, and saved DECSC state are clamped to the new
    // bounds. Alt-screen backup is kept consistent at the new size — its
    // contents are discarded since dimensions differ.
    // Marks the full new grid dirty.
    void resize(uint16_t cols, uint16_t rows);

    // Bounded normal-screen scrollback. Rows are captured only when the full
    // primary screen scrolls upward; alternate-screen TUIs and partial DEC
    // scroll regions never pollute shell history. `offset_from_live == 0`
    // copies the live grid, while larger offsets walk toward older rows.
    size_t scrollback_rows() const { return scrollback_count_; }
    // Cumulative captured rows, including rows that evict old ring entries.
    // Viewers use this to anchor a historical page while new output arrives.
    uint64_t scrollback_total_rows() const { return scrollback_total_; }
    // An application may explicitly crop its local console above an overlay.
    // Default capture still requires a full-screen primary scroll region.
    void set_scrollback_capture_rows(uint16_t rows) {
        scrollback_capture_rows_ = rows > 0 && rows < rows_ ? rows : rows_;
    }
    bool copy_view(size_t offset_from_live, std::span<Cell> out) const;
    void clear_scrollback() {
        scrollback_count_ = 0;
        scrollback_head_ = 0;
    }

    // IParserSink:
    void put_char(char32_t ch) override;
    void execute(uint8_t c0) override;
    void csi(char final_byte,
             std::span<const int> params,
             std::span<const uint8_t> intermediates,
             bool private_marker) override;
    void esc(char final_byte,
             std::span<const uint8_t> intermediates) override;
    void osc(std::span<const uint8_t> data) override;

private:
    enum class Charset : uint8_t { ascii, dec_special_graphics };

    Cell& mut(uint16_t row, uint16_t col);
    void mark(uint16_t row, uint16_t col);
    void mark_range(uint16_t row, uint16_t c0, uint16_t c1);
    void mark_rect(uint16_t r0, uint16_t c0, uint16_t r1, uint16_t c1);

    void place_cursor(uint16_t row, uint16_t col);
    void carriage_return();
    void line_feed();         // moves down, scrolls within scroll region
    void reverse_index();     // ESC M
    void backspace();
    void tab();

    void scroll_up(uint16_t n);    // content moves up, lines vacated at bottom
    void scroll_down(uint16_t n);  // content moves down, vacated at top

    void erase_display(int mode);
    void erase_line(int mode);
    void clear_cells(uint16_t r0, uint16_t c0, uint16_t r1, uint16_t c1);

    void insert_lines(uint16_t n);
    void delete_lines(uint16_t n);
    void insert_chars(uint16_t n);
    void delete_chars(uint16_t n);
    void erase_chars(uint16_t n);

    void set_dec_modes(std::span<const int> params, bool set);

    void device_status_report(std::span<const int> params, bool private_marker);
    void respond(const char* s, size_t len);

    void enter_alt_screen();
    void exit_alt_screen();

    void apply_sgr(std::span<const int> params);

    int param_or(std::span<const int> p, int idx, int def) const;

    // When overwriting a wide-pair half, replace its orphaned partner with
    // `fill_cell` so the renderer never sees a dangling wide / wide_cont
    // flag.
    void clear_orphan_pair(uint16_t row, uint16_t col, const Cell& fill_cell);
    void capture_scrollback(uint16_t rows);

    uint16_t cols_;
    uint16_t rows_;
    std::vector<Cell> grid_;
    static constexpr size_t kMaximumScrollbackRows = 512;
    // One fixed circular allocation keeps the history in PSRAM on Tab5. A
    // vector-per-row would be smaller than ESP-IDF's external-RAM threshold
    // and could exhaust internal SRAM after a few hundred lines.
    std::vector<Cell> scrollback_;
    size_t scrollback_count_{0};
    size_t scrollback_head_{0};
    uint64_t scrollback_total_{0};
    uint16_t scrollback_capture_rows_{0}; // zero means the full grid height

    uint16_t cursor_row_{0};
    uint16_t cursor_col_{0};
    bool wrap_pending_{false};  // VT100 "last column" deferred wrap

    // VT100 G0/G1 character-set designation. Modern programs normally send
    // UTF-8 box drawing directly, while older curses applications use
    // ESC ( 0 / ESC ) 0 plus SI/SO to select DEC Special Graphics.
    Charset g0_charset_{Charset::ascii};
    Charset g1_charset_{Charset::ascii};
    bool g1_selected_{false};

    // DEC modes (DECSET/DECRST `?h` / `?l`).
    bool autowrap_{true};       // ?7 — wrap to next line at last column
    bool cursor_visible_{true}; // ?25 — DECTCEM cursor visibility

    // Scroll region: [scroll_top_, scroll_bot_) inclusive top, exclusive bot.
    uint16_t scroll_top_{0};
    uint16_t scroll_bot_{0};  // == rows_ when full screen

    // Saved state (DECSC / DECRC).
    uint16_t saved_row_{0};
    uint16_t saved_col_{0};
    Attrs saved_attrs_{};
    Color saved_fg_{Color::default_color()};
    Color saved_bg_{Color::default_color()};
    bool has_saved_{false};

    // Current SGR state.
    Attrs cur_attrs_{};
    Color cur_fg_{Color::default_color()};
    Color cur_bg_{Color::default_color()};

    DamageRect damage_{};
    bool bell_pending_{false};
    ResponseSink response_{};

    // Alt-screen support (DECSET ?1049 / ?1047 / ?47). Holds whichever
    // screen is currently inactive — swapped wholesale on transition.
    // autowrap_ and cursor_visible_ remain shared across both screens.
    struct ScreenBackup {
        std::vector<Cell> grid;
        uint16_t cursor_row{0};
        uint16_t cursor_col{0};
        bool wrap_pending{false};
        uint16_t scroll_top{0};
        uint16_t scroll_bot{0};
        Attrs cur_attrs{};
        Color cur_fg{Color::default_color()};
        Color cur_bg{Color::default_color()};
        uint16_t saved_row{0};
        uint16_t saved_col{0};
        Attrs saved_attrs{};
        Color saved_fg{Color::default_color()};
        Color saved_bg{Color::default_color()};
        bool has_saved{false};
    };
    ScreenBackup backup_{};
    bool alt_active_{false};

    void swap_with_backup();
};

}  // namespace term
