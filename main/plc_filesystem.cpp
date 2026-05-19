#include "plc_filesystem.hpp"

#include "esp_littlefs.h"
#include "esp_log.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static const char *TAG = "PLC_FS";
static const char *PLC_FS_PARTITION_LABEL = "storage";

enum class DirEnsureResult {
    Existing,
    Created,
    Error,
};

static DirEnsureResult ensure_dir(const char *path)
{
    struct stat st = {};

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGI(TAG, "Directory exists: %s", path);
            return DirEnsureResult::Existing;
        }

        ESP_LOGE(TAG, "Path exists but is not a directory: %s", path);
        return DirEnsureResult::Error;
    }

    if (errno != ENOENT) {
        ESP_LOGE(TAG, "stat failed for %s: errno=%d (%s)", path, errno, strerror(errno));
        return DirEnsureResult::Error;
    }

    if (mkdir(path, 0775) != 0) {
        ESP_LOGE(TAG, "mkdir failed for %s: errno=%d (%s)", path, errno, strerror(errno));
        return DirEnsureResult::Error;
    }

    ESP_LOGI(TAG, "Created directory: %s", path);
    return DirEnsureResult::Created;
}

esp_err_t plc_filesystem_mount(void)
{
    ESP_LOGI(TAG, "Mounting LittleFS at %s", PLC_FS_MOUNT_POINT);

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = PLC_FS_MOUNT_POINT;
    conf.partition_label = PLC_FS_PARTITION_LABEL;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0;
    size_t used = 0;
    ret = esp_littlefs_info(PLC_FS_PARTITION_LABEL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted: total=%u used=%u free=%u",
                 (unsigned)total,
                 (unsigned)used,
                 (unsigned)(total - used));
    } else {
        ESP_LOGW(TAG, "esp_littlefs_info failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}

esp_err_t plc_filesystem_ensure_standard_dirs(void)
{
    static const char *dirs[] = {
        PLC_FS_MOUNT_POINT "/scripts",
        PLC_FS_MOUNT_POINT "/libs",
        PLC_FS_MOUNT_POINT "/hmi",
        PLC_FS_MOUNT_POINT "/ladder",
        PLC_FS_MOUNT_POINT "/tags",
        PLC_FS_MOUNT_POINT "/config",
        PLC_FS_MOUNT_POINT "/logs",
        PLC_FS_MOUNT_POINT "/backups",
    };

    unsigned existing_count = 0;
    unsigned created_count = 0;
    unsigned error_count = 0;

    for (const char *dir : dirs) {
        switch (ensure_dir(dir)) {
        case DirEnsureResult::Existing:
            existing_count++;
            break;

        case DirEnsureResult::Created:
            created_count++;
            break;

        case DirEnsureResult::Error:
            error_count++;
            break;
        }
    }

    ESP_LOGI(TAG,
             "Standard PLC directories checked: existing=%u created=%u errors=%u",
             existing_count,
             created_count,
             error_count);

    return (error_count == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t plc_filesystem_init(void)
{
    esp_err_t ret = plc_filesystem_mount();
    if (ret != ESP_OK) {
        return ret;
    }

    ret = plc_filesystem_ensure_standard_dirs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "One or more standard PLC directories could not be created");
        return ret;
    }

    ESP_LOGI(TAG, "PLC filesystem ready");
    return ESP_OK;
}
