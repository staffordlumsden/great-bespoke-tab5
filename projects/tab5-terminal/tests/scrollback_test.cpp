#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#include "term_core/parser.hpp"
#include "term_core/screen.hpp"

namespace {

void fail(const char* message) {
    std::fprintf(stderr, "scrollback_test: %s\n", message);
    std::exit(1);
}

void expect(bool condition, const char* message) {
    if (!condition) fail(message);
}

void feed(term::Parser& parser, term::Screen& screen, std::string_view text) {
    parser.feed(std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t*>(text.data()), text.size()),
                screen);
}

char32_t at(const std::vector<term::Cell>& cells, size_t row, size_t columns) {
    return cells[row * columns].ch;
}

}  // namespace

int main() {
    term::Parser parser;
    term::Screen screen(4, 3);
    feed(parser, screen, "A\r\nB\r\nC\r\nD");
    expect(screen.scrollback_rows() == 1, "full-screen LF did not save one row");

    std::vector<term::Cell> view(12);
    expect(screen.copy_view(0, view), "live view copy failed");
    expect(at(view, 0, 4) == U'B' && at(view, 1, 4) == U'C' &&
               at(view, 2, 4) == U'D',
           "live view has unexpected rows");
    expect(screen.copy_view(1, view), "history view copy failed");
    expect(at(view, 0, 4) == U'A' && at(view, 1, 4) == U'B' &&
               at(view, 2, 4) == U'C',
           "one-row scrollback view is incorrect");

    const size_t primary_history = screen.scrollback_rows();
    feed(parser, screen, "\x1b[?1049hX\r\nY\r\nZ\r\nW");
    expect(screen.alt_active(), "alternate screen did not activate");
    expect(screen.scrollback_rows() == primary_history,
           "alternate-screen output polluted shell history");
    feed(parser, screen, "\x1b[?1049l");
    expect(!screen.alt_active(), "alternate screen did not restore primary");

    term::Parser region_parser;
    term::Screen region(4, 4);
    feed(region_parser, region, "\x1b[2;3r\x1b[2;1H1\r\n2\r\n3");
    expect(region.scrollback_rows() == 0,
           "partial DEC scroll region polluted shell history");

    screen.resize(2, 2);
    view.resize(4);
    expect(screen.copy_view(1, view), "resized history view copy failed");
    expect(at(view, 0, 2) == U'A', "history row was lost during zoom resize");
    std::vector<term::Cell> too_small(3);
    expect(!screen.copy_view(0, too_small), "undersized destination was accepted");

    term::Parser bounded_parser;
    term::Screen bounded(2, 2);
    for (int line = 0; line < 700; ++line) {
        const char record[] = {static_cast<char>('A' + line % 26), '\r', '\n'};
        feed(bounded_parser, bounded, std::string_view(record, sizeof(record)));
    }
    expect(bounded.scrollback_rows() == 512, "scrollback is not bounded at 512 rows");
    view.resize(4);
    expect(bounded.copy_view(512, view), "oldest ring-buffer view copy failed");
    expect(at(view, 0, 2) == U'F' && at(view, 1, 2) == U'G',
           "ring buffer did not retain history in logical order");

    bounded.clear_scrollback();
    expect(bounded.scrollback_rows() == 0, "clear_scrollback failed");

    term::Parser glyph_parser;
    term::Screen glyphs(20, 2);
    feed(glyph_parser, glyphs, "-_|");
    expect(glyphs.at(0, 0).ch == U'-' && glyphs.at(0, 1).ch == U'_' &&
               glyphs.at(0, 2).ch == U'|',
           "literal ASCII punctuation changed in the parser/screen path");
    feed(glyph_parser, glyphs, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x90");
    expect(glyphs.at(0, 3).ch == U'┌' && glyphs.at(0, 4).ch == U'─' &&
               glyphs.at(0, 5).ch == U'┐',
           "UTF-8 box-drawing codepoints were not preserved");

    term::Parser dec_parser;
    term::Screen dec(20, 2);
    feed(dec_parser, dec, "\x1b(0lqk\x1b(B-_|");
    expect(dec.at(0, 0).ch == U'┌' && dec.at(0, 1).ch == U'─' &&
               dec.at(0, 2).ch == U'┐',
           "DEC G0 Special Graphics line drawing was not translated");
    expect(dec.at(0, 3).ch == U'-' && dec.at(0, 4).ch == U'_' &&
               dec.at(0, 5).ch == U'|',
           "ASCII was not restored after ESC ( B");

    term::Parser g1_parser;
    term::Screen g1(20, 2);
    feed(g1_parser, g1, "\x1b)0\x0elqk\x0f-_|");
    expect(g1.at(0, 0).ch == U'┌' && g1.at(0, 1).ch == U'─' &&
               g1.at(0, 2).ch == U'┐',
           "DEC G1 Special Graphics SO selection was not translated");
    expect(g1.at(0, 3).ch == U'-' && g1.at(0, 4).ch == U'_' &&
               g1.at(0, 5).ch == U'|',
           "SI did not restore the G0 ASCII character set");
    std::puts("TAB5_TERMINAL_SCROLLBACK PASS");
    return 0;
}
