/*
 * UVC webcam main entrypoint (ESP-IDF).
 *
 * Next steps: integrate `usb_device_uvc` and feed it frames from `esp32-camera`.
 */

#include "esp_log.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"

#include "camera.h"
#include "status_led.h"
#include "esp_camera.h"

#include "uvc_streamer.h"

static const char *TAG = "main";
static uint8_t s_led_state = 0;

static constexpr int kStartupDelaySeconds = 5;
static constexpr size_t kUvcMaxFrameSize = 60u * 1024u;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting (UVC webcam main)");
    for(int i = 0; i < kStartupDelaySeconds; i++) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Startup delay %d seconds...", kStartupDelaySeconds - i);
    }

    status_led_init();

    auto *streamer = new UvcStreamer(320, 240, kUvcMaxFrameSize,
                                     xTaskGetCurrentTaskHandle());
    if (streamer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate streamer state");
        return;
    }

    esp_err_t err = streamer->start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uvc_streamer_start failed: %d", static_cast<int>(err));
        // We intentionally don't free store/streamer/buffer here; app is about to stop anyway.
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        return;
    }

    if (camera_init() != ESP_OK) {
        return;
    }

    uint8_t consecutive_failures = 0;

    while (true) {
        streamer->waitForFrameRequest(portMAX_DELAY);

        status_led_set(s_led_state);
        s_led_state = !s_led_state;

        camera_fb_t *pic = camera_get_frame();
        if (pic == nullptr) {
            ESP_LOGW(TAG, "esp_camera_fb_get returned NULL");
            consecutive_failures++;

            if (consecutive_failures >= 3) {
                ESP_LOGW(TAG, "Too many consecutive failures; reinitializing camera");
                camera_deinit();
                vTaskDelay(pdMS_TO_TICKS(100));
                if (camera_init() == ESP_OK) {
                    ESP_LOGI(TAG, "Camera re-init OK");
                    consecutive_failures = 0;
                } else {
                    ESP_LOGE(TAG, "Camera re-init failed; will keep retrying");
                }
            }
        } else {
            consecutive_failures = 0;
            // ESP_LOGI(TAG, "Frame: %zu bytes, %dx%d", pic->len, pic->width, pic->height);
            streamer->setNewJpegData(pic->buf, pic->len);
            camera_return_frame(pic);
        }
        // vTaskDelay(pdMS_TO_TICKS(CONFIG_BLINK_PERIOD));
    }
}

