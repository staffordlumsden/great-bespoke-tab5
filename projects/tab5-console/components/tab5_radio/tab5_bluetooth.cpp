// Hosted transport adaptation of Contextual Micro's accepted Winmaxle B1 path.
// The descriptor validator and keyboard decoder in cyd_keyboard/ are unchanged.
#include "tab5_bluetooth.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "esp_bt_main.h"
#include "esp_bluedroid_hci.h"
#include "esp_gap_ble_api.h"
#include "esp_hidh.h"
#include "esp_hidh_bluedroid.h"
extern "C" {
#include "esp_hosted_bt.h"
}
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "keyboard_mapping.h"
#include "keyboard_report.h"

namespace {
constexpr const char *TAG = "TAB5_BLE";
constexpr uint16_t NO_CONNECTION = 0xffff;
SemaphoreHandle_t mutex, scan_stopped;
QueueHandle_t work, closed_devices, reports, bytes;
tab5_radio_status_t state{};
tab5_ble_device_t devices[TAB5_RADIO_MAX_BLE_DEVICES]{};
tab5_ble_device_t selected{};
bool initialized, permitted, scan_active;
esp_hidh_dev_t *device;
struct Allowed { uint8_t map; uint16_t id; } allowed[8]{};
size_t allowed_count;
struct Report { uint32_t generation; uint8_t data[8]; };
std::atomic<uint32_t> generation{0};
cm::BootKeyboard decoder; // Owned exclusively by the UI task's input poll.

void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mutex); }
void message(const char *value) {
    std::snprintf(state.bt_message, sizeof(state.bt_message), "%s", value);
    ++state.revision;
}
bool matches(const uint8_t *addr) {
    lock();
    bool yes = permitted && state.bluetooth_enabled && !std::memcmp(selected.addr, addr, 6);
    unlock();
    return yes;
}
bool has_bond(const uint8_t *addr) {
    int count = esp_ble_get_bond_device_num();
    if (count < 1 || count > 16) return false;
    esp_ble_bond_dev_t bonds[16]{};
    if (esp_ble_get_bond_device_list(&count, bonds) != ESP_OK) return false;
    for (int i = 0; i < count; ++i)
        if (!std::memcmp(bonds[i].bd_addr, addr, 6)) return true;
    return false;
}
void ready_locked() {
    if (!device || !allowed_count || !state.ble_encrypted || !state.ble_bonded) return;
    state.ble_keyboard_ready = true;
    state.ble_connecting = false;
    state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
    message("PAIRED + TRUSTED - KEYBOARD READY");
    ESP_LOGI(TAG, "Keyboard ready: encrypted, bonded, validated report layout");
}
bool inspect_reports(esp_hidh_dev_t *dev) {
    size_t count = 0, map_count = 0, usable = 0;
    esp_hid_report_item_t *items = nullptr;
    esp_hid_raw_report_map_t *maps = nullptr;
    Allowed accepted[8]{};
    if (esp_hidh_dev_reports_get(dev, &count, &items) != ESP_OK) return false;
    bool got_maps = esp_hidh_dev_report_maps_get(dev, &map_count, &maps) == ESP_OK;
    for (size_t i = 0; i < count; ++i) {
        const auto &r = items[i];
        if (r.usage != ESP_HID_USAGE_KEYBOARD || r.report_type != ESP_HID_REPORT_TYPE_INPUT)
            continue;
        bool canonical = r.protocol_mode == ESP_HID_PROTOCOL_MODE_REPORT && got_maps &&
            r.map_index < map_count && r.report_id <= 255 &&
            cm::canonical_keyboard_report(maps[r.map_index].data, maps[r.map_index].len,
                                          static_cast<uint8_t>(r.report_id));
        ESP_LOGI(TAG, "Keyboard report map=%u id=%u metadata_bytes=%u canonical=%d",
                 r.map_index, r.report_id, r.value_len, canonical);
        if (canonical && usable < 8) accepted[usable++] = {r.map_index, r.report_id};
    }
    std::free(items);
    lock();
    allowed_count = usable;
    std::memcpy(allowed, accepted, sizeof(allowed));
    unlock();
    return usable != 0;
}
void scan_finished() {
    lock();
    scan_active = false;
    state.bt_scanning = false;
    if (!state.ble_connecting && state.bluetooth_enabled) {
        char msg[TAB5_RADIO_MESSAGE_LEN];
        std::snprintf(msg, sizeof(msg), "SCAN COMPLETE - %u DEVICES", (unsigned)state.ble_count);
        message(msg);
    } else ++state.revision;
    unlock();
    xSemaphoreGive(scan_stopped);
}
void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        esp_err_t err = p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS
            ? esp_ble_gap_start_scanning(10) : ESP_FAIL;
        if (err != ESP_OK) {
            scan_finished();
            lock(); message("SCAN COULD NOT START"); unlock();
        }
    } else if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT && p->scan_start_cmpl.status) {
        scan_finished();
        lock(); message("SCAN COULD NOT START"); unlock();
    } else if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
        scan_finished();
    } else if (event == ESP_GAP_BLE_SCAN_RESULT_EVT) {
        auto &r = p->scan_rst;
        if (r.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT) { scan_finished(); return; }
        if (r.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;
        uint8_t length = 0;
        uint8_t *name = esp_ble_resolve_adv_data_by_type(r.ble_adv,
            r.adv_data_len + r.scan_rsp_len, ESP_BLE_AD_TYPE_NAME_CMPL, &length);
        if (!name) name = esp_ble_resolve_adv_data_by_type(r.ble_adv,
            r.adv_data_len + r.scan_rsp_len, ESP_BLE_AD_TYPE_NAME_SHORT, &length);
        lock();
        if (!state.bt_scanning) { unlock(); return; }
        size_t index = 0;
        for (; index < state.ble_count; ++index)
            if (devices[index].addr_type == r.ble_addr_type &&
                !std::memcmp(devices[index].addr, r.bda, 6)) break;
        // Prefer a newly named device to an anonymous beacon in a crowded scan.
        if (index == TAB5_RADIO_MAX_BLE_DEVICES && name && length) {
            for (size_t i = 0; i < state.ble_count; ++i)
                if (!devices[i].name[0]) { index = i; devices[i] = {}; break; }
        }
        if (index < TAB5_RADIO_MAX_BLE_DEVICES) {
            if (index == state.ble_count) ++state.ble_count;
            auto &d = devices[index];
            std::memcpy(d.addr, r.bda, 6); d.addr_type = r.ble_addr_type; d.rssi = r.rssi;
            // A scan response must never erase an earlier connectable indication.
            d.connectable |= r.ble_evt_type == ESP_BLE_EVT_CONN_ADV ||
                             r.ble_evt_type == ESP_BLE_EVT_CONN_DIR_ADV;
            if (name && length) {
                size_t n = length < sizeof(d.name) - 1 ? length : sizeof(d.name) - 1;
                for (size_t i = 0; i < n; ++i)
                    d.name[i] = name[i] >= 32 && name[i] <= 126 ? name[i] : '?';
                d.name[n] = 0;
            }
            ++state.revision; // UI suppresses advertisement/RSSI-only redraws.
        }
        unlock();
    } else if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
        esp_ble_gap_security_rsp(p->ble_security.ble_req.bd_addr,
                                 matches(p->ble_security.ble_req.bd_addr));
    } else if (event == ESP_GAP_BLE_AUTH_CMPL_EVT) {
        auto &a = p->ble_security.auth_cmpl;
        if (!matches(a.bd_addr)) return;
        bool bonded = a.success && has_bond(a.bd_addr);
        ESP_LOGI(TAG, "Authentication success=%d mode=0x%02x reason=%u bonded=%d",
                 a.success, a.auth_mode, a.fail_reason, bonded);
        lock();
        state.ble_encrypted = a.success;
        state.ble_bonded = bonded;
        state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
        if (a.success) { message("PAIRED - OPENING KEYBOARD"); ready_locked(); }
        else {
            state.ble_connecting = false;
            state.ble_keyboard_ready = false;
            char msg[TAB5_RADIO_MESSAGE_LEN];
            std::snprintf(msg, sizeof(msg), "PAIRING FAILED - REASON %u", a.fail_reason);
            message(msg);
        }
        unlock();
        if (!a.success) esp_ble_gap_disconnect(a.bd_addr);
    } else if (event == ESP_GAP_BLE_PASSKEY_NOTIF_EVT || event == ESP_GAP_BLE_NC_REQ_EVT) {
        auto &k = p->ble_security.key_notif;
        if (!matches(k.bd_addr)) {
            if (event == ESP_GAP_BLE_NC_REQ_EVT) esp_ble_confirm_reply(k.bd_addr, false);
            return;
        }
        lock();
        state.ble_passkey = k.passkey;
        state.ble_pairing_mode = event == ESP_GAP_BLE_NC_REQ_EVT
            ? TAB5_BLE_PAIRING_CONFIRM : TAB5_BLE_PAIRING_DISPLAY;
        message(event == ESP_GAP_BLE_NC_REQ_EVT ? "CONFIRM THE DISPLAYED NUMBER"
                                             : "TYPE THE PASSKEY ON KEYBOARD + ENTER");
        unlock();
    } else if (event == ESP_GAP_BLE_PASSKEY_REQ_EVT && matches(p->ble_security.ble_req.bd_addr)) {
        lock(); state.ble_pairing_mode = TAB5_BLE_PAIRING_INPUT;
        message("ENTER THE DEVICE PAIRING PASSKEY"); unlock();
    }
}
void gatt_callback(esp_gattc_cb_event_t event, esp_gatt_if_t iface, esp_ble_gattc_cb_param_t *p) {
    if (event == ESP_GATTC_OPEN_EVT)
        ESP_LOGI(TAG, "GATT open status=%u", p->open.status);
    if (event == ESP_GATTC_DISCONNECT_EVT)
        ESP_LOGI(TAG, "GATT disconnect reason=%u", p->disconnect.reason);
    esp_hidh_gattc_event_handler(event, iface, p);
}
void hid_callback(void *, esp_event_base_t, int32_t id, void *data) {
    auto *p = static_cast<esp_hidh_event_data_t *>(data);
    if (id == ESP_HIDH_OPEN_EVENT) {
        auto *dev = p->open.dev;
        const uint8_t *addr = esp_hidh_dev_bda_get(dev);
        if (p->open.status || !addr || !matches(addr)) {
            if (dev) esp_hidh_dev_close(dev);
            return;
        }
        bool valid = inspect_reports(dev);
        lock();
        device = dev; state.ble_connected = true;
        const char *name = esp_hidh_dev_name_get(dev);
        if (name && *name) std::snprintf(state.ble_peer_name, sizeof(state.ble_peer_name), "%s", name);
        message(valid ? "KEYBOARD FOUND - SECURING LINK" : "UNSUPPORTED KEYBOARD REPORT LAYOUT");
        ready_locked();
        unlock();
        if (valid) {
            esp_err_t err = esp_ble_set_encryption(const_cast<uint8_t *>(addr), ESP_BLE_SEC_ENCRYPT_MITM);
            if (err != ESP_OK) {
                lock(); message("KEYBOARD ENCRYPTION COULD NOT START"); unlock();
                esp_hidh_dev_close(dev);
            }
        } else esp_hidh_dev_close(dev);
    } else if (id == ESP_HIDH_CLOSE_EVENT) {
        generation.fetch_add(1);
        lock();
        if (device == p->close.dev) device = nullptr;
        state.ble_connected = state.ble_connecting = state.ble_encrypted = false;
        state.ble_bonded = state.ble_keyboard_ready = false;
        state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
        state.ble_conn_handle = NO_CONNECTION;
        allowed_count = 0;
        char msg[TAB5_RADIO_MESSAGE_LEN];
        std::snprintf(msg, sizeof(msg), "DISCONNECTED - REASON %d", p->close.reason);
        message(state.bluetooth_enabled ? msg : "BLUETOOTH OFF");
        unlock();
        // The open worker may still be completing subscriptions. Free only there.
        if (xQueueSend(closed_devices, &p->close.dev, 0) != pdTRUE)
            ESP_LOGE(TAG, "Deferred HID close queue full");
    } else if (id == ESP_HIDH_INPUT_EVENT && p->input.usage == ESP_HID_USAGE_KEYBOARD) {
        lock();
        bool accept = device == p->input.dev && state.ble_keyboard_ready && state.bluetooth_enabled;
        bool layout = false;
        for (size_t i = 0; i < allowed_count; ++i)
            layout |= allowed[i].map == p->input.map_index && allowed[i].id == p->input.report_id;
        unlock();
        if (!accept || !layout || p->input.length != 8) return;
        Report report{generation.load(), {}};
        static uint32_t last_logged = UINT32_MAX;
        if (last_logged != report.generation) {
            last_logged = report.generation;
            ESP_LOGI(TAG, "First validated keyboard input received (payload omitted)");
        }
        std::memcpy(report.data, p->input.data, 8);
        if (xQueueSend(reports, &report, 0) != pdTRUE) generation.fetch_add(1);
    }
}
void worker(void *) {
    tab5_ble_device_t request{};
    for (;;) {
        esp_hidh_dev_t *closed = nullptr;
        while (xQueueReceive(closed_devices, &closed, 0) == pdTRUE) esp_hidh_dev_free(closed);
        if (xQueueReceive(work, &request, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        lock(); bool scanning = scan_active; bool enabled = state.bluetooth_enabled; unlock();
        if (scanning) {
            xSemaphoreTake(scan_stopped, 0);
            if (esp_ble_gap_stop_scanning() == ESP_OK)
                xSemaphoreTake(scan_stopped, pdMS_TO_TICKS(1500));
        }
        if (!enabled) continue;
        ESP_LOGI(TAG, "Opening selected BLE HID device (address type=%u)", request.addr_type);
        esp_hidh_dev_t *opened = esp_hidh_dev_open(request.addr, ESP_HID_TRANSPORT_BLE, request.addr_type);
        lock();
        if (!opened) {
            state.ble_connecting = false;
            state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
            message(state.bluetooth_enabled ? "CONNECT FAILED - PUT KEYBOARD IN PAIRING MODE"
                                             : "BLUETOOTH OFF");
        }
        enabled = state.bluetooth_enabled;
        unlock();
        if (opened && !enabled) esp_ble_gap_disconnect(request.addr);
    }
}
void emit(void *, const cm::KeySequence &key) {
    if (uxQueueSpacesAvailable(bytes) < key.size) return;
    for (size_t i = 0; i < key.size; ++i) xQueueSend(bytes, key.bytes + i, 0);
}
} // namespace

esp_err_t tab5_bluetooth_init(void) {
    if (initialized) return ESP_OK;
    mutex = xSemaphoreCreateMutex(); scan_stopped = xSemaphoreCreateBinary();
    work = xQueueCreate(1, sizeof(tab5_ble_device_t));
    closed_devices = xQueueCreate(4, sizeof(esp_hidh_dev_t *));
    reports = xQueueCreate(32, sizeof(Report)); bytes = xQueueCreate(256, 1);
    if (!mutex || !scan_stopped || !work || !closed_devices || !reports || !bytes) return ESP_ERR_NO_MEM;
    state.ble_conn_handle = NO_CONNECTION;
    message("BLUETOOTH OFF");
    hosted_hci_bluedroid_open();
    esp_bluedroid_hci_driver_operations_t ops{};
    ops.send = hosted_hci_bluedroid_send;
    ops.check_send_available = hosted_hci_bluedroid_check_send_available;
    ops.register_host_callback = hosted_hci_bluedroid_register_host_callback;
    esp_bluedroid_attach_hci_driver(&ops);
    esp_err_t err = esp_bluedroid_init();
    if (err == ESP_OK) err = esp_bluedroid_enable();
    if (err == ESP_OK) err = esp_ble_gap_register_callback(gap_callback);
    if (err == ESP_OK) err = esp_ble_gattc_register_callback(gatt_callback);
    if (err != ESP_OK) return err;
    // Exact CYD B1 policy: display passkeys, bond, prefer SC/MITM; legacy peers
    // negotiate their supported authentication mode (the tested B1 uses 0x01).
    uint8_t auth = ESP_LE_AUTH_REQ_SC_MITM_BOND, io = ESP_IO_CAP_OUT;
    uint8_t key_size = 16, keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    err = esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1);
    if (err == ESP_OK) err = esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1);
    if (err == ESP_OK) err = esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, 1);
    if (err == ESP_OK) err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &keys, 1);
    if (err == ESP_OK) err = esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &keys, 1);
    if (err != ESP_OK) return err;
    esp_hidh_config_t cfg{}; cfg.callback = hid_callback; cfg.event_stack_size = 4096;
    err = esp_hidh_init(&cfg);
    if (err != ESP_OK) return err;
    if (xTaskCreate(worker, "tab5_ble_open", 6144, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    initialized = true;
    ESP_LOGI(TAG, "Hosted Bluedroid HID ready; CYD B1 descriptor validator + decoder");
    return ESP_OK;
}
void tab5_bluetooth_get_status(tab5_radio_status_t *out) {
    if (!mutex) return;
    lock();
    out->revision += state.revision;
    out->bluetooth_enabled = state.bluetooth_enabled; out->bt_scanning = state.bt_scanning;
    out->ble_count = state.ble_count; out->ble_connected = state.ble_connected;
    out->ble_connecting = state.ble_connecting; out->ble_keyboard_ready = state.ble_keyboard_ready;
    out->ble_encrypted = state.ble_encrypted; out->ble_bonded = state.ble_bonded;
    out->ble_conn_handle = state.ble_conn_handle; out->ble_passkey = state.ble_passkey;
    out->ble_pairing_mode = state.ble_pairing_mode;
    std::memcpy(out->bt_message, state.bt_message, sizeof(out->bt_message));
    std::memcpy(out->ble_peer_name, state.ble_peer_name, sizeof(out->ble_peer_name));
    std::memcpy(out->ble_peer_addr, state.ble_peer_addr, sizeof(out->ble_peer_addr));
    out->ble_peer_addr_type = state.ble_peer_addr_type;
    unlock();
}
extern "C" esp_err_t tab5_radio_set_bluetooth_enabled(bool enabled) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    lock();
    state.bluetooth_enabled = enabled;
    if (!enabled) {
        permitted = false; state.ble_keyboard_ready = false;
        state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
        generation.fetch_add(1);
    }
    bool scanning = scan_active; bool linked = state.ble_connected || state.ble_connecting;
    tab5_ble_device_t peer = selected;
    message(enabled ? "BLUETOOTH READY - SCAN FOR DEVICES" : "BLUETOOTH OFF");
    unlock();
    if (!enabled && scanning) esp_ble_gap_stop_scanning();
    if (!enabled && linked) esp_ble_gap_disconnect(peer.addr);
    return ESP_OK;
}
extern "C" esp_err_t tab5_radio_ble_scan(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    lock();
    if (!state.bluetooth_enabled || state.ble_connecting || scan_active) { unlock(); return ESP_ERR_INVALID_STATE; }
    std::memset(devices, 0, sizeof(devices)); // Never inherit stale names on a new scan.
    state.ble_count = 0; state.bt_scanning = scan_active = true;
    message("SCANNING BLUETOOTH..."); unlock();
    esp_ble_scan_params_t params{};
    params.scan_type = BLE_SCAN_TYPE_ACTIVE; params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
    params.scan_interval = 0x50; params.scan_window = 0x30;
    params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
    esp_err_t err = esp_ble_gap_set_scan_params(&params);
    if (err != ESP_OK) scan_finished();
    return err;
}
extern "C" bool tab5_radio_get_ble_device(size_t index, tab5_ble_device_t *out) {
    if (!out || !initialized) return false;
    lock(); bool found = index < state.ble_count;
    if (found) *out = devices[index];
    unlock(); return found;
}
extern "C" esp_err_t tab5_radio_ble_connect(const tab5_ble_device_t *peer) {
    if (!peer || !initialized) return ESP_ERR_INVALID_ARG;
    lock();
    if (!state.bluetooth_enabled || state.ble_connecting || device) { unlock(); return ESP_ERR_INVALID_STATE; }
    if (!peer->connectable) { message("DEVICE IS NOT ADVERTISING A CONNECTABLE LINK"); unlock(); return ESP_ERR_NOT_SUPPORTED; }
    selected = *peer; permitted = true; allowed_count = 0;
    state.ble_connecting = true; state.ble_keyboard_ready = state.ble_encrypted = state.ble_bonded = false;
    state.ble_pairing_mode = TAB5_BLE_PAIRING_NONE;
    std::snprintf(state.ble_peer_name, sizeof(state.ble_peer_name), "%s", peer->name);
    std::memcpy(state.ble_peer_addr, peer->addr, 6); state.ble_peer_addr_type = peer->addr_type;
    generation.fetch_add(1);
    message("CONNECTING KEYBOARD..."); unlock();
    if (xQueueSend(work, peer, 0) == pdTRUE) return ESP_OK;
    lock(); state.ble_connecting = false; message("KEYBOARD WORKER BUSY"); unlock();
    return ESP_FAIL;
}
extern "C" esp_err_t tab5_radio_ble_disconnect(void) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    lock(); auto peer = selected; permitted = false;
    state.ble_keyboard_ready = false; generation.fetch_add(1); unlock();
    return esp_ble_gap_disconnect(peer.addr);
}
extern "C" esp_err_t tab5_radio_ble_pair_confirm(bool accept) {
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (!accept) return tab5_radio_ble_disconnect();
    lock(); auto peer = selected; auto mode = state.ble_pairing_mode; unlock();
    if (mode != TAB5_BLE_PAIRING_CONFIRM) return ESP_ERR_INVALID_STATE;
    return esp_ble_confirm_reply(peer.addr, true);
}
extern "C" esp_err_t tab5_radio_ble_submit_passkey(uint32_t passkey) {
    if (!initialized || passkey > 999999) return ESP_ERR_INVALID_ARG;
    lock(); auto peer = selected; auto mode = state.ble_pairing_mode; unlock();
    if (mode != TAB5_BLE_PAIRING_INPUT) return ESP_ERR_INVALID_STATE;
    return esp_ble_passkey_reply(peer.addr, true, passkey);
}
extern "C" size_t tab5_radio_ble_keyboard_poll(uint8_t *out, size_t capacity) {
    if (!initialized || !out || !capacity) return 0;
    static uint32_t seen_generation = 0;
    uint32_t current = generation.load();
    if (current != seen_generation) {
        decoder.reset(); xQueueReset(bytes); seen_generation = current;
    }
    lock(); bool ready = state.ble_keyboard_ready && state.bluetooth_enabled; unlock();
    Report report{};
    uint64_t now = esp_timer_get_time() / 1000;
    while (xQueueReceive(reports, &report, 0) == pdTRUE) {
        if (ready && report.generation == current &&
            !decoder.report(report.data, sizeof(report.data), now, emit, nullptr)) decoder.reset();
    }
    if (ready) decoder.tick(now, emit, nullptr);
    size_t count = 0;
    while (count < capacity && xQueueReceive(bytes, out + count, 0) == pdTRUE) ++count;
    return count;
}
