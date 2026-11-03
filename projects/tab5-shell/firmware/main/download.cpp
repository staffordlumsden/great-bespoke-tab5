#include "download.h"
#include <array>
#include <ctime>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
namespace tab5_shell {
bool https_download(Shell& shell,std::string_view url,std::string_view path,std::string& error) {
    std::string physical;
    if (!https_url_allowed(url) || !shell.file_path(path,true,physical,error)) {
        if (error.empty()) error="HTTPS URL rejected (no credentials, fragments or alternate ports)";
        return false;
    }
    if (!ensure_shell_wifi()) { error="Wi-Fi unavailable; configure saved network in Console"; return false; }
    if (std::time(nullptr)<1704067200) {
        esp_sntp_config_t config=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        auto init=esp_netif_sntp_init(&config);
        bool synced=init==ESP_OK && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000))==ESP_OK;
        if (init==ESP_OK) esp_netif_sntp_deinit();
        if (!synced) { error="Clock synchronization failed; TLS certificate dates remain enforced"; return false; }
    }
    const std::string address(url);
    esp_http_client_config_t config{};
    config.url=address.c_str(); config.timeout_ms=5000;
    config.crt_bundle_attach=esp_crt_bundle_attach;
    config.disable_auto_redirect=true;
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if (!client) { error="HTTPS client allocation failed"; return false; }
    bool ok=esp_http_client_open(client,0)==ESP_OK;
    auto length=ok?esp_http_client_fetch_headers(client):-1;
    ok=ok && length>=0 && length<=65536 && esp_http_client_get_status_code(client)==200;
    std::string contents;
    std::array<char,1024> buffer{};
    auto deadline=esp_timer_get_time()+15000000;
    while (ok && esp_timer_get_time()<deadline) {
        int n=esp_http_client_read(client,buffer.data(),buffer.size());
        if (n<0 || contents.size()+static_cast<std::size_t>(n)>65536) { ok=false; break; }
        if (n==0) { ok=esp_http_client_is_complete_data_received(client); break; }
        contents.append(buffer.data(),static_cast<std::size_t>(n));
    }
    ok=ok && esp_http_client_is_complete_data_received(client);
    esp_http_client_close(client); esp_http_client_cleanup(client);
    if (!ok) { error="HTTPS failed: TLS/status/redirect/size/deadline; destination preserved"; return false; }
    return shell.write_file(path,contents,error);
}
} // namespace tab5_shell
