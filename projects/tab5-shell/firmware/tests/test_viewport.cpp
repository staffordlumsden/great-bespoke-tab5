#include "document_mode.h"
#include "term_core/terminal.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <unistd.h>
using namespace tab5_shell;
struct FakeDisplay : term::IDisplay {
    unsigned draws=0,flushes=0;
    term::Result<void> init() override { return {}; }
    std::uint16_t cols() const override { return 80; }
    std::uint16_t rows() const override { return 24; }
    term::Result<void> draw_cells(std::uint16_t row,std::uint16_t col,std::span<const term::Cell> cells) override {
        assert(row<24 && col+cells.size()<=80); ++draws; return {};
    }
    term::Result<void> flush(term::DamageRect damage) override {
        assert(damage.row1<=24 && damage.col1<=80); ++flushes; return {};
    }
};
int main() {
    char dir[]="/tmp/tab5-viewport-XXXXXX"; std::filesystem::path root(mkdtemp(dir));
    Shell shell({(root/"home").string(),root.string(),true}); std::string error;
    assert(shell.prepare(error));
    assert(shell.write_file("long.txt",std::string(512,'x')+"\nend",error));
    DocumentMode document(shell); assert(document.open("long.txt",false,false,error));
    FakeDisplay display; term::Terminal terminal(80,24,display);
    assert(terminal.feed("/home $ ")); assert(terminal.feed("\x1b[?1049h\x1b[2J\x1b[H"));
    document.feed("\x05");
    auto rows=document.screen(80,24);
    assert(document.cursor_col()<80);
    for (std::size_t row=0;row<rows.size();++row) {
        assert(terminal.feed("\x1b["+std::to_string(row+1)+";1H\x1b[0m\x1b[2K"));
        assert(terminal.feed(rows[row].text));
    }
    assert(terminal.screen().at(0,1).ch==U'N');
    assert(terminal.screen().at(1,0).ch==U'x');
    assert(terminal.render_dirty());
    auto flushes=display.flushes; assert(terminal.render_dirty() && display.flushes==flushes);
    assert(terminal.feed("\x1b[?1049l"));
    assert(terminal.screen().at(0,0).ch==U'/');
    assert(terminal.screen().at(0,7).ch==U' ');
    assert(terminal.feed("\x1b[23;80H_"));
    assert(terminal.render_dirty());
    assert(terminal.feed("\x1b]0;unfinished remote OSC"));
    terminal=term::Terminal(80,24,display);
    assert(terminal.feed("/home $ "));
    assert(terminal.screen().at(0,0).ch==U'/');
    assert(terminal.screen().cursor_visible());
    std::filesystem::remove_all(root);
    std::cout<<"PASS document/real VT100 alternate-screen, long-line and damage integration ASan+UBSan\n";
}
