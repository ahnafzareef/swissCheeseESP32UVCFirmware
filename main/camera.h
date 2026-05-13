#pragma once

#include "esp_err.h"

#include "esp_camera.h"

esp_err_t camera_init();
void camera_deinit();

camera_fb_t *camera_get_frame();
void camera_return_frame(camera_fb_t *frame);

