#include "tab5_app_boot.h"
#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "nvs.h"
#include <string.h>

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "SD applications require the rollback-enabled Console bootloader"
#endif

static const esp_partition_t *find(esp_partition_type_t type, int subtype,
                                  uint32_t address, uint32_t size)
{
    const esp_partition_t *p = esp_partition_find_first(type, subtype, NULL);
    return p && p->address == address && p->size == size && !p->encrypted ? p : NULL;
}

static const esp_partition_t *console(void)
{
    const esp_partition_t *p = find(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_FACTORY, TAB5_CONSOLE_ADDRESS, TAB5_CONSOLE_SIZE);
    esp_app_desc_t desc;
    if (!p || esp_ota_get_partition_description(p, &desc) != ESP_OK ||
        strncmp(desc.project_name, "tab5_console", sizeof(desc.project_name))) return NULL;
    return p;
}

esp_err_t tab5_app_boot_check_layout(void)
{
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK || flash_size != 0x1000000 ||
        !console() ||
        !find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, 0x9000, 0x6000) ||
        !find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_PHY, 0xf000, 0x1000) ||
        !find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA,
              TAB5_OTA_DATA_ADDRESS, 0x2000) ||
        !find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0,
              0x420000, TAB5_APP_SLOT_SIZE) ||
        !find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1,
              0x920000, TAB5_APP_SLOT_SIZE)) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

bool tab5_app_boot_console_available(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    return tab5_app_boot_check_layout() == ESP_OK &&
           running && running->type == ESP_PARTITION_TYPE_APP &&
           (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0 ||
            running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);
}

esp_err_t tab5_app_boot_prepare_launch(const esp_partition_t **slot)
{
    if (!slot) return ESP_ERR_INVALID_ARG;
    *slot = NULL;
    if (tab5_app_boot_check_layout() != ESP_OK ||
        esp_ota_get_running_partition() != console()) return ESP_ERR_INVALID_STATE;

    uint8_t last = 1;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("tab5_launcher", NVS_READONLY, &handle);
    if (result == ESP_OK) {
        result = nvs_get_u8(handle, "last_slot", &last);
        nvs_close(handle);
        if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) return result;
        if (last > 1) last = 1;
    } else if (result != ESP_ERR_NVS_NOT_FOUND) return result;

    /* Clear any previous OTA selection before staging. With only the new
     * candidate in otadata, an unconfirmed reboot falls back to factory. */
    result = esp_ota_set_boot_partition(console());
    if (result != ESP_OK) return result;
    *slot = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        last == 0 ? ESP_PARTITION_SUBTYPE_APP_OTA_1 : ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    return *slot ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t tab5_app_boot_select(const esp_partition_t *slot)
{
    if (tab5_app_boot_check_layout() != ESP_OK ||
        esp_ota_get_running_partition() != console() || !slot) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *a = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *b = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    if (slot != a && slot != b) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t result = nvs_open("tab5_launcher", NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_u8(handle, "last_slot", slot == b ? 1 : 0);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) return result;
    return esp_ota_set_boot_partition(slot);
}

esp_err_t tab5_app_boot_ready(bool hardware_ok)
{
    if (!hardware_ok || !tab5_app_boot_console_available()) return ESP_ERR_INVALID_STATE;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
        running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_1) return ESP_ERR_INVALID_STATE;
    esp_ota_img_states_t state;
    esp_err_t result = esp_ota_get_state_partition(running, &state);
    if (result == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        result = esp_ota_mark_app_valid_cancel_rollback();
        if (result != ESP_OK) return result;
    } else if (result == ESP_OK && state != ESP_OTA_IMG_VALID &&
               state != ESP_OTA_IMG_UNDEFINED) return ESP_ERR_INVALID_STATE;
    else if (result != ESP_OK && result != ESP_ERR_NOT_FOUND) return result;
    /* Select factory only AFTER confirmation; selecting it erases otadata. */
    return esp_ota_set_boot_partition(console());
}

esp_err_t tab5_app_boot_return_to_console(void)
{
    if (!tab5_app_boot_console_available()) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = esp_ota_set_boot_partition(console());
    if (result == ESP_OK) esp_restart();
    return result;
}
