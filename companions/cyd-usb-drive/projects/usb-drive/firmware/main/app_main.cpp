#include "console_bridge.h"
#include "cyd_hal.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_drive.h"
#include "ui.h"
#include <cstdio>
#include <cstring>

namespace {
usbdrive::View view{};
bool nvs_ok=false;
void theme(bool save) {
    nvs_handle_t handle{};uint8_t value=0;
    if(nvs_ok&&nvs_open("usbdrive",save?NVS_READWRITE:NVS_READONLY,&handle)==ESP_OK){
        if(save){nvs_set_u8(handle,"light",view.light);nvs_commit(handle);}else{nvs_get_u8(handle,"light",&value);view.light=value!=0;}nvs_close(handle);
    }
}
}
extern "C" void app_main() {
    esp_log_level_set("*",ESP_LOG_NONE);
    nvs_ok=nvs_flash_init()==ESP_OK;
    const auto board=cyd::hal_init();
    theme(false);
    const bool healthy=nvs_ok&&board.display==ESP_OK&&board.touch==ESP_OK&&board.sd==ESP_OK&&board.fat32;
    myapp::confirm_first_boot(healthy);
    view.console=myapp::console_available();
    const bool serial=healthy&&usbdrive::serial_start();
    std::printf("USB_DRIVE boot healthy=%d serial=%d protocol=UD1 accepted=NO\n",healthy,serial);
    bool pressed=false,touch_armed=false,redraw=true;
    unsigned idle_release_samples=0,released=0,touch_samples=0;
    int tx=0,ty=0;uint64_t last_touch_ms=0;usbdrive::Snapshot previous{};
    for(;;){
        auto current=usbdrive::snapshot();
        const bool changed=std::memcmp(&current,&previous,sizeof(current))!=0;
        if(changed){previous=current;view.drive=current;redraw=true;}
        int x=0,y=0;const bool down=board.touch==ESP_OK&&cyd::hal_touch_poll(x,y);
        const uint64_t now=cyd::hal_uptime_ms();
        if(!touch_armed){
            if(down)idle_release_samples=0;
            else if(++idle_release_samples>=10){touch_armed=true;released=0;}
        }else if(down){
            if(!pressed){tx=x;ty=y;pressed=true;touch_samples=1;}
            else{tx=(3*tx+x)/4;ty=(3*ty+y)/4;if(touch_samples<16)++touch_samples;}
            last_touch_ms=now;released=0;
        }
        else if(pressed&&now-last_touch_ms>=45&&++released>=3){pressed=false;
            const auto action=touch_samples>=2?usbdrive::hit(tx,ty):usbdrive::Action::none;
            std::printf("USB_DRIVE_TOUCH x=%d y=%d samples=%u action=%u\n",tx,ty,touch_samples,unsigned(action));std::fflush(stdout);
            switch(action){
            case usbdrive::Action::theme:view.light=!view.light;theme(true);redraw=true;break;
            case usbdrive::Action::stop:if(view.drive.busy)usbdrive::cancel_transfer();else usbdrive::host_disconnect();redraw=true;break;
            case usbdrive::Action::console:
                usbdrive::cancel_transfer();cyd::hal_sd_unmount();
                if(view.console)myapp::return_to_console();
                break;
            default:break;
            }
        }
        if(board.display==ESP_OK&&redraw&&!pressed){view.drive=usbdrive::snapshot();cyd::hal_render(usbdrive::pixel,&view);redraw=false;}
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
