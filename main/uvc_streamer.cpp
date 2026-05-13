#include "uvc_streamer.h"

#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "uvc_streamer";

UvcStreamer::UvcStreamer(int display_width, int display_height, size_t max_frame_size,
                         TaskHandle_t producer_task)
    : display_width_(display_width),
      display_height_(display_height),
      max_frame_size_(max_frame_size),
      producer_task_(producer_task)
{
}

UvcStreamer::~UvcStreamer()
{
    free(uvc_buffer_);
    uvc_buffer_ = nullptr;
    free(jpeg_buf_);
    jpeg_buf_ = nullptr;
}

bool UvcStreamer::waitForFrameRequest(TickType_t ticks_to_wait)
{
    if (producer_task_ == nullptr) {
        return false;
    }
    // xTaskNotifyWait: Block until another task sends a notification via xTaskNotify.
    // This is more efficient than polling and uses less CPU than a semaphore for
    // simple task wakeup scenarios. The notification value lets us distinguish
    // between different event types if needed.
    uint32_t value = 0;
    const BaseType_t ok = xTaskNotifyWait(0, kNotifyFrameReqBit, &value, ticks_to_wait);
    return (ok == pdTRUE) && ((value & kNotifyFrameReqBit) != 0);
}

void UvcStreamer::camera_stop_cb(void *cb_ctx)
{
    (void)cb_ctx;
    ESP_LOGI(TAG, "camera stop");
}

esp_err_t UvcStreamer::camera_start_cb(uvc_format_t format, int width, int height, int rate, void *cb_ctx)
{
    (void)cb_ctx;
    ESP_LOGI(TAG, "camera start: format=%d width=%d height=%d rate=%d", (int)format, width, height, rate);
    return ESP_OK;
}

void UvcStreamer::camera_fb_return_cb(uvc_fb_t *fb, void *cb_ctx)
{
    auto *self = static_cast<UvcStreamer *>(cb_ctx);
    if (self == nullptr) {
        return;
    }
    self->fbReturn(fb);
}

uvc_fb_t *UvcStreamer::camera_fb_get_cb(void *cb_ctx)
{
    auto *self = static_cast<UvcStreamer *>(cb_ctx);
    if (self == nullptr) {
        return nullptr;
    }
    return self->fbGet();
}

uvc_fb_t *UvcStreamer::fbGet()
{
    // Host is requesting a frame; wake the producer to draw/encode a new one.
    // xTaskNotify: Send a notification to the producer task, setting a bit in its
    // notification value. This wakes the producer if it's blocked in xTaskNotifyWait.
    if (producer_task_ != nullptr) {
        xTaskNotify(producer_task_, kNotifyFrameReqBit, eSetBits);
    }

    if (jpeg_buf_ == nullptr || jpeg_size_ == 0 || jpeg_sem_ == nullptr) {
        return nullptr;
    }

    // xSemaphoreTake: Acquire exclusive ownership of the JPEG buffer.
    // Binary semaphores implement mutual exclusion: only one task can "take" it
    // at a time; others block until it's "given" back. We use a short timeout
    // to avoid stalling the UVC task if the producer is mid-copy.
    if (xSemaphoreTake(jpeg_sem_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return nullptr;
    }

    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());

    // It's possible we raced with initialization and size is still 0.
    if (jpeg_size_ == 0) {
        xSemaphoreGive(jpeg_sem_);
        return nullptr;
    }

    memset(&fb_, 0, sizeof(fb_));
    fb_.buf = jpeg_buf_;
    fb_.len = jpeg_size_;
    fb_.width = display_width_;
    fb_.height = display_height_;
    fb_.format = UVC_FORMAT_JPEG;
    fb_.timestamp.tv_sec = now_us / 1000000ULL;
    fb_.timestamp.tv_usec = now_us % 1000000ULL;

    // IMPORTANT: we intentionally do NOT give jpeg_sem_ here. The UVC task will memcpy()
    // from fb_.buf and then call fbReturn(), which releases the semaphore.
    return &fb_;
}

void UvcStreamer::fbReturn(uvc_fb_t *fb)
{
    if (fb == nullptr || fb->buf == nullptr) {
        return;
    }
    if (jpeg_sem_ == nullptr) {
        return;
    }
    // Release ownership so the producer can update, or a new frame can be read.
    xSemaphoreGive(jpeg_sem_);
}

esp_err_t UvcStreamer::start()
{
    if (max_frame_size_ == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Allocate UVC transfer buffer
    uvc_buffer_ = static_cast<uint8_t *>(malloc(max_frame_size_));
    if (uvc_buffer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate UVC buffer (%zu bytes)", max_frame_size_);
        return ESP_ERR_NO_MEM;
    }

    // Allocate JPEG staging buffer
    jpeg_buf_ = static_cast<uint8_t *>(malloc(max_frame_size_));
    if (jpeg_buf_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer (%zu bytes)", max_frame_size_);
        return ESP_ERR_NO_MEM;
    }
    jpeg_size_ = 0;

    jpeg_sem_ = xSemaphoreCreateBinary();
    if (jpeg_sem_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    // Mark buffer as initially free.
    xSemaphoreGive(jpeg_sem_);

    uvc_device_config_t config = {
        .uvc_buffer = uvc_buffer_,
        .uvc_buffer_size = max_frame_size_,
        .start_cb = camera_start_cb,
        .fb_get_cb = camera_fb_get_cb,
        .fb_return_cb = camera_fb_return_cb,
        .stop_cb = camera_stop_cb,
        .cb_ctx = this,
    };

    esp_err_t err = uvc_device_config(0, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uvc_device_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uvc_device_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uvc_device_init failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

void UvcStreamer::setNewJpegData(const uint8_t *jpeg_data, size_t jpeg_data_size)
{
    if (jpeg_data == nullptr || jpeg_data_size == 0) {
        return;
    }
    if (jpeg_buf_ == nullptr || jpeg_sem_ == nullptr || max_frame_size_ == 0 || jpeg_data_size > max_frame_size_) {
        return;
    }

    // Block until the UVC task has finished reading the current buffer (fbReturn()).
    // portMAX_DELAY means wait forever - the producer won't proceed until it
    // safely owns the buffer. This prevents overwriting data while UVC is reading.
    xSemaphoreTake(jpeg_sem_, portMAX_DELAY);

    memcpy(jpeg_buf_, jpeg_data, jpeg_data_size);
    jpeg_size_ = jpeg_data_size;

    xSemaphoreGive(jpeg_sem_);
}
