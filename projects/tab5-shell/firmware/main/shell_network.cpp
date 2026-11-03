#include "shell_network.h"
#include "sdkconfig.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include "esp_hosted_api.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "tab5_hal.h"

#if !defined(CONFIG_SLAVE_IDF_TARGET_ESP32C6) || !CONFIG_SLAVE_IDF_TARGET_ESP32C6
#error "Tab5 Shell requires the factory ESP32-C6 companion target"
#endif
static_assert(std::string_view(CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET) == "esp32c6",
              "ESP-Hosted transport must expect ESP32-C6");
static_assert(std::string_view(CONFIG_IDF_SLAVE_TARGET) == "esp32c6",
              "Wi-Fi Remote must target ESP32-C6");

namespace tab5_shell {
namespace {
bool wifi_initialized=false;
bool wifi_init_failed=false;
esp_netif_t* station=nullptr;
std::atomic<bool> wifi_started{false};
bool private_read(const char* path, std::string& value) {
    FILE* f=fopen(path,"rb"); if (!f) return false;
    std::array<char,512> bytes{}; value.clear(); bool ok=true;
    for (;;) {
        auto n=fread(bytes.data(),1,bytes.size(),f);
        if (value.size()+n>16384) { ok=false; break; }
        value.append(bytes.data(),n);
        if (n<bytes.size()) { ok=!ferror(f); break; }
    }
    fclose(f); return ok;
}
bool start_wifi() {
    if (wifi_init_failed) return false;
    if (!wifi_initialized) {
        wifi_init_failed=true; // partial hosted initialization requires a restart
        if (tab5_radio_power_set(true)!=ESP_OK) return false;
        vTaskDelay(pdMS_TO_TICKS(250));
        // This HAL has no tab5_radio_init symbol. esp_wifi_init is the hosted
        // C6 initialization boundary; it is reached only after power succeeds.
        auto result=esp_netif_init();
        if (result!=ESP_OK && result!=ESP_ERR_INVALID_STATE) return false;
        result=esp_event_loop_create_default();
        if (result!=ESP_OK && result!=ESP_ERR_INVALID_STATE) return false;
        station=esp_netif_create_default_wifi_sta(); if (!station) return false;
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&init)!=ESP_OK) return false;
        // Select station mode without persisting mode/config changes, then read
        // the saved configuration loaded by esp_wifi_init.
        wifi_config_t saved{};
        if (esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK ||
            esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK) return false;
        bool saved_ok=esp_wifi_get_config(WIFI_IF_STA,&saved)==ESP_OK;
        esp_hosted_coprocessor_fwver_t version{};
        if (esp_hosted_get_coprocessor_fwversion(&version)!=ESP_OK || version.major1!=1 || version.minor1!=4) return false;
        if (!saved_ok || !saved.sta.ssid[0] || esp_wifi_set_config(WIFI_IF_STA,&saved)!=ESP_OK) return false;
        volatile auto* wipe=reinterpret_cast<volatile unsigned char*>(&saved);
        for (std::size_t i=0;i<sizeof(saved);++i) wipe[i]=0;
        if (esp_wifi_start()!=ESP_OK) return false;
        wifi_initialized=true; wifi_started=true; wifi_init_failed=false;
    }
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap)!=ESP_OK) (void)esp_wifi_connect();
    const auto deadline=esp_timer_get_time()+15000000;
    while (esp_timer_get_time()<deadline) {
        esp_netif_ip_info_t ip{};
        if (esp_netif_get_ip_info(station,&ip)==ESP_OK && ip.ip.addr) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}
}
bool ensure_shell_wifi() { return start_wifi(); }

bool verify_host_key(const char* host, std::uint16_t port, const std::uint8_t* fingerprint) {
    std::string pins;
    if (!private_read("/sdcard/private/tab5-shell-known-hosts.txt",pins)) return false;
    std::array<std::uint8_t,32> fp{}; std::copy_n(fingerprint,32,fp.begin());
    SshRequest request; request.host=host; request.port=port;
    return known_host_matches(pins,request,fp);
}
bool ShellNetwork::start(SshRequest request, std::string password, std::string& error) {
    if (busy_.load()) { error="SSH transport busy"; return false; }
    if (!request.profile.empty()) {
        std::string profiles;
        if (!private_read("/sdcard/private/tab5-shell-profiles.txt",profiles) ||
            !select_profile(profiles,request.profile,request,error)) return false;
    }
    if (!rx_) rx_=xStreamBufferCreate(8192,1);
    if (!rx_) { error="SSH receive allocation failed"; return false; }
    xStreamBufferReset(rx_);
    request_=std::move(request); password_=std::move(password);
    stop_=false; busy_=true; result_="CONNECTING";
    if (xTaskCreate(task,"shell_network",12288,this,4,nullptr)!=pdPASS) {
        busy_=false; error="SSH worker allocation failed"; return false;
    }
    return true;
}
void ShellNetwork::task(void* context) {
    auto& self=*static_cast<ShellNetwork*>(context);
    if (!start_wifi()) self.result_="Wi-Fi unavailable; configure saved network in Console";
    else if (!self.stop_) {
        const auto& r=self.request_;
        tab5::SshConfig config{r.host.c_str(),r.port,r.user.c_str(),self.password_.c_str(),
            r.key.c_str(),"",80,24,8192};
        self.connection_=std::make_unique<tab5::SshConnection>(config);
        bool up=self.connection_->start([&self](std::span<const std::uint8_t> bytes) {
            std::size_t at=0;
            while (at<bytes.size() && !self.stop_) {
                auto n=xStreamBufferSend(self.rx_,bytes.data()+at,bytes.size()-at,pdMS_TO_TICKS(50));
                at+=n;
            }
        });
        volatile char* secret=self.password_.data();
        for (std::size_t i=0;i<self.password_.size();++i) secret[i]=0;
        self.password_.clear();
        self.connected_=up;
        self.result_=up ? "SSH CONNECTED (^] disconnects)" : tab5::ssh_failure_label(tab5::ssh_last_failure());
        while (up && self.connection_->is_connected() && !self.stop_) vTaskDelay(pdMS_TO_TICKS(20));
        {
            std::lock_guard lock(self.transport_mutex_);
            self.connected_=false;
        }
        self.connection_->stop();
        while (!tab5::ssh_transport_idle()) vTaskDelay(pdMS_TO_TICKS(20));
        {
            std::lock_guard lock(self.transport_mutex_);
            self.connection_.reset();
        }
        if (up) self.result_="SSH disconnected";
    }
    volatile char* p=self.password_.data();
    for (std::size_t i=0;i<self.password_.size();++i) p[i]=0;
    self.password_.clear(); self.busy_=false; vTaskDelete(nullptr);
}
void ShellNetwork::stop() { stop_=true; }
void ShellNetwork::send(std::span<const std::uint8_t> bytes) {
    // Only the UI calls send. The worker waits for its producer to leave before
    // destroying the connection (UI observes busy until teardown completes).
    std::lock_guard lock(transport_mutex_);
    if (connected_ && connection_) connection_->send(bytes);
}
std::size_t ShellNetwork::receive(std::uint8_t* bytes,std::size_t capacity) {
    return rx_ ? xStreamBufferReceive(rx_,bytes,capacity,0) : 0;
}
std::string ShellNetwork::wifi_status() {
    if (!wifi_started) return "Wi-Fi NOT STARTED (SSH joins saved network)";
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap)!=ESP_OK) return "Wi-Fi disconnected";
    return "Wi-Fi connected RSSI="+std::to_string(ap.rssi)+" dBm";
}
} // namespace tab5_shell
