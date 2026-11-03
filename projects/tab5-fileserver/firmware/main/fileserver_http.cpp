#include "fileserver_http.hpp"
#include "fileserver_net.hpp"
#include "fonts/web_pixel_fonts.h"
#include "fs_core.hpp"
#include "tab5_hal.h"
#include "web_assets.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace tab5_fs {

namespace {

constexpr size_t kStreamChunkSize = 16384;
constexpr size_t kMaxListEntries = 256;

SemaphoreHandle_t s_http_lock = nullptr;
httpd_handle_t s_server = nullptr;
HttpStats s_stats{};
char s_sd_root[64] = "/sdcard";
char s_passcode[9] = "12345678";

void* alloc_buf(size_t size) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = std::malloc(size);
    return p;
}

void lock_http() {
    if (s_http_lock) xSemaphoreTake(s_http_lock, portMAX_DELAY);
}

void unlock_http() {
    if (s_http_lock) xSemaphoreGive(s_http_lock);
}

void record_event(const char* evt, uint64_t bytes = 0, int kind = 0) {
    lock_http();
    ++s_stats.requests;
    if (kind == 1) ++s_stats.downloads;
    else if (kind == 2) ++s_stats.html_streams;
    else if (kind == 3) ++s_stats.uploads;
    s_stats.bytes_transferred += bytes;
    if (evt && evt[0]) {
        std::snprintf(s_stats.last_event, sizeof(s_stats.last_event), "%s", evt);
    }
    ++s_stats.revision;
    unlock_http();
}

void get_active_passcode(char out[9]) {
    lock_http();
    std::snprintf(out, 9, "%s", s_passcode);
    unlock_http();
}

bool check_request_auth(httpd_req_t* req) {
    char active[9];
    get_active_passcode(active);

    char candidate[32] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Tab5-Passcode", candidate, sizeof(candidate)) == ESP_OK) {
        if (verify_passcode(candidate, active)) return true;
    }

    char cookie_hdr[256] = {0};
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_hdr, sizeof(cookie_hdr)) == ESP_OK) {
        if (extract_cookie_value(cookie_hdr, "tab5_passcode", candidate, sizeof(candidate))) {
            if (verify_passcode(candidate, active)) return true;
        }
    }

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (get_query_param(query, "code", candidate, sizeof(candidate))) {
            if (verify_passcode(candidate, active)) return true;
        }
    }
    return false;
}

esp_err_t send_json_error(httpd_req_t* req, const char* status, const char* message) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    char body[192];
    std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message ? message : "ERROR");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

void json_escape_into(const char* src, char* dst, size_t dst_cap) {
    if (!dst || dst_cap == 0) return;
    size_t w = 0;
    for (size_t i = 0; src && src[i] != '\0'; ++i) {
        const char c = src[i];
        if (c == '"' || c == '\\') {
            if (w + 2 >= dst_cap) break;
            dst[w++] = '\\';
            dst[w++] = c;
        } else if (static_cast<unsigned char>(c) >= 32) {
            if (w + 1 >= dst_cap) break;
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
}

esp_err_t handle_index(httpd_req_t* req) {
    record_event("OPEN WEB FILE EXPLORER");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, tab5_fs_web::kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_font(httpd_req_t* req) {
    httpd_resp_set_type(req, "font/ttf");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    if (std::strstr(req->uri, "pixelfont13.ttf")) {
        return httpd_resp_send(req, reinterpret_cast<const char*>(tab5_fs_fonts::kPixelFont13Ttf),
                               tab5_fs_fonts::kPixelFont13TtfSize);
    }
    if (std::strstr(req->uri, "pixelfont7.ttf")) {
        return httpd_resp_send(req, reinterpret_cast<const char*>(tab5_fs_fonts::kPixelFont7Ttf),
                               tab5_fs_fonts::kPixelFont7TtfSize);
    }
    return httpd_resp_send(req, reinterpret_cast<const char*>(tab5_fs_fonts::kPixelFont11Ttf),
                           tab5_fs_fonts::kPixelFont11TtfSize);
}

esp_err_t handle_api_auth(httpd_req_t* req) {
    if (!check_request_auth(req)) {
        record_event("WEB LOGIN REJECTED (BAD CODE)");
        return send_json_error(req, "401 Unauthorized", "INVALID 8-DIGIT PASSCODE");
    }
    char active[9];
    get_active_passcode(active);

    char cookie_hdr[96];
    std::snprintf(cookie_hdr, sizeof(cookie_hdr),
                  "tab5_passcode=%s; Path=/; SameSite=Lax; HttpOnly", active);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    record_event("WEB EXPLORER UNLOCKED");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_api_logout(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Set-Cookie", "tab5_passcode=; Path=/; Max-Age=0; SameSite=Lax; HttpOnly");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    record_event("WEB EXPLORER LOCKED");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_api_status(httpd_req_t* req) {
    const bool authed = check_request_auth(req);
    uint64_t sd_total = 0, sd_free = 0;
    tab5_sd_usage(&sd_total, &sd_free);

    NetStatus net{};
    net_get_status(&net);

    char resp[320];
    std::snprintf(
        resp, sizeof(resp),
        "{\"ok\":true,\"authenticated\":%s,\"mode\":\"%s\",\"ip\":\"%s\","
        "\"sd_total\":%llu,\"sd_free\":%llu}",
        authed ? "true" : "false",
        net.mode == NetMode::hotspot ? "HOTSPOT" : "LAN",
        net.ip_address,
        static_cast<unsigned long long>(sd_total),
        static_cast<unsigned long long>(sd_free));

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_api_list(httpd_req_t* req) {
    if (!check_request_auth(req)) {
        return send_json_error(req, "401 Unauthorized", "8-DIGIT PASSCODE REQUIRED");
    }

    char query[256] = {0};
    char req_path[192] = "/";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        get_query_param(query, "path", req_path, sizeof(req_path));
    }

    char vfs_path[256];
    char clean_rel[192];
    if (!resolve_sd_path(s_sd_root, req_path, vfs_path, sizeof(vfs_path), clean_rel, sizeof(clean_rel))) {
        return send_json_error(req, "400 Bad Request", "INVALID SD PATH");
    }

    auto* entries = static_cast<DirEntry*>(alloc_buf(sizeof(DirEntry) * kMaxListEntries));
    if (!entries) {
        return send_json_error(req, "500 Internal Server Error", "OUT OF MEMORY");
    }

    size_t count = 0, total_dirs = 0, total_files = 0;
    if (!list_directory(vfs_path, entries, kMaxListEntries, count, total_dirs, total_files)) {
        std::free(entries);
        return send_json_error(req, "404 Not Found", "DIRECTORY NOT FOUND ON SD");
    }

    char evt[72];
    std::snprintf(evt, sizeof(evt), "LIST %.38s (%u ITEMS)", clean_rel, static_cast<unsigned>(count));
    record_event(evt);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char chunk[384];
    char escaped[192];
    json_escape_into(clean_rel, escaped, sizeof(escaped));
    std::snprintf(chunk, sizeof(chunk), "{\"ok\":true,\"path\":\"%s\",\"entries\":[", escaped);
    httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);

    for (size_t i = 0; i < count; ++i) {
        json_escape_into(entries[i].name, escaped, sizeof(escaped));
        std::snprintf(
            chunk, sizeof(chunk),
            "%s{\"name\":\"%s\",\"is_dir\":%s,\"is_html\":%s,\"size\":%llu,\"mtime\":%lld}",
            (i > 0) ? "," : "",
            escaped,
            entries[i].is_dir ? "true" : "false",
            entries[i].is_html ? "true" : "false",
            static_cast<unsigned long long>(entries[i].size),
            static_cast<long long>(entries[i].mtime));
        httpd_resp_send_chunk(req, chunk, HTTPD_RESP_USE_STRLEN);
    }

    std::free(entries);
    httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_api_mkdir(httpd_req_t* req) {
    if (!check_request_auth(req)) {
        return send_json_error(req, "401 Unauthorized", "8-DIGIT PASSCODE REQUIRED");
    }

    char query[256] = {0};
    char dir_path[192] = "/";
    char folder_name[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        get_query_param(query, "path", dir_path, sizeof(dir_path));
        get_query_param(query, "name", folder_name, sizeof(folder_name));
    }

    char vfs_target[256];
    if (!join_sd_child_path(s_sd_root, dir_path, folder_name, vfs_target, sizeof(vfs_target))) {
        return send_json_error(req, "400 Bad Request", "INVALID FOLDER NAME");
    }

    if (mkdir(vfs_target, 0775) != 0) {
        return send_json_error(req, "409 Conflict", "COULD NOT CREATE FOLDER");
    }

    char evt[72];
    std::snprintf(evt, sizeof(evt), "MKDIR %.48s", folder_name);
    record_event(evt);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_api_upload(httpd_req_t* req) {
    if (!check_request_auth(req)) {
        return send_json_error(req, "401 Unauthorized", "8-DIGIT PASSCODE REQUIRED");
    }

    char query[256] = {0};
    char dir_path[192] = "/";
    char file_name[128] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        get_query_param(query, "path", dir_path, sizeof(dir_path));
        get_query_param(query, "name", file_name, sizeof(file_name));
    }

    char vfs_target[256];
    if (!join_sd_child_path(s_sd_root, dir_path, file_name, vfs_target, sizeof(vfs_target))) {
        return send_json_error(req, "400 Bad Request", "INVALID UPLOAD FILENAME");
    }

    auto* buf = static_cast<char*>(alloc_buf(kStreamChunkSize));
    if (!buf) {
        return send_json_error(req, "500 Internal Server Error", "OUT OF MEMORY");
    }

    FILE* fp = std::fopen(vfs_target, "wb");
    if (!fp) {
        std::free(buf);
        return send_json_error(req, "500 Internal Server Error", "CANNOT OPEN SD FILE FOR WRITE");
    }

    size_t remaining = req->content_len;
    uint64_t written_total = 0;
    bool write_ok = true;

    while (remaining > 0) {
        const size_t to_read = (remaining < kStreamChunkSize) ? remaining : kStreamChunkSize;
        const int r = httpd_req_recv(req, buf, to_read);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            write_ok = false;
            break;
        }
        const size_t w = std::fwrite(buf, 1, static_cast<size_t>(r), fp);
        if (w != static_cast<size_t>(r)) {
            write_ok = false;
            break;
        }
        remaining -= static_cast<size_t>(r);
        written_total += w;
    }

    std::fflush(fp);
    fsync(fileno(fp));
    std::fclose(fp);
    std::free(buf);

    if (!write_ok) {
        unlink(vfs_target);
        return send_json_error(req, "500 Internal Server Error", "SD WRITE INTERRUPTED");
    }

    char size_str[24];
    format_bytes(written_total, size_str, sizeof(size_str));
    char evt[72];
    std::snprintf(evt, sizeof(evt), "UPLOAD %.32s (%.20s)", file_name, size_str);
    record_event(evt, written_total, 3);

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_fs_file(httpd_req_t* req) {
    if (!check_request_auth(req)) {
        return send_json_error(req, "401 Unauthorized", "8-DIGIT PASSCODE REQUIRED");
    }

    // Extract URI path after "/fs" and before '?'
    const char* uri = req->uri;
    if (std::strncmp(uri, "/fs", 3) != 0) {
        return send_json_error(req, "400 Bad Request", "INVALID FILE URI");
    }
    const char* raw_sub = uri + 3;
    const char* qmark = std::strchr(raw_sub, '?');
    const size_t raw_len = qmark ? static_cast<size_t>(qmark - raw_sub) : std::strlen(raw_sub);

    char encoded_rel[256];
    if (raw_len == 0) {
        std::snprintf(encoded_rel, sizeof(encoded_rel), "/");
    } else if (raw_len < sizeof(encoded_rel)) {
        std::memcpy(encoded_rel, raw_sub, raw_len);
        encoded_rel[raw_len] = '\0';
    } else {
        return send_json_error(req, "414 URI Too Long", "PATH TOO LONG");
    }

    char decoded_rel[256];
    if (!url_decode(encoded_rel, decoded_rel, sizeof(decoded_rel))) {
        return send_json_error(req, "400 Bad Request", "MALFORMED URL ENCODING");
    }

    char vfs_path[256];
    char clean_rel[256];
    if (!resolve_sd_path(s_sd_root, decoded_rel, vfs_path, sizeof(vfs_path), clean_rel, sizeof(clean_rel))) {
        return send_json_error(req, "403 Forbidden", "PATH ACCESS DENIED");
    }

    struct stat st{};
    if (stat(vfs_path, &st) != 0 || S_ISDIR(st.st_mode)) {
        return send_json_error(req, "404 Not Found", "FILE NOT FOUND ON SD");
    }

    const char* base = std::strrchr(clean_rel, '/');
    const char* filename = (base && base[1]) ? (base + 1) : clean_rel;

    bool force_dl = false;
    char query[128] = {0};
    char dl_param[8] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (get_query_param(query, "dl", dl_param, sizeof(dl_param)) && dl_param[0] == '1') {
            force_dl = true;
        }
    }

    const DeliveryMode mode = force_dl ? DeliveryMode::download_attachment
                                       : classify_file_delivery(filename);
    const char* mime = mime_type_for_filename(filename);
    httpd_resp_set_type(req, mime);

    char disp_hdr[300];
    if (mode == DeliveryMode::stream_html) {
        httpd_resp_set_hdr(req, "Content-Disposition", "inline");
    } else {
        std::snprintf(disp_hdr, sizeof(disp_hdr), "attachment; filename=\"%.255s\"", filename);
        httpd_resp_set_hdr(req, "Content-Disposition", disp_hdr);
    }

    FILE* fp = std::fopen(vfs_path, "rb");
    if (!fp) {
        return send_json_error(req, "500 Internal Server Error", "COULD NOT OPEN FILE");
    }

    auto* buf = static_cast<char*>(alloc_buf(kStreamChunkSize));
    if (!buf) {
        std::fclose(fp);
        return send_json_error(req, "500 Internal Server Error", "OUT OF MEMORY");
    }

    uint64_t total_sent = 0;
    size_t nread = 0;
    while ((nread = std::fread(buf, 1, kStreamChunkSize, fp)) > 0) {
        if (httpd_resp_send_chunk(req, buf, nread) != ESP_OK) {
            break;
        }
        total_sent += nread;
    }

    std::free(buf);
    std::fclose(fp);
    httpd_resp_send_chunk(req, nullptr, 0);

    char evt[72];
    if (mode == DeliveryMode::stream_html) {
        std::snprintf(evt, sizeof(evt), "STREAM HTML %.48s", filename);
        record_event(evt, total_sent, 2);
    } else {
        std::snprintf(evt, sizeof(evt), "DOWNLOAD %.50s", filename);
        record_event(evt, total_sent, 1);
    }
    return ESP_OK;
}

}  // namespace

esp_err_t http_server_start(const char* sd_root, const char initial_passcode[9]) {
    if (s_server) return ESP_OK;
    if (!s_http_lock) {
        s_http_lock = xSemaphoreCreateMutex();
        if (!s_http_lock) return ESP_ERR_NO_MEM;
    }

    lock_http();
    if (sd_root && sd_root[0]) {
        std::snprintf(s_sd_root, sizeof(s_sd_root), "%s", sd_root);
    }
    if (initial_passcode && std::strlen(initial_passcode) == 8) {
        std::snprintf(s_passcode, sizeof(s_passcode), "%s", initial_passcode);
    }
    std::snprintf(s_stats.last_event, sizeof(s_stats.last_event), "HTTP SERVER READY ON PORT 80");
    unlock_http();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 12288;
    config.max_uri_handlers = 12;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, handle_index, nullptr},
        {"/index.html", HTTP_GET, handle_index, nullptr},
        {"/fonts/*", HTTP_GET, handle_font, nullptr},
        {"/api/auth", HTTP_POST, handle_api_auth, nullptr},
        {"/api/logout", HTTP_POST, handle_api_logout, nullptr},
        {"/api/status", HTTP_GET, handle_api_status, nullptr},
        {"/api/list", HTTP_GET, handle_api_list, nullptr},
        {"/api/mkdir", HTTP_POST, handle_api_mkdir, nullptr},
        {"/api/upload", HTTP_POST, handle_api_upload, nullptr},
        {"/fs/*", HTTP_GET, handle_fs_file, nullptr},
    };
    for (const auto& r : routes) {
        httpd_register_uri_handler(s_server, &r);
    }

    lock_http();
    s_stats.running = true;
    ++s_stats.revision;
    unlock_http();
    return ESP_OK;
}

void http_server_set_passcode(const char new_passcode[9]) {
    if (!new_passcode || std::strlen(new_passcode) != 8) return;
    lock_http();
    std::snprintf(s_passcode, sizeof(s_passcode), "%s", new_passcode);
    std::snprintf(s_stats.last_event, sizeof(s_stats.last_event), "PASSCODE ROTATED — SESSIONS RESET");
    ++s_stats.revision;
    unlock_http();
}

void http_server_get_stats(HttpStats* out) {
    if (!out) return;
    lock_http();
    *out = s_stats;
    unlock_http();
}

}  // namespace tab5_fs
