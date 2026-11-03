#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_FOUND 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_INVALID_CRC 6
#define ESP_ERR_INVALID_SIZE 7
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(lock) ((void)(lock))
#define taskEXIT_CRITICAL(lock) ((void)(lock))
void vTaskDelay(unsigned ticks);
typedef uint32_t esp_ota_handle_t;
#define OTA_WITH_SEQUENTIAL_WRITES ((size_t)-2)
typedef int esp_partition_type_t;
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
#define ESP_PARTITION_SUBTYPE_APP_OTA_1 17
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_PARTITION_SUBTYPE_DATA_PHY 1
#define ESP_PARTITION_SUBTYPE_DATA_NVS 2
typedef struct esp_partition_stub {
    int type, subtype;
    uint32_t address, size;
    bool encrypted;
} esp_partition_t;
esp_err_t esp_ota_begin(const esp_partition_t *, size_t, esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t, const void *, size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *, size_t);
typedef struct { char project_name[32]; } esp_app_desc_t;
typedef enum { ESP_OTA_IMG_NEW, ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID,
               ESP_OTA_IMG_UNDEFINED } esp_ota_img_states_t;
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *, esp_app_desc_t *);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_flash_get_size(void *, uint32_t *);
void esp_restart(void);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_u8(nvs_handle_t, const char *, uint8_t *);
esp_err_t nvs_set_u8(nvs_handle_t, const char *, uint8_t);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
