#include "tab5_hal.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"
static const char *TAG = "TAB5_KEYBOARD";
static QueueHandle_t events, bytes;
static atomic_bool connected;
static _Atomic(hid_host_device_handle_t) keyboard;
static uint8_t previous[6];
static bool caps_lock;
static portMUX_TYPE filter_lock = portMUX_INITIALIZER_UNLOCKED;
static tab5_keyboard_key_filter_t key_filter;
static void *key_filter_context;

static void emit(const char *s, size_t n)
{
    if (uxQueueSpacesAvailable(bytes) < n) { ESP_LOGW(TAG, "Input queue full; dropping key"); return; }
    for (size_t i = 0; i < n; i++) xQueueSend(bytes, s + i, 0);
}
static void key_press(uint8_t code, uint8_t mods)
{
    bool shift = mods & 0x22;
    const char *seq = NULL;
    char c = 0;
    if (code == 0x39) { caps_lock = !caps_lock; return; }
    if (code >= 4 && code <= 29) {
        c = (shift != caps_lock ? 'A' : 'a') + code - 4;
        if (mods & 0x11) c = code - 3;
    } else if (code >= 30 && code <= 39) {
        c = (shift ? "!@#$%^&*()" : "1234567890")[code - 30];
    } else {
        switch (code) {
        case 40: c = '\r'; break;
        case 41: c = '\x1b'; break;
        case 42: c = '\x7f'; break;
        case 43: c = '\t'; break;
        case 44: c = ' '; break;
        case 45: c = shift ? '_' : '-'; break;
        case 46: c = shift ? '+' : '='; break;
        case 47: c = shift ? '{' : '['; break;
        case 48: c = shift ? '}' : ']'; break;
        case 49: c = shift ? '|' : '\\'; break;
        case 51: c = shift ? ':' : ';'; break;
        case 52: c = shift ? '"' : '\''; break;
        case 53: c = shift ? '~' : '`'; break;
        case 54: c = shift ? '<' : ','; break;
        case 55: c = shift ? '>' : '.'; break;
        case 56: c = shift ? '?' : '/'; break;
        case 73: seq = "\x1b[2~"; break;
        case 74: seq = "\x1b[H"; break;
        case 75: seq = "\x1b[5~"; break;
        case 76: seq = "\x1b[3~"; break;
        case 77: seq = "\x1b[F"; break;
        case 78: seq = "\x1b[6~"; break;
        case 79: seq = "\x1b[C"; break;
        case 80: seq = "\x1b[D"; break;
        case 81: seq = "\x1b[B"; break;
        case 82: seq = "\x1b[A"; break;
        default: return;
        }
    }
    char output[10]; size_t n = 0;
    if (mods & 0x44) output[n++] = '\x1b';
    if (seq) { size_t l = strlen(seq); memcpy(output + n, seq, l); n += l; }
    else output[n++] = c;
    emit(output, n);
}
static void report(const uint8_t *data, size_t len)
{
    if (len != 8) return;
    // RollOver/POSTFail/ErrorUndefined: retain the last valid state.
    for (size_t i = 2; i < 8; i++) if (data[i] >= 1 && data[i] <= 3) return;
    for (size_t i = 2; i < 8; i++) {
        uint8_t code = data[i];
        if (!code || memchr(previous, code, sizeof(previous)) || memchr(data + 2, code, i - 2)) continue;
        ESP_LOGI(TAG, "Key press usage=0x%02x modifiers=0x%02x", code, data[0]);
        tab5_keyboard_key_filter_t filter;
        void *context;
        portENTER_CRITICAL(&filter_lock);
        filter = key_filter;
        context = key_filter_context;
        portEXIT_CRITICAL(&filter_lock);
        if (filter && filter(code, data[0], context)) continue;
        key_press(code, data[0]);
    }
    memcpy(previous, data + 2, sizeof(previous));
}
static void interface_event(hid_host_device_handle_t dev, hid_host_interface_event_t event, void *arg)
{
    (void)arg;
    if (dev != keyboard) return;
    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        uint8_t data[64]; size_t len = 0;
        if (hid_host_device_get_raw_input_report_data(dev, data, sizeof(data), &len) == ESP_OK) report(data, len);
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        atomic_store(&connected, false); keyboard = NULL;
        memset(previous, 0, sizeof(previous)); caps_lock = false;
        xQueueReset(bytes);
        ESP_LOGI(TAG, "Keyboard disconnected (close=%s)", esp_err_to_name(hid_host_device_close(dev)));
    } else if (event == HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) ESP_LOGW(TAG, "Keyboard transfer error");
}
static void driver_event(hid_host_device_handle_t dev, hid_host_driver_event_t event, void *arg)
{
    (void)arg;
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED && xQueueSend(events, &dev, 0) != pdTRUE)
        ESP_LOGE(TAG, "HID connection queue full");
}
// Control requests run outside the HID driver's callback task.
static void device_task(void *arg)
{
    (void)arg;
    hid_host_device_handle_t dev;
    for (;;) {
        if (xQueueReceive(events, &dev, portMAX_DELAY) != pdTRUE) continue;
        hid_host_dev_params_t p;
        if (hid_host_device_get_params(dev, &p) != ESP_OK) continue;
        ESP_LOGI(TAG, "HID interface addr=%u iface=%u subclass=%u protocol=%u", p.addr, p.iface_num, p.sub_class, p.proto);
        if (keyboard || p.sub_class != HID_SUBCLASS_BOOT_INTERFACE || p.proto != HID_PROTOCOL_KEYBOARD) continue;
        hid_host_device_config_t cfg = {.callback = interface_event};
        esp_err_t r = hid_host_device_open(dev, &cfg);
        if (r != ESP_OK) { ESP_LOGW(TAG, "Open: %s", esp_err_to_name(r)); continue; }
        r = hid_class_request_set_protocol(dev, HID_REPORT_PROTOCOL_BOOT);
        if (r == ESP_OK) {
            hid_class_request_set_idle(dev, 0, 0); // optional on some keyboards
            keyboard = dev; memset(previous, 0, sizeof(previous)); caps_lock = false;
            atomic_store(&connected, true);
            r = hid_host_device_start(dev);
        }
        if (r != ESP_OK) {
            atomic_store(&connected, false); keyboard = NULL;
            hid_host_device_close(dev);
            ESP_LOGW(TAG, "Start: %s", esp_err_to_name(r));
        } else {
            hid_host_dev_info_t info;
            if (hid_host_get_device_info(dev, &info) == ESP_OK)
                ESP_LOGI(TAG, "Keyboard connected VID=%04x PID=%04x", info.VID, info.PID);
        }
    }
}
static void host_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags = 0;
        esp_err_t r = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (r != ESP_OK) { ESP_LOGW(TAG, "Host events: %s", esp_err_to_name(r)); vTaskDelay(pdMS_TO_TICKS(10)); }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}
esp_err_t tab5_keyboard_init(void)
{
    events = xQueueCreate(8, sizeof(hid_host_device_handle_t)); bytes = xQueueCreate(256, 1);
    if (!events || !bytes) {
        if (events) vQueueDelete(events);
        if (bytes) vQueueDelete(bytes);
        events = bytes = NULL; return ESP_ERR_NO_MEM;
    }
    usb_host_config_t host = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    esp_err_t r = usb_host_install(&host);
    if (r != ESP_OK) goto fail_queues;
    TaskHandle_t host_handle, device_handle;
    if (xTaskCreate(host_task, "tab5_usb", 4096, NULL, 5, &host_handle) != pdPASS) {
        r = ESP_ERR_NO_MEM; usb_host_uninstall(); goto fail_queues;
    }
    if (xTaskCreate(device_task, "tab5_kbd", 4096, NULL, 4, &device_handle) != pdPASS) {
        r = ESP_ERR_NO_MEM; vTaskDelete(host_handle); usb_host_uninstall(); goto fail_queues;
    }
    hid_host_driver_config_t hid = {.create_background_task = true, .task_priority = 5,
        .stack_size = 4096, .core_id = tskNO_AFFINITY, .callback = driver_event};
    r = hid_host_install(&hid);
    if (r != ESP_OK) {
        vTaskDelete(device_handle); vTaskDelete(host_handle); usb_host_uninstall(); goto fail_queues;
    }
    ESP_LOGI(TAG, "USB host and HID driver ready; USB-A power enabled");
    return ESP_OK;
fail_queues:
    vQueueDelete(events); vQueueDelete(bytes); events = bytes = NULL; return r;
}
bool tab5_keyboard_connected(void) { return atomic_load(&connected); }
void tab5_keyboard_set_key_filter(tab5_keyboard_key_filter_t filter, void *context)
{
    portENTER_CRITICAL(&filter_lock);
    key_filter = filter;
    key_filter_context = context;
    portEXIT_CRITICAL(&filter_lock);
}
size_t tab5_keyboard_poll(uint8_t *out, size_t capacity)
{
    if (!out || !bytes) return 0;
    size_t count = 0;
    while (count < capacity && xQueueReceive(bytes, out + count, 0) == pdTRUE) count++;
    return count;
}
