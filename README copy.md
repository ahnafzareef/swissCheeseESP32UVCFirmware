## webcam — ESP32‑S3 USB UVC “real camera” (esp32-camera → UVC)

[![YouTube Video](docs/images/thumbnail-with-play-button.jpg)](https://www.youtube.com/watch?v=zhTTmRQLNws)

This demo turns an **ESP32‑S3** into a **USB UVC camera device** that streams **live JPEG frames** captured from a camera sensor using Espressif’s [`esp32-camera`](https://components.espressif.com/components/espressif/esp32-camera).

It uses Espressif’s `usb_device_uvc` component (see upstream: [`usb_device_uvc` in esp-iot-solution](https://github.com/espressif/esp-iot-solution/tree/36d8130e8e880720108de2c31ce0779827b1bcd9/components/usb/usb_device_uvc)).

### What it does

- Enumerates as a **USB UVC camera** when plugged into a host
- Initializes the camera as **JPEG QVGA (320×240)** and streams frames as **MJPEG over UVC**
- Paces capture to host demand (captures a new frame each time the host requests one)
- Toggles a simple **status LED** on each frame request

### Hardware

- **ESP32‑S3** with native USB (USB‑OTG) wired to a USB connector on your dev board
- A supported camera module for `esp32-camera`

Notes:

- The default pin mapping in `main/camera.cpp` is for **Seeed Studio XIAO ESP32S3 Sense** (OV3660/OV2640-style DVP wiring). If you’re using a different board/module, you’ll need to adjust the pin definitions and possibly the sensor tuning.
- Plug the board into your host via the **USB (native) port** used for UVC (not a separate UART bridge).

### Where the code is

- **`main/main.cpp`**: App entrypoint; waits for UVC frame requests and forwards camera JPEG frames
- **`main/camera.*`**: `esp32-camera` init + frame get/return (currently configured for JPEG QVGA)
- **`main/uvc_streamer.*`**: UVC glue (wakes the producer on host requests; hands MJPEG frames to USB)
- **`main/status_led.*`**: Status LED GPIO helper

### Configure

In `idf.py menuconfig`:

- **`Example Configuration → Blink GPIO number`**: GPIO used for the status LED (defaults to 8)
- **UVC component settings** (resolution / frame rate / bulk vs isochronous), depending on your `usb_device_uvc` version

Important:

- This demo is hard-wired to **320×240** in `main/main.cpp` (`UvcStreamer(320, 240, ...)`) and `main/camera.cpp` (`FRAMESIZE_QVGA`). If you change one, update the other and the UVC component framesize to match.

### Build / flash / view

```bash
idf.py set-target esp32s3
idf.py menuconfig   # optional
idf.py build flash monitor
```

On the host, open any webcam viewer and select the new camera device.

### Troubleshooting

- **No video / UVC device doesn’t appear**: make sure you’re using the board’s **native USB** port/cable and that UVC is enabled/configured in menuconfig for your target.
- **Camera init fails**: confirm the camera module wiring/pinout matches `main/camera.cpp` (XIAO Sense by default).
- **Corrupt / missing frames**: lower camera JPEG quality (increase the numeric value in `main/camera.cpp`), or increase the max frame size budget in `main/main.cpp` (default ~60 KiB).
