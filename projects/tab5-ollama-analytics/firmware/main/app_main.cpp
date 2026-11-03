#include "ollama_analytics.h"
#include "ollama_http.h"
#include "ui.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tab5_app_boot.h"
#include "tab5_hal.h"
#include "tab5_weather_wifi.h"
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace {
oa::Config config;
oa::View view;
ou::PollPolicy poll;
QueueHandle_t jobs, results;
nvs_handle_t settings;
bool nvs_ready = false, settings_ready = false;
uint64_t milliseconds() { return static_cast<uint64_t>(esp_timer_get_time()/1000); }
void status(const char *message) { std::snprintf(view.status,sizeof(view.status),"%s",message); }
void fetch_worker(void *) {
    bool request;
    static oa::FetchResult result;
    for (;;) if (xQueueReceive(jobs,&request,portMAX_DELAY)==pdTRUE) {
        oa::fetch_ollama(config,result);
        xQueueSend(results,&result,portMAX_DELAY);
    }
}
bool start_worker() {
    jobs=xQueueCreate(1,sizeof(bool));
    results=xQueueCreate(1,sizeof(oa::FetchResult));
    return jobs&&results&&xTaskCreate(fetch_worker,"ollama_fetch",8192,nullptr,4,nullptr)==pdPASS;
}
void draw_rect(void *context,int x,int y,int w,int h,uint32_t colour) {
    auto &ok=*static_cast<bool *>(context);
    if(tab5_display_rect(x,y,w,h,colour)!=ESP_OK)ok=false;
}
bool repaint(unsigned regions) {
    if(!regions)return true;
    bool ok=true;
    oa::draw(view,{&ok,draw_rect},regions);
    if(!ok)return false;
    if(regions&oa::full)return tab5_display_present()==ESP_OK;
    if((regions&oa::footer)&&(regions&(oa::ollama|oa::quota)))
        return tab5_display_present_region(0,150,1280,570)==ESP_OK;
    if(regions&oa::ollama)return tab5_display_present_region(24,150,1232,478)==ESP_OK;
    if(regions&oa::quota)return tab5_display_present_region(792,150,464,250)==ESP_OK;
    return tab5_display_present_region(0,646,1280,74)==ESP_OK;
}
void return_to_console() {
    tab5_sd_unmount();
    if(tab5_app_boot_return_to_console()!=ESP_OK) {
        status("Console return unavailable / press reset");
        repaint(oa::footer);
    }
}
struct Touch {
    bool armed=false,down=false,cancelled=false;
    unsigned releases=0;
    oa::Action start=oa::Action::none;
    oa::Action sample(bool pressed,int x,int y) {
        if(!armed) {
            releases=pressed?0:releases+1;
            if(releases>=3){armed=true;releases=0;}
            return oa::Action::none;
        }
        if(pressed) {
            releases=0;
            const auto action=oa::hit(x,y);
            if(!down){down=true;cancelled=false;start=action;}
            else if(action!=start)cancelled=true;
        } else if(down&&++releases>=3) {
            down=false;releases=0;
            return cancelled?oa::Action::none:start;
        }
        return oa::Action::none;
    }
};
} // namespace

extern "C" void app_main() {
    nvs_ready=nvs_flash_init()==ESP_OK; // Never erase shared NVS on an error.
    const auto board=tab5_board_init();
    // Retain the original namespace so the user's theme survives the rename.
    if(nvs_ready&&nvs_open("ai_usage",NVS_READWRITE,&settings)==ESP_OK) {
        settings_ready=true;
        uint8_t light=0;
        if(nvs_get_u8(settings,"light",&light)==ESP_OK)view.dark=!light;
    }
    if(board.sd_ready)oa::load_config(config);
    view.configured=config.ready;
    const bool worker=start_worker();
    const bool healthy=nvs_ready&&board.display_ready&&board.touch_ready&&board.sd_ready&&
        worker&&tab5_app_boot_console_available();
    if(config.ready)status("Connecting to saved Wi-Fi");
    if(!healthy)status("Startup issue / returning to Console");
    const bool first_frame=repaint(oa::full);
    bool confirmed=healthy&&first_frame&&tab5_app_boot_ready(true)==ESP_OK;
    const uint64_t boot_deadline=milliseconds()+15000;
    std::printf("TAB5_OLLAMA_BOOT healthy=%d first_frame=%d confirmed=%d credentials=WITHHELD\n",
                healthy,first_frame,confirmed);

    // The C6 must be powered before any hosted station RPC.
    bool network=false;
    if(tab5_radio_power_set(true)==ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250));
        network=tab5_weather_wifi_init()==ESP_OK&&tab5_weather_wifi_start()==ESP_OK;
    }
    bool sntp=false;
    bool models_failed=false,balance_failed=false;
    uint64_t models_at=0,balance_at=0;
    int64_t minute=-1;
    Touch touch;
    static oa::FetchResult result;
    for(;;) {
        const uint64_t now=milliseconds();
        unsigned dirty=0;
        if(!confirmed&&now>=boot_deadline){return_to_console();esp_restart();}
        tab5_weather_wifi_status_t station{};
        if(network)tab5_weather_wifi_get_status(&station);
        const bool online=network&&station.has_ip;
        const bool clock_ready=std::time(nullptr)>=1704067200;
        view.now=clock_ready?std::time(nullptr):0;
        if(view.now/60!=minute) {
            minute=view.now/60;
            if(view.has_balance)dirty|=oa::quota;
        }
        if(view.wifi!=online||view.clock_ready!=clock_ready)dirty|=oa::footer;
        view.wifi=online;view.clock_ready=clock_ready;
        if(online&&!sntp) {
            esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
                2,ESP_SNTP_SERVER_LIST("time.google.com","pool.ntp.org"));
            sntp=esp_netif_sntp_init(&cfg)==ESP_OK;
        }
        if(worker&&xQueueReceive(results,&result,0)==pdTRUE) {
            view.busy=false;
            if(result.models_meta.ok&&!oa::models_fresh(result.models,std::time(nullptr))) {
                result.models_meta.ok=false;result.models_meta.error=oa::HttpError::json;
            }
            models_failed=!result.models_meta.ok;balance_failed=!result.balance_meta.ok;
            if(result.models_meta.ok){view.models=result.models;view.has_models=true;models_at=now;}
            if(result.balance_meta.ok){view.balance=result.balance;view.has_balance=true;balance_at=now;}
            const bool ok=result.models_meta.ok&&result.balance_meta.ok;
            const int failure=result.balance_meta.ok?result.models_meta.status:result.balance_meta.status;
            poll.finish(now,ok,failure,std::max(result.models_meta.retry_after,result.balance_meta.retry_after));
            status(ok?"Ollama updated directly over HTTPS":poll.blocked?"Ollama sign-in failed / check your SD key":"Fetch failed / previous values may be stale");
            dirty|=oa::ollama|oa::footer;
            std::printf("TAB5_OLLAMA_FETCH balance_ok=%d models_ok=%d account_values=WITHHELD\n",
                        result.balance_meta.ok,result.models_meta.ok);
        }
        const bool model_stale=view.has_models&&(models_failed||!online||now-models_at>180000);
        const bool balance_stale=view.has_balance&&(balance_failed||!online||now-balance_at>180000);
        if(view.models_stale!=model_stale||view.balance_stale!=balance_stale)dirty|=oa::ollama;
        view.models_stale=model_stale;view.balance_stale=balance_stale;
        int x=0,y=0;
        const bool pressed=tab5_touch_poll(&x,&y);
        switch(touch.sample(pressed,x,y)) {
        case oa::Action::console:return_to_console();dirty|=oa::footer;break;
        case oa::Action::theme:
            view.dark=!view.dark;
            if(settings_ready){nvs_set_u8(settings,"light",!view.dark);nvs_commit(settings);}
            dirty|=oa::full;break;
        case oa::Action::refresh:
            if(!config.ready)status("Add Ollama key to SD, then relaunch");
            else if(!online)status("Set up Internet Wi-Fi in Console first");
            else if(!clock_ready)status("Waiting for secure network time");
            else if(!view.busy&&!poll.manual(now))status("Please wait for the refresh cooldown");
            dirty|=oa::footer;break;
        case oa::Action::none:break;
        }
        if(worker&&config.ready&&online&&clock_ready&&!view.busy&&poll.ready(now)) {
            const bool request=true;
            if(xQueueSend(jobs,&request,0)==pdTRUE) {
                view.busy=true;poll.begin(now);status("Updating Ollama directly over HTTPS");dirty|=oa::footer;
            }
        }
        if(dirty&&!repaint(dirty))status("Display update failed / return to Console");
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
