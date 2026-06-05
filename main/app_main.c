#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "app/app_state.h"
#include "app/app_tasks.h"
#include "board/pilab_board.h"
#include "hmi/hmi_lvgl_runtime.h"
#include "hmi/hmi_tag_db.h"
#include "hmi/hmi_script_engine.h"
#include "hmi/hmi_memory_diag.h"
#include "panel/panel_storage.h"
#include "panel/panel_wifi.h"
#include "mqtt/panel_mqtt.h"
#include "web/panel_web_server.h"
#include "pilab_panel_version.h"

static const char *TAG = "PiLab_Panel";


static void configure_task_watchdog_for_debug(void)
{
    // Diagnostic build: allow larger LVGL JSON-driven screen builds to run long
    // enough to determine whether they eventually complete or truly hang.
    // This is intentionally not a final real-time/runtime policy.
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    };

    esp_err_t ret = esp_task_wdt_reconfigure(&twdt_config);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = esp_task_wdt_init(&twdt_config);
    }

    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "Diagnostic Task WDT timeout set to 10000 ms");
    } else {
        ESP_LOGW(TAG, "Could not change Task WDT timeout: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting PiLab Panel - multi-board LVGL HMI runtime");
    ESP_LOGI(TAG, "Build marker: %s (%s)", PILAB_PANEL_VERSION, PILAB_PANEL_BUILD_NOTE);
    hmi_memory_diag_checkpoint("boot: app_main start");

    configure_task_watchdog_for_debug();

    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    ESP_ERROR_CHECK(app_state_init());
    ESP_ERROR_CHECK(panel_storage_init());
    ESP_ERROR_CHECK(hmi_tag_db_init());
    hmi_memory_diag_checkpoint("boot: storage/state/tags ready");

    // Bring up LCD/touch/LVGL first so the ESP-IDF peripheral allocation path stays
    // close to the proven baseline, but keep the backlight dark while the boot
    // script compiles.  The HMI layout is not created until AngelScript is ready,
    // so the operator never sees the real HMI appear and then disappear during boot.
    lv_display_t *display = NULL;
    lv_indev_t *touch = NULL;
    ESP_ERROR_CHECK(pilab_board_display_init(&display, &touch));
    ESP_ERROR_CHECK(pilab_board_set_backlight(false));
    hmi_memory_diag_checkpoint("boot: lcd/lvgl initialized, backlight off");

    // Compile AngelScript before exposing the HMI. This still uses a dedicated
    // PSRAM-stack task internally, but app_main waits for completion so the first
    // visible HMI screen is the final runtime screen, not a temporary compile page.
    esp_err_t script_ret = hmi_script_engine_boot_compile_pre_hmi(10000);
    if (script_ret != ESP_OK) {
        ESP_LOGE(TAG, "AngelScript startup failed before HMI display: %s", hmi_script_engine_last_error());
    }
    hmi_memory_diag_checkpoint(script_ret == ESP_OK ? "boot: script compiled pre-HMI" : "boot: script compile failed pre-HMI");

    ESP_ERROR_CHECK(hmi_lvgl_runtime_create(display));
    if (!hmi_lvgl_runtime_wait_until_ready(5000)) {
        ESP_LOGW(TAG, "Initial HMI layout did not report ready before backlight enable");
    }
    hmi_memory_diag_checkpoint("boot: initial HMI layout ready");

    // Run OnPanelStart after the HMI runtime exists but while the backlight is
    // still off. This lets startup scripts initialize tags or select a startup
    // screen without visibly disrupting the panel.
    if (script_ret == ESP_OK) {
        esp_err_t start_ret = hmi_script_engine_call("OnPanelStart");
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "OnPanelStart failed at boot: %s", hmi_script_engine_last_error());
        }
        hmi_lvgl_runtime_update();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    hmi_memory_diag_checkpoint("boot: OnPanelStart complete");

    ESP_ERROR_CHECK(pilab_board_set_backlight(true));
    hmi_memory_diag_checkpoint("boot: backlight on");

    ESP_ERROR_CHECK(panel_wifi_start());
    hmi_memory_diag_checkpoint("boot: WiFi service started");

    // Initialize the MQTT config/status service. If the saved active broker has
    // autoConnect enabled, the MQTT module starts a small deferred-start task
    // that waits for STA DHCP/IP before connecting. Starting MQTT during AP+STA
    // bring-up caused DNS failures and instability in earlier builds.
    ESP_ERROR_CHECK(panel_mqtt_init());
    ESP_LOGI(TAG, "MQTT service initialized; auto-start is deferred until WiFi STA has IP");
    hmi_memory_diag_checkpoint("boot: MQTT service initialized");
    ESP_ERROR_CHECK(panel_web_server_start());
    hmi_memory_diag_checkpoint("boot: HTTP server started");
    ESP_ERROR_CHECK(app_tasks_start());
    hmi_memory_diag_checkpoint("boot: app tasks started");

    ESP_LOGI(TAG, "PiLab Panel is running. Connect to WiFi SSID 'PiLab-Panel' and open http://192.168.50.1");
}
