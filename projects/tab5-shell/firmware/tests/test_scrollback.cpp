#include "scrollback.h"
#include "shell_core.h"
#include "term_core/terminal.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <unistd.h>

using namespace tab5_shell;
struct Display : term::IDisplay {
    unsigned flushes=0;
    std::vector<term::Cell> cells=std::vector<term::Cell>(80*24);
    term::Result<void> init() override { return {}; }
    std::uint16_t cols() const override { return 80; }
    std::uint16_t rows() const override { return 24; }
    term::Result<void> draw_cells(std::uint16_t row,std::uint16_t col,std::span<const term::Cell> values) override {
        assert(row<24 && col+values.size()<=80);
        std::copy(values.begin(),values.end(),cells.begin()+row*80+col); return {};
    }
    term::Result<void> flush(term::DamageRect r) override {
        assert(r.row1<=24 && r.col1<=80); ++flushes; return {};
    }
    std::string row(std::size_t row) const {
        std::string value;
        for(std::size_t i=0;i<80;++i)value+=static_cast<char>(cells[row*80+i].ch);
        return value;
    }
};
void line(term::Terminal& terminal, int n) {
    char value[64]; std::snprintf(value,sizeof(value),"line-%04d\r\n",n);
    assert(terminal.feed(value));
}
int main() {
    for (auto mods : {0x02,0x20}) {
        assert(scroll_key(0x4b,mods)==ScrollAction::older);
        assert(scroll_key(0x4e,mods)==ScrollAction::newer);
        assert(scroll_key(0x4a,mods)==ScrollAction::oldest);
        assert(scroll_key(0x4d,mods)==ScrollAction::live);
    }
    assert(scroll_key(0x4b,0)==ScrollAction::none);
    assert(scroll_key(0x52,2)==ScrollAction::none); // Up remains normal readline
    assert(scroll_key(0x4b,3)==ScrollAction::none); // Ctrl+Shift passes through
    assert(scroll_button(650,700)==ScrollAction::older);
    assert(scroll_button(750,700)==ScrollAction::newer);
    assert(scroll_button(850,700)==ScrollAction::live);
    assert(scroll_button(950,700)==ScrollAction::none); // KEYS owns its button
    assert(scroll_swipe(200,250,205,400,684)==ScrollAction::older);
    assert(scroll_swipe(200,400,205,250,684)==ScrollAction::newer);
    assert(scroll_swipe(200,400,400,410,684)==ScrollAction::none);
    assert(scroll_swipe(200,10,200,300,684)==ScrollAction::none);
    assert(scroll_swipe(200,500,200,600,414)==ScrollAction::none);

    char path[]="/tmp/tab5-shell-scrollback-XXXXXX";
    std::filesystem::path root(mkdtemp(path));
    Shell shell({(root/"home").string(),root.string(),true});
    std::string error,document;
    assert(shell.prepare(error));
    for(int i=0;i<200;++i) { char row[32]; std::snprintf(row,sizeof(row),"response-%04d\n",i); document+=row; }
    assert(shell.write_file("large-response.txt",document,error));
    Display display; term::Terminal terminal(80,24,display); Scrollback history;
    assert(shell.execute("cat large-response.txt",[&](std::string_view text) {
        for(char c:text) { if(c=='\n')assert(terminal.feed("\r"));assert(terminal.feed(std::string_view(&c,1))); }
    })==0);
    assert(terminal.feed("/home $ ")); assert(terminal.render_dirty());
    assert(display.row(0).find("response-0000")==std::string::npos);
    assert(terminal.screen().scrollback_rows()>0);
    assert(history.move(ScrollAction::older,terminal.screen(),22));
    assert(history.draw(terminal.screen(),display,24));
    assert(history.move(ScrollAction::oldest,terminal.screen(),22));
    assert(history.draw(terminal.screen(),display,24));
    assert(display.row(0).starts_with("response-0000"));
    assert(history.move(ScrollAction::live,terminal.screen(),22));
    assert(history.draw(terminal.screen(),display,24));
    assert(display.row(23).starts_with("/home $ "));
    std::filesystem::remove_all(root);

    Display streaming_display; term::Terminal stream(80,24,streaming_display); Scrollback view;
    for(int i=0;i<800;++i)line(stream,i);
    assert(stream.screen().scrollback_rows()==512);
    assert(view.move(ScrollAction::older,stream.screen(),100));
    assert(view.draw(stream.screen(),streaming_display,24));
    const auto anchor=streaming_display.row(0);
    const auto draws=streaming_display.flushes;
    for(int i=800;i<830;++i)line(stream,i);
    assert(!view.observe(stream.screen())); // keep displayed history frozen
    assert(view.offset()==130 && streaming_display.flushes==draws);
    assert(view.draw(stream.screen(),streaming_display,24));
    assert(streaming_display.row(0)==anchor); // stable even at ring saturation
    assert(view.move(ScrollAction::oldest,stream.screen(),1));
    for(int i=830;i<850;++i)line(stream,i);
    assert(view.observe(stream.screen()) && view.offset()==512); // oldest expired
    assert(view.draw(stream.screen(),streaming_display,24));
    const auto captured=stream.screen().scrollback_total_rows();
    assert(stream.feed("\x1b[?1049h"));
    assert(view.observe(stream.screen()) && !view.active());
    for(int i=0;i<40;++i)line(stream,i);
    assert(stream.screen().scrollback_total_rows()==captured); // TUI owns its view
    assert(!view.move(ScrollAction::older,stream.screen(),20));
    assert(stream.feed("\x1b[?1049l"));
    assert(view.move(ScrollAction::oldest,stream.screen(),1));
    stream.screen().clear_scrollback();
    assert(view.observe(stream.screen()) && !view.active());

    Display local_display; term::Terminal local(80,24,local_display); Scrollback local_history;
    assert(local.feed("\x1b[1;14r"));
    for(int i=0;i<30;++i)line(local,i);
    assert(local.screen().scrollback_rows()==0); // partial TUI regions still excluded
    local.screen().set_scrollback_capture_rows(14);
    for(int i=30;i<100;++i)line(local,i);
    assert(local.screen().scrollback_rows()==70);
    assert(local_history.move(ScrollAction::oldest,local.screen(),12));
    assert(local_history.draw(local.screen(),local_display,14));
    assert(local_display.row(0).starts_with("line-0017"));
    local.screen().set_scrollback_capture_rows(24);
    assert(local.feed("\x1b[r\x1b[24;1H"));line(local,100);
    assert(local.screen().scrollback_rows()==71);
    local_history.reset(); assert(!local_history.active());
    std::cout<<"PASS Shell response/streaming history, 512-row wrap, viewport, keys/touch and cropped console ASan+UBSan\n";
    std::cout<<"View scratch bytes="<<sizeof(term::Cell)*80*24<<"\n";
}
