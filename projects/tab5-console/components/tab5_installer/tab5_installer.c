/* TAB5 Console SD-app installer: bounded, hash-verified OTA install from SD. */
#include "tab5_installer.h"
#include "tab5_app_boot.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "TAB5_INSTALLER"
#define SLOT_SIZE 0x500000
#define ESP32P4_CHIP_ID 0x0012
#define TESTED_REVISION 103
#define FLASH_SIZE_16MB 4

static tab5_installer_state_t s_state = TAB5_INSTALLER_IDLE;
static uint8_t s_progress = 0;
static char s_message[80] = "IDLE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static tab5_installer_progress_cb_t s_callback;
static void *s_callback_user;

void tab5_installer_set_progress_callback(tab5_installer_progress_cb_t callback, void *user)
{
    s_callback = callback;
    s_callback_user = user;
}

static void publish(void)
{
    if (s_callback) {
        tab5_installer_status_t snapshot;
        tab5_installer_get_status(&snapshot);
        s_callback(&snapshot, s_callback_user);
    }
}

static void set_status(tab5_installer_state_t state, const char *message, int progress)
{
    taskENTER_CRITICAL(&s_lock);
    s_state = state;
    if (message) {
        snprintf(s_message, sizeof(s_message), "%s", message);
    } else {
        s_message[0] = '\0';
    }
    if (progress >= 0) s_progress = (uint8_t)progress;
    taskEXIT_CRITICAL(&s_lock);
    publish();
}

static void set_state(tab5_installer_state_t state, const char *message)
{
    set_status(state, message, state == TAB5_INSTALLER_DONE ? 100 :
                              state == TAB5_INSTALLER_IDLE ? 0 : -1);
}

static void set_progress(uint8_t value)
{
    taskENTER_CRITICAL(&s_lock);
    s_progress = value > 100 ? 100 : value;
    taskEXIT_CRITICAL(&s_lock);
    publish();
}

void tab5_installer_get_status(tab5_installer_status_t *status)
{
    if (!status) return;
    taskENTER_CRITICAL(&s_lock);
    status->state = s_state;
    status->progress = s_progress;
    snprintf(status->message, sizeof(status->message), "%s", s_message);
    taskEXIT_CRITICAL(&s_lock);
}

static bool ends_with(const char *path, const char *suffix)
{
    const size_t plen = strlen(path);
    const size_t slen = strlen(suffix);
    return plen >= slen && strcmp(path + plen - slen, suffix) == 0;
}

static bool safe_filename(const char *name)
{
    if (!name || !name[0] || !strcmp(name, ".") || !strcmp(name, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == ' ' || *p == '.' || *p == '_' || *p == '-'))
            return false;
    }
    return true;
}

static void copy_safe(char *dst, size_t capacity, const char *src)
{
    const size_t len = src ? strlen(src) : 0;
    const size_t n = len < capacity - 1 ? len : capacity - 1;
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

static bool json_string(const char *raw, const char *key, char *out, size_t capacity)
{
    char marker[32];
    const int marker_len = snprintf(marker, sizeof(marker), "\"%s\"", key);
    if (marker_len <= 0 || (size_t)marker_len >= sizeof(marker) || capacity < 2) return false;
    const char *p = strstr(raw, marker);
    if (!p) return false;
    p += marker_len;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != '\"') return false;
    size_t len = 0;
    while (*p && *p != '\"') {
        const unsigned char c = (unsigned char)*p++;
        if (c < 32 || c == '\\' || len + 1 >= capacity) return false;
        out[len++] = (char)c;
    }
    if (*p != '\"' || len == 0) return false;
    out[len] = '\0';
    return true;
}

static bool json_size(const char *raw, uint32_t *out)
{
    const char *p = strstr(raw, "\"size_bytes\"");
    if (!p || !out) return false;
    p += sizeof("\"size_bytes\"") - 1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p++ != ':') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p < '0' || *p > '9') return false;
    errno = 0;
    char *end = NULL;
    const unsigned long value = strtoul(p, &end, 10);
    if (errno || end == p || value == 0 || value > UINT32_MAX) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != ',' && *end != '}') return false;
    *out = (uint32_t)value;
    return true;
}

static bool valid_sha256(const char *value)
{
    if (strlen(value) != TAB5_INSTALLER_SHA256_HEX_LEN - 1) return false;
    for (size_t i = 0; value[i]; ++i) {
        const char c = value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

static bool read_json(const char *path, char *project, char *version,
                      char *image_file, char *license_file, uint32_t *size_bytes,
                      char *sha256)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char raw[512];
    const size_t n = fread(raw, 1, sizeof(raw) - 1, file);
    const bool truncated = n == sizeof(raw) - 1 && fgetc(file) != EOF;
    const bool read_error = ferror(file);
    fclose(file);
    raw[n] = '\0';
    if (truncated || read_error) return false;

    project[0] = version[0] = image_file[0] = license_file[0] = sha256[0] = '\0';
    return json_string(raw, "project", project, TAB5_INSTALLER_PROJECT_NAME_MAX) &&
           json_string(raw, "version", version, TAB5_INSTALLER_VERSION_MAX) &&
           json_string(raw, "image", image_file, TAB5_INSTALLER_PATH_MAX) &&
           json_string(raw, "license", license_file, TAB5_INSTALLER_PATH_MAX) &&
           json_string(raw, "sha256", sha256, TAB5_INSTALLER_SHA256_HEX_LEN) &&
           valid_sha256(sha256) && json_size(raw, size_bytes);
}

static bool sibling_path(const char *json_path, const char *file_name,
                         char *out, size_t capacity)
{
    const char *last = strrchr(json_path, '/');
    if (!last) return false;
    const size_t dir_len = last - json_path;
    const size_t name_len = strlen(file_name);
    if (dir_len + 1 + name_len >= capacity) return false;
    memcpy(out, json_path, dir_len);
    out[dir_len] = '/';
    memcpy(out + dir_len + 1, file_name, name_len + 1);
    return true;
}

static int compare_packages(const void *a, const void *b)
{
    const tab5_installer_package_t *pa = (const tab5_installer_package_t *)a;
    const tab5_installer_package_t *pb = (const tab5_installer_package_t *)b;
    int cmp = strcasecmp(pa->project, pb->project);
    if (cmp != 0) return cmp;
    return strcasecmp(pa->version, pb->version);
}

esp_err_t tab5_installer_scan(tab5_installer_catalog_t *catalog)
{
    if (!catalog) return ESP_ERR_INVALID_ARG;
    memset(catalog, 0, sizeof(*catalog));
    DIR *dir = opendir("/sdcard/firmware");
    if (!dir) {
        set_state(TAB5_INSTALLER_FAILED, "NO /sdcard/firmware");
        return ESP_ERR_NOT_FOUND;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && catalog->count < TAB5_INSTALLER_MAX_PACKAGES) {
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) continue;
        if (entry->d_name[0] == '.') continue;
        if (!ends_with(entry->d_name, ".json")) continue;
        if (strlen(entry->d_name) >= 239) continue;
        tab5_installer_package_t *pkg = &catalog->packages[catalog->count];
        char bounded_name[239];
        char json_path[TAB5_INSTALLER_PATH_MAX];
        copy_safe(bounded_name, sizeof(bounded_name), entry->d_name);
        const int path_len = snprintf(json_path, sizeof(json_path), "/sdcard/firmware/%s",
                                      bounded_name);
        if (path_len <= 0 || (size_t)path_len >= sizeof(json_path)) continue;
        char project[TAB5_INSTALLER_PROJECT_NAME_MAX];
        char version[TAB5_INSTALLER_VERSION_MAX];
        char image_file[TAB5_INSTALLER_PATH_MAX];
        char license_file[TAB5_INSTALLER_PATH_MAX];
        char sha256[TAB5_INSTALLER_SHA256_HEX_LEN];
        uint32_t size_bytes = 0;
        if (!read_json(json_path, project, version, image_file, license_file,
                       &size_bytes, sha256)) continue;
        if (!safe_filename(image_file) || !safe_filename(license_file)) continue;
        char image_path[TAB5_INSTALLER_PATH_MAX];
        char license_path[TAB5_INSTALLER_PATH_MAX];
        if (!sibling_path(json_path, image_file, image_path, sizeof(image_path))) continue;
        if (!sibling_path(json_path, license_file, license_path, sizeof(license_path))) continue;
        copy_safe(pkg->json_path, sizeof(pkg->json_path), json_path);
        copy_safe(pkg->image_path, sizeof(pkg->image_path), image_path);
        copy_safe(pkg->license_path, sizeof(pkg->license_path), license_path);
        copy_safe(pkg->project, sizeof(pkg->project), project);
        copy_safe(pkg->version, sizeof(pkg->version), version);
        pkg->size_bytes = size_bytes;
        copy_safe(pkg->sha256, sizeof(pkg->sha256), sha256);
        pkg->valid = false;
        ++catalog->count;
    }
    closedir(dir);
    if (catalog->count > 1) {
        qsort(catalog->packages, catalog->count, sizeof(catalog->packages[0]), compare_packages);
    }
    set_state(TAB5_INSTALLER_IDLE,
              catalog->count ? "PACKAGES FOUND" : "NO PACKAGES");
    return ESP_OK;
}

static void hexify(const uint8_t *in, char *out, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[len * 2] = '\0';
}

static bool inspect_image(const uint8_t *data, size_t length, char *project_out,
                          size_t project_cap, char *version_out, size_t version_cap)
{
    if (length < 320 || length > SLOT_SIZE) return false;
    if (data[0] != 0xE9 || data[1] < 1 || data[1] > 16) return false;
    uint16_t chip;
    memcpy(&chip, data + 12, sizeof(chip));
    if (chip != ESP32P4_CHIP_ID) return false;
    if ((data[3] >> 4) != FLASH_SIZE_16MB) return false;
    if (data[23] != 1) return false;
    uint32_t marker;
    memcpy(&marker, data + 32, sizeof(marker));
    if (marker != 0xABCD5432) return false;

    uint16_t minimum = data[15] | ((uint16_t)data[16] << 8);
    if (minimum == 0) minimum = data[14] * 100;
    uint16_t maximum = data[17] | ((uint16_t)data[18] << 8);
    if (minimum > TESTED_REVISION ||
        (maximum != 0 && maximum != 0xFFFF && maximum < TESTED_REVISION)) {
        return false;
    }

    /* Application description fields at offset 48 and 80 must be valid ASCII. */
    for (size_t offset = 48; offset <= 80; offset += 32) {
        size_t term = 0;
        while (term < 32 && data[offset + term]) ++term;
        if (term == 0 || term >= 32) return false;
        for (size_t i = 0; i < term; ++i) {
            if (data[offset + i] < 32 || data[offset + i] > 126) return false;
        }
        char *out = offset == 48 ? version_out : project_out;
        size_t capacity = offset == 48 ? version_cap : project_cap;
        if (term + 1 > capacity) return false;
        memcpy(out, data + offset, term);
        out[term] = '\0';
    }
    if (strcmp(project_out, "tab5_console") == 0) return false;

    /* Segment checksum. */
    size_t offset = 24;
    uint8_t checksum = 0xEF;
    for (uint8_t seg = 0; seg < data[1]; ++seg) {
        if (offset + 8 > length) return false;
        uint32_t seg_len;
        memcpy(&seg_len, data + offset + 4, sizeof(seg_len));
        if (seg_len % 4 || offset + 8 + seg_len > length) return false;
        for (uint32_t i = 0; i < seg_len; ++i) checksum ^= data[offset + 8 + i];
        offset += 8 + seg_len;
    }
    size_t checksum_at = offset | 15;
    if (checksum_at + 33 != length || data[checksum_at] != checksum) return false;

    /* Appended SHA-256 must match file except last 32 bytes. */
    uint8_t digest[32];
    mbedtls_sha256(data, length - 32, digest, 0);
    return memcmp(digest, data + length - 32, 32) == 0;
}

esp_err_t tab5_installer_validate(tab5_installer_package_t *package)
{
    if (!package) return ESP_ERR_INVALID_ARG;
    set_status(TAB5_INSTALLER_VALIDATING, "CHECKING FIRMWARE", 0);
    uint32_t expected_size = package->size_bytes;
    char expected_sha256[TAB5_INSTALLER_SHA256_HEX_LEN];
    copy_safe(expected_sha256, sizeof(expected_sha256), package->sha256);
    package->valid = false;
    package->size_bytes = 0;
    package->sha256[0] = '\0';

    FILE *file = fopen(package->image_path, "rb");
    if (!file) {
        set_state(TAB5_INSTALLER_FAILED, "IMAGE FILE MISSING");
        return ESP_ERR_NOT_FOUND;
    }
    if (!expected_size || expected_size > SLOT_SIZE) {
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "MANIFEST IMAGE SIZE INVALID");
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *data = malloc(expected_size);
    if (!data) {
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "OUT OF MEMORY");
        return ESP_ERR_NO_MEM;
    }
    size_t n = 0;
    while (n < expected_size) {
        size_t wanted = expected_size - n;
        if (wanted > 4096) wanted = 4096;
        size_t got = fread(data + n, 1, wanted, file);
        n += got;
        set_progress((uint8_t)((n * 20) / expected_size));
        vTaskDelay(1); // Keep display DMA and the UI responsive during SD reads.
        if (got != wanted) break;
    }
    const bool too_large = n == expected_size && fgetc(file) != EOF;
    const bool read_error = ferror(file);
    fclose(file);
    if (too_large || read_error || n < 320 || n != expected_size) {
        free(data);
        set_state(TAB5_INSTALLER_FAILED, "IMAGE SIZE INVALID");
        return ESP_ERR_INVALID_SIZE;
    }

    char project[TAB5_INSTALLER_PROJECT_NAME_MAX];
    char version[TAB5_INSTALLER_VERSION_MAX];
    if (!inspect_image(data, n, project, sizeof(project), version, sizeof(version))) {
        free(data);
        set_state(TAB5_INSTALLER_FAILED, "IMAGE VALIDATION FAILED");
        return ESP_ERR_INVALID_CRC;
    }
    if (strcmp(project, package->project) != 0) {
        free(data);
        set_state(TAB5_INSTALLER_FAILED, "PROJECT MISMATCH");
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(version, package->version) != 0) {
        free(data);
        set_state(TAB5_INSTALLER_FAILED, "VERSION MISMATCH");
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digest[32];
    mbedtls_sha256(data, n, digest, 0);
    char actual_sha256[TAB5_INSTALLER_SHA256_HEX_LEN];
    hexify(digest, actual_sha256, 32);
    if ((expected_size && expected_size != n) ||
        (expected_sha256[0] && strcmp(expected_sha256, actual_sha256))) {
        free(data);
        set_state(TAB5_INSTALLER_FAILED, "IMAGE CHANGED SINCE VALIDATION");
        return ESP_ERR_INVALID_CRC;
    }
    copy_safe(package->sha256, sizeof(package->sha256), actual_sha256);
    package->size_bytes = (uint32_t)n;
    package->valid = true;
    free(data);
    set_state(TAB5_INSTALLER_VALIDATING, "IMAGE VERIFIED");
    set_progress(20);
    return ESP_OK;
}

static bool readback_matches(const esp_partition_t *slot, FILE *file,
                             size_t length)
{
    if (length > slot->size) return false;
    uint8_t file_buf[512], flash_buf[512];
    size_t remaining = length;
    size_t offset = 0;
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(file_buf) ? remaining : sizeof(file_buf);
        if (fread(file_buf, 1, chunk, file) != chunk) return false;
        if (esp_partition_read(slot, offset, flash_buf, chunk) != ESP_OK) return false;
        if (memcmp(file_buf, flash_buf, chunk) != 0) return false;
        offset += chunk;
        remaining -= chunk;
        set_progress((uint8_t)(80 + (offset * 18) / length));
        if (offset % 4096 == 0) vTaskDelay(1);
    }
    return true;
}

static esp_err_t installer_install_impl(const tab5_installer_package_t *package, int *slot)
{
    if (slot) *slot = -1;
    if (!package || !package->image_path[0]) {
        set_state(TAB5_INSTALLER_FAILED, "INVALID PACKAGE");
        return ESP_ERR_INVALID_ARG;
    }

    tab5_installer_package_t verified = *package;
    esp_err_t result = tab5_installer_validate(&verified);
    if (result != ESP_OK) return result;
    if (!verified.valid ||
        verified.size_bytes != package->size_bytes || strcmp(verified.sha256, package->sha256)) {
        set_state(TAB5_INSTALLER_FAILED, "IMAGE CHANGED SINCE VALIDATION");
        return ESP_ERR_INVALID_CRC;
    }

    FILE *file = fopen(package->image_path, "rb");
    if (!file) {
        set_state(TAB5_INSTALLER_FAILED, "IMAGE FILE MISSING");
        return ESP_ERR_NOT_FOUND;
    }

    const esp_partition_t *target = NULL;
    result = tab5_app_boot_prepare_launch(&target);
    if (result != ESP_OK) {
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "CONSOLE / OTA MIGRATION REQUIRED");
        return result;
    }
    if (package->size_bytes > target->size) {
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "IMAGE EXCEEDS OTA SLOT");
        return ESP_ERR_INVALID_SIZE;
    }
    if (slot) *slot = (target->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) ? 1 : 0;

    set_state(TAB5_INSTALLER_WRITING, "PREPARING APP SLOT");
    esp_ota_handle_t update = 0;
    result = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &update);
    if (result != ESP_OK) {
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "ERASE FAILED");
        return result;
    }

    set_state(TAB5_INSTALLER_WRITING, "WRITING IMAGE");
    size_t written = 0;
    uint8_t buf[4096];
    uint8_t written_digest[32];
    char written_sha256[TAB5_INSTALLER_SHA256_HEX_LEN];
    mbedtls_sha256_context hash_context;
    mbedtls_sha256_init(&hash_context);
    if (mbedtls_sha256_starts(&hash_context, 0) != 0) {
        esp_ota_abort(update);
        mbedtls_sha256_free(&hash_context);
        fclose(file);
        set_state(TAB5_INSTALLER_FAILED, "HASH INIT FAILED");
        return ESP_FAIL;
    }
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
        if (written > package->size_bytes || n > package->size_bytes - written ||
            written > target->size || n > target->size - written) {
            esp_ota_abort(update);
            fclose(file);
            mbedtls_sha256_free(&hash_context);
            set_state(TAB5_INSTALLER_FAILED, "IMAGE SIZE CHANGED");
            return ESP_ERR_INVALID_SIZE;
        }
        if (mbedtls_sha256_update(&hash_context, buf, n) != 0) {
            esp_ota_abort(update);
            fclose(file);
            mbedtls_sha256_free(&hash_context);
            set_state(TAB5_INSTALLER_FAILED, "HASH UPDATE FAILED");
            return ESP_FAIL;
        }
        result = esp_ota_write(update, buf, n);
        if (result != ESP_OK) {
            esp_ota_abort(update);
            fclose(file);
            mbedtls_sha256_free(&hash_context);
            set_state(TAB5_INSTALLER_FAILED, "WRITE FAILED");
            return result;
        }
        written += n;
        if (package->size_bytes) {
            set_progress((uint8_t)(20 + (written * 60) / package->size_bytes));
        }
        vTaskDelay(1); // Small sequential erases, with a display/UI pause between writes.
    }
    const bool read_ok = !ferror(file);
    fclose(file);
    if (mbedtls_sha256_finish(&hash_context, written_digest) != 0) {
        esp_ota_abort(update);
        mbedtls_sha256_free(&hash_context);
        set_state(TAB5_INSTALLER_FAILED, "HASH FINISH FAILED");
        return ESP_FAIL;
    }
    mbedtls_sha256_free(&hash_context);
    hexify(written_digest, written_sha256, 32);
    if (strcmp(written_sha256, package->sha256) != 0) {
        esp_ota_abort(update);
        set_state(TAB5_INSTALLER_FAILED, "WRITTEN IMAGE HASH MISMATCH");
        return ESP_ERR_INVALID_CRC;
    }
    if (!read_ok || written != package->size_bytes || written == 0) {
        esp_ota_abort(update);
        set_state(TAB5_INSTALLER_FAILED, "IMAGE SIZE OR READ FAILED");
        return ESP_ERR_INVALID_STATE;
    }
    result = esp_ota_end(update);
    if (result != ESP_OK) {
        set_state(TAB5_INSTALLER_FAILED, "FINAL IMAGE CHECK FAILED");
        return result;
    }

    set_state(TAB5_INSTALLER_VERIFYING, "VERIFYING READBACK");
    file = fopen(package->image_path, "rb");
    if (!file) {
        set_state(TAB5_INSTALLER_FAILED, "REOPEN FAILED");
        return ESP_ERR_NOT_FOUND;
    }
    bool match = readback_matches(target, file, written);
    fclose(file);
    if (!match) {
        set_state(TAB5_INSTALLER_FAILED, "READBACK MISMATCH");
        return ESP_ERR_INVALID_STATE;
    }

    set_state(TAB5_INSTALLER_SELECTING, "SELECTING BOOT");
    set_progress(99);
    result = tab5_app_boot_select(target);
    if (result != ESP_OK) {
        set_state(TAB5_INSTALLER_FAILED, "BOOT SELECT FAILED");
        return result;
    }

    set_state(TAB5_INSTALLER_DONE, "INSTALL OK - REBOOT");
    set_progress(100);
    return ESP_OK;
}

esp_err_t tab5_installer_install(const tab5_installer_package_t *package,
                                 tab5_installer_status_t *status, int *slot)
{
    const esp_err_t result = installer_install_impl(package, slot);
    if (status) tab5_installer_get_status(status);
    return result;
}

esp_err_t tab5_installer_confirm_running(void)
{
    return tab5_app_boot_ready(true);
}

esp_err_t tab5_installer_return_to_factory(void)
{
    return tab5_app_boot_return_to_console();
}
