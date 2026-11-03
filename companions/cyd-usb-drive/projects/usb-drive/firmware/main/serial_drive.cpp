#include "serial_drive.h"
#include "drive_path.h"
#include "cJSON.h"
#include "driver/uart.h"
#include "ff.h"
#include "mbedtls/base64.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace usbdrive {
namespace {
constexpr size_t line_capacity = 6144;
constexpr size_t io_size = 2048;
char input[line_capacity];
uint8_t io[io_size];
char encoded[io_size * 4 / 3 + 16];
portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
Snapshot state{};
uint64_t last_link_ms = 0;
FIL upload{};
FIL download{};
bool upload_open = false;
uint32_t upload_token = 0;
uint32_t upload_expected = 0;
uint32_t upload_done = 0;
char upload_target[260]{};
char upload_temp[290]{};
char upload_backup[290]{};

uint64_t now_ms() { return uint64_t(xTaskGetTickCount()) * portTICK_PERIOD_MS; }
void set_state(const char *operation, const char *name, bool busy, bool writing,
               uint64_t done, uint64_t total, const char *message) {
    portENTER_CRITICAL(&state_lock);
    state.busy = busy; state.writing = writing; state.done = done; state.total = total;
    std::snprintf(state.operation, sizeof(state.operation), "%s", operation ? operation : "");
    std::snprintf(state.name, sizeof(state.name), "%s", name ? name : "");
    std::snprintf(state.message, sizeof(state.message), "%s", message ? message : "");
    portEXIT_CRITICAL(&state_lock);
}
void linked() {
    portENTER_CRITICAL(&state_lock);
    state.linked = true;
    last_link_ms = now_ms();
    portEXIT_CRITICAL(&state_lock);
}
void storage() {
    FATFS *fs = nullptr; DWORD clusters = 0;
    if (f_getfree("0:", &clusters, &fs) != FR_OK || !fs) return;
    const uint64_t unit = uint64_t(fs->csize) * FF_MIN_SS;
    portENTER_CRITICAL(&state_lock);
    state.card_free = uint64_t(clusters) * unit;
    state.card_total = uint64_t(fs->n_fatent - 2) * unit;
    portEXIT_CRITICAL(&state_lock);
}
bool line(uint64_t deadline) {
    size_t used = 0;
    while (now_ms() < deadline) {
        uint8_t ch = 0;
        const int count = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(20));
        if (count != 1) continue;
        if (ch == '\n') {
            while (used && input[used - 1] == '\r') --used;
            input[used] = 0;
            return used != 0;
        }
        if (ch && ch != 127 && used + 1 < sizeof(input)) input[used++] = char(ch);
        else if (used + 1 >= sizeof(input)) used = 0;
    }
    return false;
}
void transmit(cJSON *response) {
    char *text = cJSON_PrintUnformatted(response);
    if (text) {
        uart_write_bytes(UART_NUM_0, "UD1 ", 4);
        uart_write_bytes(UART_NUM_0, text, std::strlen(text));
        uart_write_bytes(UART_NUM_0, "\n", 1);
        cJSON_free(text);
    }
    cJSON_Delete(response);
}
cJSON *base(uint32_t sequence, bool ok) {
    auto *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "seq", sequence);
    cJSON_AddBoolToObject(result, "ok", ok);
    return result;
}
void fail(uint32_t sequence, const char *code, const char *message) {
    auto *result = base(sequence, false);
    cJSON_AddStringToObject(result, "code", code);
    cJSON_AddStringToObject(result, "message", message);
    transmit(result);
}
bool number(cJSON *root, const char *key, uint32_t &value) {
    auto *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > 4294967295.0) return false;
    value = uint32_t(item->valuedouble);
    return true;
}
const char *string(cJSON *root, const char *key) {
    auto *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}
void abort_upload(const char *message) {
    if (upload_open) { f_close(&upload); upload_open = false; }
    if (upload_temp[0]) f_unlink(upload_temp);
    upload_token = upload_expected = upload_done = 0;
    upload_target[0] = upload_temp[0] = upload_backup[0] = 0;
    set_state("Ready", "", false, false, 0, 0, message);
}
bool begin_upload(uint32_t sequence, const char *path, uint32_t size) {
    char target[260];
    if (!virtual_path(path, target, sizeof(target))) return false;
    FILINFO info{};
    if (f_stat(target, &info) == FR_OK && (info.fattrib & AM_DIR)) return false;
    abort_upload("");
    const char *slash = std::strrchr(target, '/');
    if (!slash) return false;
    const int directory = int(slash - target);
    std::snprintf(upload_temp, sizeof(upload_temp), "%.*s/.cyd-usb-upload.part", directory, target);
    std::snprintf(upload_backup, sizeof(upload_backup), "%.*s/.cyd-usb-backup.part", directory, target);
    f_unlink(upload_temp); f_unlink(upload_backup);
    if (f_open(&upload, upload_temp, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    upload_open = true; upload_token = sequence; upload_expected = size; upload_done = 0;
    std::snprintf(upload_target, sizeof(upload_target), "%s", target);
    set_state("Writing", display_name(path), true, true, 0, size, "Receiving from Finder");
    return true;
}
bool upload_chunk(uint32_t token, uint32_t offset, const char *data) {
    if (!upload_open || token != upload_token || offset != upload_done || !data) return false;
    size_t decoded = 0;
    if (mbedtls_base64_decode(io, sizeof(io), &decoded,
                              reinterpret_cast<const unsigned char *>(data), std::strlen(data)) != 0 ||
        !decoded || upload_done + decoded > upload_expected) return false;
    UINT written = 0;
    if (f_write(&upload, io, decoded, &written) != FR_OK || written != decoded) return false;
    upload_done += decoded;
    char current_name[sizeof(state.name)]{};
    portENTER_CRITICAL(&state_lock);
    std::snprintf(current_name, sizeof(current_name), "%s", state.name);
    portEXIT_CRITICAL(&state_lock);
    set_state("Writing", current_name, true, true, upload_done, upload_expected, "Receiving from Finder");
    return true;
}
bool commit_upload(uint32_t token) {
    if (!upload_open || token != upload_token || upload_done != upload_expected) return false;
    bool ok = f_sync(&upload) == FR_OK && f_close(&upload) == FR_OK;
    upload_open = false;
    bool had_existing = false;
    FILINFO info{};
    if (ok && f_stat(upload_target, &info) == FR_OK) {
        had_existing = true;
        ok = !(info.fattrib & AM_DIR) && f_rename(upload_target, upload_backup) == FR_OK;
    }
    if (ok) ok = f_rename(upload_temp, upload_target) == FR_OK;
    if (!ok && had_existing) f_rename(upload_backup, upload_target);
    if (ok && had_existing) f_unlink(upload_backup);
    if (!ok) f_unlink(upload_temp);
    const uint32_t completed = upload_done;
    upload_token = upload_expected = upload_done = 0;
    upload_target[0] = upload_temp[0] = upload_backup[0] = 0;
    storage();
    set_state(ok ? "Complete" : "Error", "", false, false, completed, completed,
              ok ? "Saved safely to SD card" : "Write could not be published");
    return ok;
}
bool remove_tree(const char *path, unsigned depth = 0) {
    if (depth > 16) return false;
    FILINFO info{};
    if (f_stat(path, &info) != FR_OK) return true;
    if (!(info.fattrib & AM_DIR)) return f_unlink(path) == FR_OK;
    FF_DIR directory{};
    if (f_opendir(&directory, path) != FR_OK) return false;
    bool ok = true;
    while (ok && f_readdir(&directory, &info) == FR_OK && info.fname[0]) {
        if (!std::strcmp(info.fname, ".") || !std::strcmp(info.fname, "..")) continue;
        char child[300];
        if (std::snprintf(child, sizeof(child), "%s/%s", path, info.fname) >= int(sizeof(child))) { ok = false; break; }
        ok = remove_tree(child, depth + 1);
    }
    f_closedir(&directory);
    return ok && f_unlink(path) == FR_OK;
}
void stat_command(uint32_t sequence, const char *path) {
    auto *result = base(sequence, true);
    if (!std::strcmp(path, "/") || !std::strcmp(path, "/Files") || !std::strcmp(path, "/Firmware")) {
        cJSON_AddStringToObject(result, "type", "dir"); cJSON_AddNumberToObject(result, "size", 0); transmit(result); return;
    }
    char fat[260]; FILINFO info{};
    if (!virtual_path(path, fat, sizeof(fat)) || f_stat(fat, &info) != FR_OK) { cJSON_Delete(result); fail(sequence, "not_found", "Item not found"); return; }
    cJSON_AddStringToObject(result, "type", (info.fattrib & AM_DIR) ? "dir" : "file");
    cJSON_AddNumberToObject(result, "size", info.fsize); transmit(result);
}
void list_command(uint32_t sequence, const char *path, uint32_t offset) {
    auto *result = base(sequence, true); auto *entries = cJSON_AddArrayToObject(result, "entries");
    if (!std::strcmp(path, "/")) {
        for (const char *name : {"Files", "Firmware"}) { auto *item = cJSON_CreateObject(); cJSON_AddStringToObject(item, "name", name); cJSON_AddStringToObject(item, "type", "dir"); cJSON_AddNumberToObject(item, "size", 0); cJSON_AddItemToArray(entries, item); }
        cJSON_AddBoolToObject(result, "more", false); transmit(result); return;
    }
    char fat[260];
    if (!virtual_path(path, fat, sizeof(fat), true)) { cJSON_Delete(result); fail(sequence, "forbidden", "Invalid shared path"); return; }
    FF_DIR directory{}; FILINFO info{};
    if (f_opendir(&directory, fat) != FR_OK) { cJSON_Delete(result); fail(sequence, "not_found", "Folder not found"); return; }
    unsigned skipped = 0, shown = 0; bool more = false;
    while (f_readdir(&directory, &info) == FR_OK && info.fname[0]) {
        if (info.fname[0] == '.' || (info.fattrib & AM_HID)) continue;
        if (skipped++ < offset) continue;
        if (shown == 32) { more = true; break; }
        auto *item = cJSON_CreateObject(); cJSON_AddStringToObject(item, "name", info.fname);
        cJSON_AddStringToObject(item, "type", (info.fattrib & AM_DIR) ? "dir" : "file");
        cJSON_AddNumberToObject(item, "size", info.fsize); cJSON_AddItemToArray(entries, item); ++shown;
    }
    f_closedir(&directory); cJSON_AddBoolToObject(result, "more", more); transmit(result);
}
void read_command(uint32_t sequence, const char *path, uint32_t offset, uint32_t length) {
    char fat[260];
    if (!path || !length || length > io_size || !virtual_path(path, fat, sizeof(fat))) {
        fail(sequence, "bad_request", "Invalid read request"); return;
    }
    if (f_open(&download, fat, FA_READ) != FR_OK) {
        fail(sequence, "not_found", "Cannot read file"); return;
    }
    if (f_lseek(&download, offset) != FR_OK) {
        f_close(&download); fail(sequence, "not_found", "Cannot read file"); return;
    }
    UINT count = 0;
    const bool ok = f_read(&download, io, length, &count) == FR_OK;
    f_close(&download);
    if (!ok) { fail(sequence, "io", "SD read failed"); return; }
    size_t output = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(encoded), sizeof(encoded), &output, io, count) != 0) {
        fail(sequence, "memory", "Encoding failed"); return;
    }
    encoded[output] = 0;
    set_state("Reading", display_name(path), false, false, offset + count, offset + count, "Sent to Finder");
    auto *result = base(sequence, true);
    cJSON_AddStringToObject(result, "data", encoded);
    cJSON_AddNumberToObject(result, "count", count);
    transmit(result);
}
void mutation_command(uint32_t sequence, const char *op, const char *path, cJSON *root) {
    char fat[260];
    if (!path || !virtual_path(path, fat, sizeof(fat))) {
        fail(sequence, "forbidden", "Invalid shared path"); return;
    }
    if (!std::strcmp(op, "mkdir")) {
        if (f_mkdir(fat) != FR_OK) { fail(sequence, "exists", "Folder could not be created"); return; }
        storage(); auto *result = base(sequence, true); transmit(result); return;
    }
    if (!std::strcmp(op, "remove")) {
        if (!remove_tree(fat)) { fail(sequence, "io", "Item could not be removed"); return; }
        storage(); auto *result = base(sequence, true); transmit(result); return;
    }
    if (!std::strcmp(op, "rename")) {
        const char *destination = string(root, "to"); char target[260];
        if (!destination || !virtual_path(destination, target, sizeof(target)) || f_rename(fat, target) != FR_OK) {
            fail(sequence, "io", "Item could not be renamed"); return;
        }
        auto *result = base(sequence, true); transmit(result); return;
    }
    fail(sequence, "bad_request", "Unsupported operation");
}
void process(cJSON *root) {
    uint32_t sequence = 0;
    if (!number(root, "seq", sequence)) return;
    const char *op = string(root, "op");
    if (!op) { fail(sequence, "bad_request", "Missing operation"); return; }
    linked();
    if (!std::strcmp(op, "ping")) {
        auto *result = base(sequence, true); cJSON_AddStringToObject(result, "protocol", "UD1"); cJSON_AddStringToObject(result, "version", "1"); transmit(result); return;
    }
    if (!std::strcmp(op, "capacity")) {
        storage(); auto snap = snapshot(); auto *result = base(sequence, true); cJSON_AddNumberToObject(result, "total", double(snap.card_total)); cJSON_AddNumberToObject(result, "free", double(snap.card_free)); transmit(result); return;
    }
    const char *path = string(root, "path");
    if ((!std::strcmp(op, "stat") || !std::strcmp(op, "list")) && !path) { fail(sequence, "bad_request", "Missing path"); return; }
    if (!std::strcmp(op, "stat")) { stat_command(sequence, path); return; }
    if (!std::strcmp(op, "list")) { uint32_t offset = 0; number(root, "offset", offset); list_command(sequence, path, offset); return; }
    if (!std::strcmp(op, "read")) {
        uint32_t offset = 0, length = 0;
        if (!number(root, "offset", offset) || !number(root, "length", length)) {
            fail(sequence, "bad_request", "Invalid read request"); return;
        }
        read_command(sequence, path, offset, length); return;
    }
    if (!std::strcmp(op, "write_begin")) {
        uint32_t size = 0; if (!path || !number(root, "size", size) || !begin_upload(sequence, path, size)) { fail(sequence, "io", "Cannot begin safe write"); return; }
        auto *result = base(sequence, true); cJSON_AddNumberToObject(result, "token", sequence); transmit(result); return;
    }
    if (!std::strcmp(op, "write_data")) {
        uint32_t token = 0, offset = 0; const char *data = string(root, "data");
        if (!number(root, "token", token) || !number(root, "offset", offset) || !upload_chunk(token, offset, data)) { abort_upload("Write cancelled after invalid data"); fail(sequence, "io", "Write chunk rejected"); return; }
        auto *result = base(sequence, true); cJSON_AddNumberToObject(result, "written", upload_done); transmit(result); return;
    }
    if (!std::strcmp(op, "write_commit")) {
        uint32_t token = 0; if (!number(root, "token", token) || !commit_upload(token)) { abort_upload("Write could not be committed"); fail(sequence, "io", "Safe write commit failed"); return; }
        auto *result = base(sequence, true); transmit(result); return;
    }
    if (!std::strcmp(op, "write_abort")) { abort_upload("Write cancelled by host"); auto *result = base(sequence, true); transmit(result); return; }
    mutation_command(sequence, op, path, root);
}
void worker(void *) {
    storage();
    portENTER_CRITICAL(&state_lock); state.ready = true; portEXIT_CRITICAL(&state_lock);
    uart_write_bytes(UART_NUM_0, "UD1 READY 1\n", 12);
    for (;;) {
        if (!line(now_ms() + 1000)) {
            const uint64_t now = now_ms();
            portENTER_CRITICAL(&state_lock);
            if (state.linked && now - last_link_ms > 3000) {
                state.linked = false;
                std::snprintf(state.message, sizeof(state.message), "Host disconnected safely");
            }
            portEXIT_CRITICAL(&state_lock);
            continue;
        }
        if (std::strncmp(input, "UD1 ", 4)) continue;
        cJSON *root = cJSON_ParseWithLength(input + 4, std::strlen(input + 4));
        if (!root) continue;
        process(root); cJSON_Delete(root);
    }
}
}

bool serial_start() {
    const esp_err_t installed = uart_driver_install(UART_NUM_0, 8192, 8192, 0, nullptr, 0);
    if (installed != ESP_OK && installed != ESP_ERR_INVALID_STATE) return false;
    if (uart_set_baudrate(UART_NUM_0, 115200) != ESP_OK) return false;
    return xTaskCreatePinnedToCore(worker, "usb_drive", 12288, nullptr, 4, nullptr, 0) == pdPASS;
}
Snapshot snapshot() { portENTER_CRITICAL(&state_lock); auto value = state; portEXIT_CRITICAL(&state_lock); return value; }
void cancel_transfer() { abort_upload("Transfer stopped on CYD"); }
void host_disconnect() {
    abort_upload("Host disconnected safely");
    portENTER_CRITICAL(&state_lock);
    state.linked = false;
    portEXIT_CRITICAL(&state_lock);
    uart_write_bytes(UART_NUM_0, "UD1 {\"event\":\"disconnect\"}\n", 27);
}
}
