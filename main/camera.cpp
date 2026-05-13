#include "camera.h"

#include "esp_camera.h"
#include "esp_log.h"

static const char *TAG = "camera";

// We are using FREENOVE ESP32-S3 PINOUT: https://docs.freenove.com/projects/fnk0086/en/latest/fnk0086/codes/tutorial/Preface.html
#define CAM_PIN_PWDN (-1)
#define CAM_PIN_RESET (-1) // software reset will be performed
#define CAM_PIN_XCLK (15)
#define CAM_PIN_SIOD (4)
#define CAM_PIN_SIOC (5)

#define CAM_PIN_Y9 (16)
#define CAM_PIN_Y8 (17)
#define CAM_PIN_Y7 (18)
#define CAM_PIN_Y6 (12)
#define CAM_PIN_Y5 (10)
#define CAM_PIN_Y4 (8)
#define CAM_PIN_Y3 (9)
#define CAM_PIN_Y2 (11)
#define CAM_PIN_VSYNC (6)
#define CAM_PIN_HREF (7)
#define CAM_PIN_PCLK (13)

static const camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_Y9,
    .pin_d6 = CAM_PIN_Y8,
    .pin_d5 = CAM_PIN_Y7,
    .pin_d4 = CAM_PIN_Y6,
    .pin_d3 = CAM_PIN_Y5,
    .pin_d2 = CAM_PIN_Y4,
    .pin_d1 = CAM_PIN_Y3,
    .pin_d0 = CAM_PIN_Y2,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    // For UVC streaming we'll use JPEG
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12, // lower is higher quality

    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,

    // Using SCCB over the specified pins above; set a default port to avoid C++ missing-field warnings.
    .sccb_i2c_port = 0,
};

esp_err_t camera_init()
{
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_camera_init failed (%s)", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != nullptr && s->id.PID == OV3660_PID)
    {
        // Common defaults for XIAO ESP32S3 Sense OV3660.
        s->set_vflip(s, 1);
        s->set_brightness(s, 1);
        s->set_saturation(s, -2);
    }

    return ESP_OK;
}

void camera_deinit()
{
    esp_camera_deinit();
}

camera_fb_t *camera_get_frame()
{
    return esp_camera_fb_get();
}

void camera_return_frame(camera_fb_t *frame)
{
    esp_camera_fb_return(frame);
}
