#include "car_control/car_service.h"
#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "system_info.h"

// Motor control pins (defined in config.h, but we need them early)
// Try to include board `config.h` if available so board-level overrides take effect.
#if defined(__has_include)
#  if __has_include("config.h")
#    include "config.h"
#  endif
#endif

// Provide guarded defaults if the board did not define MOTOR_* pins
#ifndef MOTOR_LF_GPIO
#define MOTOR_LF_GPIO GPIO_NUM_12   // Left Forward
#define MOTOR_LB_GPIO GPIO_NUM_13  // Left Backward
#define MOTOR_RF_GPIO GPIO_NUM_14  // Right Forward
#define MOTOR_RB_GPIO GPIO_NUM_21  // Right Backward (was GPIO_NUM_3)
#endif

#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if CONFIG_CAR_BLE_CONTROL
    // BLE and motor safety work without Wi-Fi, cloud activation or audio startup.
    if (!car::Start()) ESP_LOGE(TAG, "Car service unavailable; motor commands remain disabled");
#endif

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
