#include "cursor_renderer.h"

#include <span>

namespace tab5_shell {

CursorRenderer::CursorRenderer(const term::Screen& screen, term::IDisplay& display)
    : screen_(screen), display_(display) {}

void CursorRenderer::erase() {
    if (!drawn_) return;
    if (drawn_row_ >= screen_.rows() || drawn_col_ >= screen_.cols()) {
        drawn_ = false;
        return;
    }
    const term::Cell cell = screen_.at(drawn_row_, drawn_col_);
    const std::uint16_t cols = cell.attrs.wide ? 2 : 1;
    (void)display_.draw_cells(drawn_row_, drawn_col_,
                              std::span<const term::Cell>(&cell, 1));
    (void)display_.flush({drawn_row_, drawn_col_,
                          static_cast<std::uint16_t>(drawn_row_ + 1),
                          static_cast<std::uint16_t>(drawn_col_ + cols)});
    drawn_ = false;
}

void CursorRenderer::draw() {
    const std::uint16_t row = screen_.cursor_row();
    const std::uint16_t col = screen_.cursor_col();
    if (row >= screen_.rows() || col >= screen_.cols()) return;
    if (drawn_) erase();
    if (!screen_.cursor_visible()) return;

    term::Cell cell = screen_.at(row, col);
    cell.attrs.reverse = !cell.attrs.reverse;
    const std::uint16_t cols = cell.attrs.wide ? 2 : 1;
    (void)display_.draw_cells(row, col, std::span<const term::Cell>(&cell, 1));
    (void)display_.flush({row, col, static_cast<std::uint16_t>(row + 1),
                          static_cast<std::uint16_t>(col + cols)});
    drawn_ = true;
    drawn_row_ = row;
    drawn_col_ = col;
}

void CursorRenderer::sync_after_render() {
    drawn_ = false;
    draw();
}

void CursorRenderer::toggle_blink() {
    if (drawn_)
        erase();
    else if (screen_.cursor_visible())
        draw();
}

}  // namespace tab5_shell
