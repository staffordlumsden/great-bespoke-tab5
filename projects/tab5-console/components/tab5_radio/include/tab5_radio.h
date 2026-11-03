#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_RADIO_MAX_WIFI_NETWORKS 16
#define TAB5_RADIO_MAX_BLE_DEVICES   16
#define TAB5_RADIO_SSID_LEN          33
#define TAB5_RADIO_NAME_LEN          33
#define TAB5_RADIO_MESSAGE_LEN       80

typedef enum {
    TAB5_BLE_PAIRING_NONE = 0,
    TAB5_BLE_PAIRING_CONFIRM,
    TAB5_BLE_PAIRING_INPUT,
    TAB5_BLE_PAIRING_DISPLAY,
} tab5_ble_pairing_mode_t;

typedef struct {
    char ssid[TAB5_RADIO_SSID_LEN];
    int8_t rssi;
    uint8_t authmode;
    bool secure;
} tab5_wifi_network_t;

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
    int8_t rssi;
    bool connectable;
    char name[TAB5_RADIO_NAME_LEN];
} tab5_ble_device_t;

typedef struct {
    uint32_t revision;
    bool initialized;
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool wifi_scanning;
    bool bt_scanning;
    bool wifi_connected;
    bool wifi_has_ip;
    int8_t wifi_rssi;
    char wifi_ssid[TAB5_RADIO_SSID_LEN];
    char ip_address[16];
    char wifi_message[TAB5_RADIO_MESSAGE_LEN];
    char bt_message[TAB5_RADIO_MESSAGE_LEN];
    size_t wifi_count;
    size_t ble_count;
    bool ble_connected;
    bool ble_connecting;
    bool ble_keyboard_ready;
    bool ble_bonded;
    bool ble_encrypted;
    uint16_t ble_conn_handle;
    uint32_t ble_passkey;
    tab5_ble_pairing_mode_t ble_pairing_mode;
    char ble_peer_name[TAB5_RADIO_NAME_LEN];
    uint8_t ble_peer_addr[6];
    uint8_t ble_peer_addr_type;
} tab5_radio_status_t;

/** Initialise hosted Wi-Fi and the Bluedroid HID host. The C6 power rail must already
 * be enabled by the board HAL before this call. */
esp_err_t tab5_radio_init(void);

esp_err_t tab5_radio_set_wifi_enabled(bool enabled);
esp_err_t tab5_radio_set_bluetooth_enabled(bool enabled);
void tab5_radio_get_status(tab5_radio_status_t *out);

esp_err_t tab5_radio_wifi_scan(void);
bool tab5_radio_get_wifi_network(size_t index, tab5_wifi_network_t *out);
esp_err_t tab5_radio_wifi_connect(const char *ssid, const char *password);
esp_err_t tab5_radio_wifi_disconnect(void);
bool tab5_radio_get_saved_wifi(char *ssid, size_t ssid_size, char *password, size_t pass_size);

esp_err_t tab5_radio_ble_scan(void);
bool tab5_radio_get_ble_device(size_t index, tab5_ble_device_t *out);
esp_err_t tab5_radio_ble_connect(const tab5_ble_device_t *device);
esp_err_t tab5_radio_ble_disconnect(void);
esp_err_t tab5_radio_ble_pair_confirm(bool accept);
esp_err_t tab5_radio_ble_submit_passkey(uint32_t passkey);

/** US-layout input from the CYD's validated BLE keyboard decoder. Call from
 * the UI task, including idle polls so held-key repeat and disconnect reset run. */
size_t tab5_radio_ble_keyboard_poll(uint8_t *out, size_t capacity);

#ifdef __cplusplus
}
#endif
