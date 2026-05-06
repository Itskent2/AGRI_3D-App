/**
 * @file agri3d_ai.cpp
 * @brief Edge Impulse FOMO weed detection integration for AGRI-3D.
 *
 * Connects the AI placeholder to the real Edge Impulse "Weed Detection"
 * model (Project #923987, Impulse #2).
 *
 * Model details:
 *   - Architecture: FOMO (Faster Objects, More Objects)
 *   - Input:        96×96 RGB image (INT8 quantized)
 *   - Output:       12×12 grid, 2 classes ("crop", "weed")
 *   - Arena size:   ~290 KB (allocated in PSRAM on ESP32-S3)
 *   - Threshold:    0.5 confidence
 *
 * Flow:
 *   1. aiInit()          — called once in setup() to verify PSRAM & model
 *   2. aiAnalyzeJpeg()   — primary API: takes raw JPEG from camera,
 *                           decodes to RGB, resizes to 96×96, runs inference
 *   3. aiAnalyzeFrame()  — lower-level: takes pre-processed 96×96 RGB buffer
 */

#include "agri3d_ai.h"
#include "agri3d_state.h"
#include "agri3d_config.h"
#include "../core/agri3d_logger.h"

// Edge Impulse model variables (pulls in the compiled TFLite graph)
#include "model-parameters/model_variables.h"

#include <esp_camera.h>
#include <esp_heap_caps.h>

// ── Module state ──────────────────────────────────────────────────────────
static bool _aiReady = false;

// ── PSRAM-aware allocator for Edge Impulse arena ─────────────────────────
static void* ei_psram_alloc(size_t size, size_t alignment) {
    // Edge Impulse needs aligned allocations for the TFLite arena
    void* ptr = heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_SPIRAM);
    if (!ptr) {
        // Fallback to internal RAM if PSRAM fails
        ptr = heap_caps_aligned_alloc(alignment, size, MALLOC_CAP_DEFAULT);
    }
    return ptr;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void aiInit() {
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Initializing Edge Impulse Weed Detection...");

    // Verify PSRAM is available (model needs ~290KB arena)
    size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] PSRAM free: %d KB (arena needs ~%d KB)",
            psramFree / 1024, EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE / 1024);

    if (psramFree < EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE) {
        AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] ✘ Not enough PSRAM for model arena!");
        _aiReady = false;
        return;
    }

    // Log model metadata
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Project: %s (#%d v%d)",
            EI_CLASSIFIER_PROJECT_NAME,
            EI_CLASSIFIER_PROJECT_ID,
            EI_CLASSIFIER_PROJECT_DEPLOY_VERSION);
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Input: %dx%d RGB | Labels: %d | FOMO grid: 12x12",
            EI_CLASSIFIER_INPUT_WIDTH,
            EI_CLASSIFIER_INPUT_HEIGHT,
            EI_CLASSIFIER_LABEL_COUNT);

    // Print class labels
    for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Class %d: \"%s\"",
                i, ei_classifier_inferencing_categories[i]);
    }

    _aiReady = true;
    AgriLog(TAG_SYSTEM, LEVEL_SUCCESS, "[AI] ✓ Edge Impulse Engine Ready");
}

bool aiIsReady() {
    return _aiReady;
}

// ============================================================================
// RAW RGB INFERENCE (96×96×3 = 27648 bytes input)
// ============================================================================

/**
 * @brief Signal callback for Edge Impulse — reads from a flat RGB888 buffer.
 */
typedef struct {
    uint8_t* buffer;
    size_t   bufferLen;
} ei_rgb_signal_ctx_t;

static int ei_rgb_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    // Edge Impulse expects pixel values normalized to 0.0–1.0 for image DSP.
    // However, with INT8 quantized models, the SDK handles this internally.
    // We provide raw RGB values as floats here.
    ei_rgb_signal_ctx_t* ctx = (ei_rgb_signal_ctx_t*)ei_read_signal_user_data;
    
    for (size_t i = 0; i < length; i++) {
        if (offset + i < ctx->bufferLen) {
            out_ptr[i] = (float)ctx->buffer[offset + i];
        } else {
            out_ptr[i] = 0.0f;
        }
    }
    return 0;
}

// Global context pointer for the signal callback
static ei_rgb_signal_ctx_t _signalCtx;
static void* ei_read_signal_user_data = nullptr;

static int ei_rgb_signal_get_data_wrapper(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        if (offset + i < _signalCtx.bufferLen) {
            out_ptr[i] = (float)_signalCtx.buffer[offset + i];
        } else {
            out_ptr[i] = 0.0f;
        }
    }
    return 0;
}

AiResult aiAnalyzeFrame(uint8_t* buf, size_t len) {
    AiResult res = {};
    res.foundPlant = false;
    res.foundWeed = false;
    res.confidence = 0.0f;
    res.xOffset = 0;
    res.yOffset = 0;
    res.cropCount = 0;
    res.weedCount = 0;
    res.totalDetections = 0;

    if (!_aiReady) {
        AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] Engine not initialized");
        return res;
    }

    if (buf == nullptr || len == 0) return res;

    // Expected: 96 * 96 * 3 = 27648 bytes
    if (len != EI_CLASSIFIER_NN_INPUT_FRAME_SIZE) {
        AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] Buffer size mismatch: got %d, expected %d",
                len, EI_CLASSIFIER_NN_INPUT_FRAME_SIZE);
        return res;
    }

    // ── Set up the Edge Impulse signal ──
    _signalCtx.buffer = buf;
    _signalCtx.bufferLen = len;

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &ei_rgb_signal_get_data_wrapper;

    // ── Run inference ──
    ei_impulse_result_t result = {};
    unsigned long t0 = millis();

    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false /* debug */);

    unsigned long elapsed = millis() - t0;

    if (err != EI_IMPULSE_OK) {
        AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] Inference failed (error %d)", err);
        return res;
    }

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Inference: %lu ms | DSP: %d ms | NN: %d ms",
            elapsed,
            (int)result.timing.dsp,
            (int)result.timing.classification);

    // ── Parse FOMO bounding boxes ──
    float highestWeedConf = 0.0f;
    int highestWeedX = 0, highestWeedY = 0;
    int detIdx = 0;

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];

        // Skip low-confidence detections
        if (bb.value < EI_CLASSIFIER_OBJECT_DETECTION_THRESHOLD) continue;

        // Store detection
        if (detIdx < EI_CLASSIFIER_OBJECT_DETECTION_COUNT) {
            res.detections[detIdx].label      = bb.label;
            res.detections[detIdx].confidence  = bb.value;
            res.detections[detIdx].x           = bb.x;
            res.detections[detIdx].y           = bb.y;
            res.detections[detIdx].width       = bb.width;
            res.detections[detIdx].height      = bb.height;
            detIdx++;
        }

        // Classify
        if (strcmp(bb.label, "weed") == 0) {
            res.foundWeed = true;
            res.weedCount++;
            if (bb.value > highestWeedConf) {
                highestWeedConf = bb.value;
                // Convert FOMO grid position to pixel offset from center
                // FOMO grid is 12×12 over 96×96 image → 8px per cell
                highestWeedX = (bb.x * 8 + 4) - 48; // Center = 48
                highestWeedY = (bb.y * 8 + 4) - 48;
            }
        } else if (strcmp(bb.label, "crop") == 0) {
            res.foundPlant = true;
            res.cropCount++;
        }

        AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] → %s (%.1f%%) at grid [%d,%d]",
                bb.label, bb.value * 100.0f, bb.x, bb.y);
    }

    res.totalDetections = detIdx;
    res.confidence = highestWeedConf;
    res.xOffset = highestWeedX;
    res.yOffset = highestWeedY;

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "[AI] Result: %d crops, %d weeds detected",
            res.cropCount, res.weedCount);

    return res;
}

// ============================================================================
// JPEG CONVENIENCE WRAPPER
// ============================================================================

AiResult aiAnalyzeJpeg(uint8_t* jpegBuf, size_t jpegLen) {
    AiResult res = {};
    res.foundPlant = false;
    res.foundWeed = false;
    res.confidence = 0.0f;
    res.xOffset = 0;
    res.yOffset = 0;
    res.cropCount = 0;
    res.weedCount = 0;
    res.totalDetections = 0;

    if (!_aiReady || jpegBuf == nullptr || jpegLen == 0) {
        return res;
    }

    // ── Step 1: Decode JPEG to RGB888 using ESP32's hardware JPEG decoder ──
    // We need a 96×96 RGB image. The camera gives us a JPEG at whatever
    // resolution is configured. We decode then downsample.

    // Allocate RGB buffer in PSRAM (96×96×3 = 27648 bytes)
    const int targetW = EI_CLASSIFIER_INPUT_WIDTH;   // 96
    const int targetH = EI_CLASSIFIER_INPUT_HEIGHT;  // 96
    const size_t rgbSize = targetW * targetH * 3;

    uint8_t* rgbBuf = (uint8_t*)heap_caps_malloc(rgbSize, MALLOC_CAP_SPIRAM);
    if (!rgbBuf) {
        AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] Failed to allocate RGB buffer (%d bytes)", rgbSize);
        return res;
    }

    // Use ESP32's built-in JPEG decoder (from esp_camera / ROM)
    // fmt2rgb888 decodes JPEG to RGB888 at original resolution.
    // For efficiency, we first try to get a small frame from the camera.

    // Decode JPEG → full-size RGB
    // Note: This temporarily allocates a large buffer for the full image.
    // On ESP32-S3 with 8MB PSRAM this is fine.
    bool decoded = fmt2rgb888(jpegBuf, jpegLen, PIXFORMAT_JPEG, rgbBuf);

    if (!decoded) {
        // fmt2rgb888 only works if output buffer matches the JPEG dimensions.
        // Since our rgbBuf is 96×96 but JPEG might be larger, we need a two-step:
        // 1) Decode to full size, 2) Downsample to 96×96

        // Get the JPEG dimensions from the header
        // For now, decode to a temp buffer and bilinear downsample
        
        // Allocate full-size decode buffer (QVGA = 320×240×3 = 230KB)
        // The camera should be set to a low resolution for AI captures
        const int srcW = 320;  // Assume QVGA for AI captures
        const int srcH = 240;
        size_t fullSize = srcW * srcH * 3;
        
        uint8_t* fullBuf = (uint8_t*)heap_caps_malloc(fullSize, MALLOC_CAP_SPIRAM);
        if (!fullBuf) {
            AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] Failed to allocate decode buffer");
            heap_caps_free(rgbBuf);
            return res;
        }

        decoded = fmt2rgb888(jpegBuf, jpegLen, PIXFORMAT_JPEG, fullBuf);
        if (!decoded) {
            AgriLog(TAG_SYSTEM, LEVEL_ERR, "[AI] JPEG decode failed");
            heap_caps_free(fullBuf);
            heap_caps_free(rgbBuf);
            return res;
        }

        // ── Step 2: Bilinear downsample from srcW×srcH to 96×96 ──
        for (int dy = 0; dy < targetH; dy++) {
            for (int dx = 0; dx < targetW; dx++) {
                // Map target pixel to source coordinates
                int sx = (dx * srcW) / targetW;
                int sy = (dy * srcH) / targetH;

                // Clamp
                if (sx >= srcW) sx = srcW - 1;
                if (sy >= srcH) sy = srcH - 1;

                int srcIdx = (sy * srcW + sx) * 3;
                int dstIdx = (dy * targetW + dx) * 3;

                rgbBuf[dstIdx + 0] = fullBuf[srcIdx + 0]; // R
                rgbBuf[dstIdx + 1] = fullBuf[srcIdx + 1]; // G
                rgbBuf[dstIdx + 2] = fullBuf[srcIdx + 2]; // B
            }
        }

        heap_caps_free(fullBuf);
    }

    // ── Step 3: Run inference on the 96×96 RGB buffer ──
    res = aiAnalyzeFrame(rgbBuf, rgbSize);

    heap_caps_free(rgbBuf);
    return res;
}
