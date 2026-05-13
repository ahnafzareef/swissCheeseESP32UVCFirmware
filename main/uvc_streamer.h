// uvc_streamer.h - USB UVC device streaming glue
//
// Bridges the game rendering loop with the USB UVC driver. Uses a producer-consumer
// pattern: the game loop waits for frame requests from the host, renders a frame,
// encodes it to JPEG, and hands it off for USB transmission.
//
// Synchronization uses FreeRTOS task notifications (for wakeup) and a binary
// semaphore (for buffer ownership).

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h" // IWYU pragma: keep (must appear before portmacro.h)
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "usb_device_uvc.h"

/**
 * Streams dynamically rendered JPEG frames over USB Video Class (UVC).
 *
 * This class uses a producer-consumer pattern to bridge a rendering loop with
 * the USB UVC driver. The producer task renders frames and encodes them to JPEG,
 * while this class handles USB transmission. Synchronization ensures the producer
 * doesn't overwrite frame data while it's being transmitted.
 *
 * Typical usage:
 *   1. Create UvcStreamer with display dimensions and max frame size
 *   2. Call start() to initialize
 *   3. In your render loop:
 *      - waitForFrameRequest() to block until host needs a frame
 *      - Render and JPEG-encode your content
 *      - setNewJpegData() to hand off the encoded frame
 */
class UvcStreamer {
public:
    /**
     * Construct a streamer for dynamically rendered content.
     *
     * @param display_width   Width of the rendered frames in pixels.
     * @param display_height  Height of the rendered frames in pixels.
     * @param max_frame_size  Maximum expected JPEG frame size in bytes.
     *                        Used to allocate internal buffers.
     * @param producer_task   Handle to the task that produces frames.
     *                        Can be set later via setProducerTask().
     */
    UvcStreamer(int display_width, int display_height, size_t max_frame_size,
                TaskHandle_t producer_task = nullptr);

    ~UvcStreamer();

    // Non-copyable
    UvcStreamer(const UvcStreamer &) = delete;
    UvcStreamer &operator=(const UvcStreamer &) = delete;

    /**
     * Start streaming frames over UVC.
     *
     * Allocates internal buffers and initializes the UVC device.
     *
     * @return ESP_OK on success, or an error code on failure.
     */
    esp_err_t start();

    /**
     * Provide a new JPEG frame to be streamed.
     *
     * Blocks until the previous frame has been transmitted, then copies the
     * new frame data into an internal buffer. Thread-safe.
     *
     * @param jpeg_data      Pointer to JPEG-encoded frame data.
     * @param jpeg_data_size Size of the JPEG data in bytes.
     */
    void setNewJpegData(const uint8_t *jpeg_data, size_t jpeg_data_size);

    /**
     * Block until the host requests a frame.
     *
     * Call this in your render loop to pace frame production with host demand.
     *
     * @param ticks_to_wait  Maximum time to wait (default: forever).
     * @return true if a frame was requested, false on timeout.
     */
    bool waitForFrameRequest(TickType_t ticks_to_wait = portMAX_DELAY);

    /**
     * Set the producer task handle.
     *
     * @param producer_task  Handle to the task that produces frames.
     */
    void setProducerTask(TaskHandle_t producer_task) { producer_task_ = producer_task; }

private:
    static void camera_stop_cb(void *cb_ctx);
    static esp_err_t camera_start_cb(uvc_format_t format, int width, int height, int rate, void *cb_ctx);
    static void camera_fb_return_cb(uvc_fb_t *fb, void *cb_ctx);
    static uvc_fb_t *camera_fb_get_cb(void *cb_ctx);

    uvc_fb_t *fbGet();
    void fbReturn(uvc_fb_t *fb);

    const int display_width_;
    const int display_height_;
    const size_t max_frame_size_;

    static constexpr uint32_t kNotifyFrameReqBit = (1u << 0);
    TaskHandle_t producer_task_ = nullptr;

    uint8_t *uvc_buffer_ = nullptr;
    uint8_t *jpeg_buf_ = nullptr;
    size_t jpeg_size_ = 0;

    // Single-buffer ownership:
    // - UVC task takes this in fbGet() and releases it in fbReturn() (after it memcpy()s into uvc_buffer).
    // - Producer task takes it in setNewJpegData(), blocking until fbReturn() has happened, so we never
    //   overwrite while UVC is reading.
    SemaphoreHandle_t jpeg_sem_ = nullptr;

    uvc_fb_t fb_{};
};

