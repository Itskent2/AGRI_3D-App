/**
 * @file agri3d_camera.cpp
 * @brief Camera init, FPM stream task, and captureFrameAtPosition()
 * implementation.
 */

#include "agri3d_camera.h"
#include "agri3d_config.h"
#include "agri3d_grbl.h"
#include "agri3d_network.h"
#include "agri3d_sd.h"
#include "agri3d_state.h"
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// ============================================================================
// CAMERA INITIALISATION
// ============================================================================

bool cameraInit() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2;
  config.pin_d1 = CAM_Y3;
  config.pin_d2 = CAM_Y4;
  config.pin_d3 = CAM_Y5;
  config.pin_d4 = CAM_Y6;
  config.pin_d5 = CAM_Y7;
  config.pin_d6 = CAM_Y8;
  config.pin_d7 = CAM_Y9;
  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;
  config.pin_sccb_sda = CAM_SIOD;
  config.pin_sccb_scl = CAM_SIOC;
  config.pin_pwdn = CAM_PWDN;
  config.pin_reset = CAM_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // UXGA (1600×1200) — highest quality for plant-map stills
  // Requires PSRAM to be enabled in board settings
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10; // 0-63, lower = higher quality
    config.fb_count = 2;      // Double buffer for smoother stream
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Fall back to QVGA if no PSRAM to prevent DRAM exhaustion!
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    Serial.println("[CAM] WARNING: No PSRAM — falling back to QVGA to save memory");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] ERROR: Init failed (0x%x)\n", err);
    return false;
  }

  // Fine-tune sensor settings for agricultural (outdoor) scenes
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0); // -2 to 2
    s->set_contrast(s, 0);   // -2 to 2
    s->set_saturation(s, 1); // Boost green for vegetation
    s->set_sharpness(s, 1);
    s->set_whitebal(s, 1);      // Auto white balance on
    s->set_awb_gain(s, 1);      // AWB gain on
    s->set_exposure_ctrl(s, 1); // Auto exposure on
    s->set_aec2(s, 1);          // Better AEC algorithm
  }

  Serial.println("[CAM] Camera OK (UXGA JPEG)");

  // Launch stream task on Core 0 to offload it from the main network/grbl loop on Core 1
  xTaskCreatePinnedToCore(streamTask, "streamTask", 8192, nullptr, 2, nullptr,
                          0);
  return true;
}

// ============================================================================
// FPM STREAM TASK
// ============================================================================

void streamTask(void * /*pvParameters*/) {
  while (true) {
    // Only stream if: flag set, camera not locked, and a client is connected
    if (sysState.isStreaming && isCameraAvailable() &&
        sysState.flutter == FLUTTER_CONNECTED) {

      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("[CAM] [STREAM] esp_camera_fb_get() failed! Running watchdog...");
        cameraSanityCheck();
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }

      // Send to singleton client only
      if (activeClientNum >= 0) {
        // Safe Handoff: only pass to network task if previous frame is gone
        if (sysState.pendingFrame == nullptr) {
          sysState.pendingFrame = (uint8_t*)malloc(fb->len);
          if (sysState.pendingFrame) {
            memcpy(sysState.pendingFrame, fb->buf, fb->len);
            sysState.pendingFrameLen = fb->len;
            sysState.pendingFrameClient = activeClientNum;
          }
        } else {
          // Network is busy — dropping frame to keep loop responsive
        }
      }

      esp_camera_fb_return(fb);

      // Convert FPM to delay: delay_ms = 60000 / fpm
      // fpm is already clamped to [1, 300] by setFpm()
      uint32_t delayMs = 60000UL / (uint32_t)sysState.fpm;
      vTaskDelay(pdMS_TO_TICKS(delayMs));

    } else {
      // Nothing to stream — sleep to free the CPU
      vTaskDelay(pdMS_TO_TICKS(STREAM_IDLE_DELAY));
    }
  }
}

// ============================================================================
// CORE CAPTURE HELPER
// ============================================================================

bool captureFrameAtPosition(uint8_t clientNum, int idx, int total,
                            float targetX, float targetY) {
  // ── 1. Send G-code move (skip Z — axis broken) ────────────────────────
  char gcode[48];
  snprintf(gcode, sizeof(gcode), "G0 X%.2f Y%.2f F%d", targetX, targetY,
           GRBL_DEFAULT_FEEDRATE);
  NanoSerial.println(gcode);
  Serial.printf("[CAM] Frame %d/%d → moving to X=%.1f Y=%.1f\n", idx, total,
                targetX, targetY);

  // ── 2. Wait for GRBL Idle ────────────────────────────────────────────────
  if (!waitForGrblIdle(SCAN_MOVE_TIMEOUT_MS)) {
    Serial.printf("[CAM] Frame %d: move timeout — skipping capture\n", idx);
    return false;
  }

  // ── 3. Stabilisation pause ────────────────────────────────────────────────
  delay(150);

  // ── 4. Capture JPEG ─────────────────────────────────────────────────────
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.printf("[CAM] Frame %d: esp_camera_fb_get() failed\n", idx);
    return false;
  }

  // ── 5. Timestamp + metadata ─────────────────────────────────────────────
  time_t now = time(nullptr);
  struct tm *tm_ = localtime(&now);
  char dateStr[12], timeStr[10];
  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", tm_->tm_year + 1900,
           tm_->tm_mon + 1, tm_->tm_mday);
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", tm_->tm_hour,
           tm_->tm_min, tm_->tm_sec);

  // Ground coverage at current Z (using OV5640 FOV constants)
  // When Z is fixed/unknown, use a nominal 200mm height for reference
  float nominalZ = 200.0f; // TODO: use actual Z when axis is repaired
  float groundW = 2.0f * nominalZ * tanf(CAM_FOV_H_DEG * 0.5f * M_PI / 180.0f);
  float groundH = 2.0f * nominalZ * tanf(CAM_FOV_V_DEG * 0.5f * M_PI / 180.0f);

  // ── 6. Always save to SD (works even when Flutter is offline) ────────────
  char sdPath[96] = {0};
#if HW_SD_CONNECTED
  sdSaveImage(fb->buf, fb->len, SD_IMG_PLANTMAP, 'f', idx, sysState.grblX,
              sysState.grblY, now, sdPath);
#endif

  // ── 7. Send JSON metadata header to Flutter (if connected) ────────────
  if (sysState.flutter == FLUTTER_CONNECTED) {
    StaticJsonDocument<256> meta;
    meta["evt"] = "FRAME_META";
    meta["idx"] = idx;
    meta["total"] = total;
    meta["x"] = serialized(String(sysState.grblX, 2));
    meta["y"] = serialized(String(sysState.grblY, 2));
    meta["z"] = serialized(String(sysState.grblZ, 2));
    meta["ts"] = (long)now;
    meta["date"] = dateStr;
    meta["time"] = timeStr;
    meta["groundW"] = (int)groundW; // mm — Flutter uses for stitch alignment
    meta["groundH"] = (int)groundH;
    meta["fovH"] = CAM_FOV_H_DEG;
    meta["fovV"] = CAM_FOV_V_DEG;
    if (sdPath[0])
      meta["sdPath"] = sdPath;
    String metaStr;
    serializeJson(meta, metaStr);
    webSocket.sendTXT(clientNum, metaStr);

    // ── 8. Send raw JPEG immediately after metadata (Safe Handoff) ────────────
    if (sysState.pendingFrame != nullptr) {
        free(sysState.pendingFrame); // Clean up if something was stuck
    }
    sysState.pendingFrame = (uint8_t*)malloc(fb->len);
    if (sysState.pendingFrame) {
        memcpy(sysState.pendingFrame, fb->buf, fb->len);
        sysState.pendingFrameLen = fb->len;
        sysState.pendingFrameClient = clientNum;
    }
  }

  Serial.printf("[CAM] Frame %d: %s %s (%.1f,%.1f) %uB%s\n", idx, dateStr,
                timeStr, sysState.grblX, sysState.grblY, fb->len,
                sdPath[0] ? " [SD]" : "");

  esp_camera_fb_return(fb);
  return true;
                            }
                            
  // =========================================================================
  // TODO(Luna): AI Hook — uncomment when ready
  // After esp_camera_fb_get(), before return:
  //   aiProcessFrame(fb->buf, fb->len, sysState.grblX, sysState.grblY);
// =========================================================================

void cameraSanityCheck() {
    // If the system has been in an "invalid" state for too long
    static unsigned long lastSuccess = millis();
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb != NULL) {
        lastSuccess = millis();
        esp_camera_fb_return(fb);
    }
    
    if (millis() - lastSuccess > 10000) { 
        Serial.println("[WATCHDOG] Camera unresponsive for 10s. Attempting reset...");
        esp_camera_deinit();
        delay(100);
        cameraInit(); // Re-init
        lastSuccess = millis();
    }
}
