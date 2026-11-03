#include "shell_display.h"
#include "tab5_hal.h"
#include "term_core/terminal.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned invalid_rects=0, presents=0;
std::string first_invalid;
bool fail_present=false;
void require(bool result,const std::string& reason) {
    if (!result) throw std::runtime_error(reason+"; first HAL rejection="+first_invalid);
}
bool bounds(int x,int y,int width,int height) {
    return x>=0 && y>=0 && width>=0 && height>=0 && x<=1280 && y<=720 &&
           width<=1280-x && height<=720-y;
}
}
esp_err_t tab5_display_set_rotation(tab5_display_rotation_t) { return ESP_OK; }
esp_err_t tab5_display_rect(int x,int y,int width,int height,std::uint32_t) {
    // Same rejection contract as the real ST7121 HAL: do not silently clip.
    if (bounds(x,y,width,height)) return ESP_OK;
    ++invalid_rects;
    if (first_invalid.empty()) first_invalid="x="+std::to_string(x)+" y="+std::to_string(y)+
        " w="+std::to_string(width)+" h="+std::to_string(height);
    return ESP_ERR_INVALID_ARG;
}
esp_err_t tab5_display_present_region(int x,int y,int width,int height) {
    require(bounds(x,y,width,height),"present bounds");
    ++presents; return fail_present?ESP_ERR_TIMEOUT:ESP_OK;
}
esp_err_t tab5_display_present() { return tab5_display_present_region(0,0,1280,720); }
int main() {
    try {
        for (int variant=0;variant<3;++variant) {
            tab5_shell::ShellDisplay display;
            tab5_shell::ShellStatus status;
            status.sd_ready=variant!=1; status.home_ready=variant==0;
            status.tab5_keyboard_ready=true; status.usb_keyboard_ready=true;
            status.message="READY / LOCAL";
            invalid_rects=presents=0; first_invalid.clear();
            require(display.init().has_value(),"init");
            require(display.draw_chrome(status,false),"initial chrome failed, variant="+std::to_string(variant));
            require(presents==0,"scanout revealed before first complete frame");
            term::Terminal terminal(80,24,display);
            require(terminal.feed("TAB5 Shell - local P4 shell\r\n/home $ ").has_value(),"feed");
            require(terminal.render_dirty().has_value(),"first terminal frame failed");
            require(presents==0,"terminal revealed before present_all");
            require(display.present_all(),"first present failed");
            require(presents==1 && invalid_rects==0,"startup HAL bounds");
            status.message="Console return failed: ESP_ERR_INVALID_STATE";
            require(display.draw_status(status),"status update");
            status.scroll_offset=512; status.scrollback_rows=512;
            require(display.draw_status(status),"history indicator/navigation bounds");
            const std::string long_status(500, 'X');
            status.scroll_offset=0; status.message=long_status.c_str();
            require(display.draw_status(status),"long status/navigation bounds");
            tab5_shell::TouchKeyboard keyboard; keyboard.visible=true;
            require(display.draw_touch_keyboard(keyboard),"keyboard bounds");
            require(invalid_rects==0,"post-start bounds");
            fail_present=true;
            require(!display.present_all(),"present failure falsely reports success");
            fail_present=false;
        }
        std::cout<<"PASS real ShellDisplay/fonts first-frame/HAL bounds/hidden-until-complete/presentation failure ASan+UBSan\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL startup display: "<<error.what()<<"\n"; return 1;
    }
}
