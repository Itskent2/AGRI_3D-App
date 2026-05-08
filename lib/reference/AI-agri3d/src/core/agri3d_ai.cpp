/**
 * @file agri3d_ai.cpp
 * @brief Edge Impulse weed-detection inference implementation.
 *        Model: "Weed Detection" – EI project #923987, impulse v8.
 *
 * Inference is designed to run on Core 1 (the loop() / Brain task) so it
 * never blocks the Core 0 CommTask.  Raw camera frames are passed in as a
 * flat float array (RGB888 normalised 0-1) whose size must match
 * EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * 3.
 */

#include "agri3d_ai.h"
#include "agri3d_logger.h"
#include "agri3d_state.h"

// ── Edge Impulse SDK ────────────────────────────────────────────────────────
// Path is relative to the component root (AI-agri3d/).
// CMakeLists.txt and platformio.ini both add that root as an include dir.
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "esp_camera.h"

// ── ROM-based JPEG decoder (available on all ESP32 variants) ────────────────
#include "rom/tjpgd.h"

// ── Label indices (must match model-parameters/model_variables.h) ──────────
// Labels are: 0 = "plant", 1 = "weed"  (order fixed by EI training)
#define EI_LABEL_PLANT 0
#define EI_LABEL_WEED  1

// ── Confidence threshold ────────────────────────────────────────────────────
static constexpr float WEED_CONFIDENCE_THRESHOLD = 0.65f;

// ── Static inference buffer (avoids heap fragmentation on ESP32) ────────────
static float ei_input_buf[EI_CLASSIFIER_INPUT_WIDTH *
                          EI_CLASSIFIER_INPUT_HEIGHT *
                          (EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME / (EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT))];


// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief Callback used by ei_run_classifier to fill the feature buffer.
 *        We pre-fill ei_input_buf before calling run_classifier, so this
 *        just copies from that staging buffer.
 */
static int ei_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, ei_input_buf + offset, length * sizeof(float));
    return 0;
}

// ────────────────────────────────────────────────────────────────────────────

void aiInit() {
    AgriLog(TAG_AI, LEVEL_INFO, "Edge Impulse Weed Detection initialised.");
    AgriLog(TAG_AI, LEVEL_INFO, "  Model input : %dx%d px",
            EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
    AgriLog(TAG_AI, LEVEL_INFO, "  Label count : %d", EI_CLASSIFIER_LABEL_COUNT);
    AgriLog(TAG_AI, LEVEL_INFO, "  DSP blocks  : %d", (int)ei_dsp_blocks_size);
}

// ────────────────────────────────────────────────────────────────────────────

/**
 * @brief Run EI inference on a pre-scaled, normalised RGB float buffer.
 */
AiResult aiRunInference(const float* floatRGB, size_t pixelCount) {
    AiResult res = { false, false, 0.0f, 0, 0 };

    if (floatRGB == nullptr || pixelCount == 0) {
        AgriLog(TAG_AI, LEVEL_WARN, "aiRunInference: null or empty buffer.");
        return res;
    }

    // ── 1. Copy into staging buffer ─────────────────────────────────────────
    const size_t totalFloats = pixelCount * 3;
    if (totalFloats > sizeof(ei_input_buf) / sizeof(float)) {
        AgriLog(TAG_AI, LEVEL_ERR, "aiRunInference: buffer too large (%d floats, max %d).",
                (int)totalFloats, (int)(sizeof(ei_input_buf) / sizeof(float)));
        return res;
    }
    memcpy(ei_input_buf, floatRGB, totalFloats * sizeof(float));

    // ── 2. Build signal descriptor ──────────────────────────────────────────
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data     = &ei_get_data;

    // ── 3. Run classifier ───────────────────────────────────────────────────
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

    if (err != EI_IMPULSE_OK) {
        AgriLog(TAG_AI, LEVEL_ERR, "run_classifier failed: error code %d", (int)err);
        return res;
    }

    // ── 4. Parse results ────────────────────────────────────────────────────
#if EI_CLASSIFIER_OBJECT_DETECTION
    // Object-detection model: iterate bounding boxes
    for (size_t i = 0; i < result.bounding_boxes_count; i++) {
        auto& bb = result.bounding_boxes[i];
        if (bb.value < WEED_CONFIDENCE_THRESHOLD) continue;

        String label = String(bb.label);
        if (label == "weed") {
            res.foundWeed = true;
            res.confidence = bb.value;
            res.xOffset = (int)bb.x - (EI_CLASSIFIER_INPUT_WIDTH / 2);
            res.yOffset = (int)bb.y - (EI_CLASSIFIER_INPUT_HEIGHT / 2);
            AgriLog(TAG_AI, LEVEL_WARN,
                    "WEED DETECTED  conf=%.2f  offset=(%d,%d)",
                    res.confidence, res.xOffset, res.yOffset);
            break; 
        } else if (label == "plant") {
            res.foundPlant = true;
        }
    }
#else
    // Classification model: pick highest scoring label
    float maxVal = 0.0f;
    int   maxIdx = -1;
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > maxVal) {
            maxVal = result.classification[i].value;
            maxIdx = (int)i;
        }
    }
    res.confidence = maxVal;
    if (maxIdx == EI_LABEL_WEED && maxVal >= WEED_CONFIDENCE_THRESHOLD) {
        res.foundWeed = true;
        AgriLog(TAG_AI, LEVEL_WARN, "WEED DETECTED  conf=%.2f", maxVal);
    } else if (maxIdx == EI_LABEL_PLANT) {
        res.foundPlant = true;
        AgriLog(TAG_AI, LEVEL_INFO, "Plant detected  conf=%.2f", maxVal);
    }
#endif

    return res;
}

// ── TJpgDec ROM-based JPEG decode helpers ────────────────────────────────────
// The ROM TJpgDec decoder is always available on ESP32/S2/S3.
// JD_FORMAT = 0 in the ROM header means output is RGB888 (3 bytes/pixel).

struct JpegIoCtx {
    // Input side
    const uint8_t* jpegData;
    size_t         jpegLen;
    size_t         jpegOffset;
    // Output side
    uint8_t*       rgbBuf;
    uint32_t       width;
    uint32_t       height;
    bool           ok;
};

// Input callback: feeds compressed JPEG bytes to the decoder.
static UINT tjpgdInputCb(JDEC* jd, BYTE* buff, UINT ndata) {
    JpegIoCtx* io = (JpegIoCtx*)jd->device;
    if (io->jpegOffset >= io->jpegLen) return 0;
    UINT avail = (UINT)(io->jpegLen - io->jpegOffset);
    if (ndata > avail) ndata = avail;
    if (buff) {
        memcpy(buff, io->jpegData + io->jpegOffset, ndata);
    }
    io->jpegOffset += ndata;
    return ndata;
}

// Output callback: writes decoded RGB888 MCU blocks into a flat buffer.
static UINT tjpgdOutputCb(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegIoCtx* io = (JpegIoCtx*)jd->device;
    uint8_t* src = (uint8_t*)bitmap;
    for (WORD y = rect->top; y <= rect->bottom; y++) {
        for (WORD x = rect->left; x <= rect->right; x++) {
            uint32_t dstIdx = (y * io->width + x) * 3;
            io->rgbBuf[dstIdx + 0] = *src++; // R
            io->rgbBuf[dstIdx + 1] = *src++; // G
            io->rgbBuf[dstIdx + 2] = *src++; // B
        }
    }
    io->ok = true;
    return 1; // Continue decoding
}

// ── Full JPEG-to-Inference pipeline ──────────────────────────────────────────
AiResult aiAnalyzeFrame(uint8_t* buf, size_t len) {
    AiResult res = { false, false, 0.0f, 0, 0 };
    if (buf == nullptr || len == 0) return res;

    // ── 1. Decode JPEG → raw RGB888 using ROM TJpgDec ──────────────────
    const size_t TJPGD_WORKSPACE = 4096;
    void* work = malloc(TJPGD_WORKSPACE);
    if (!work) {
        AgriLog(TAG_AI, LEVEL_ERR, "aiAnalyzeFrame: TJpgDec workspace alloc failed");
        return res;
    }

    JpegIoCtx io = { buf, len, 0, nullptr, 0, 0, false };
    JDEC jdec;
    JRESULT jres = jd_prepare(&jdec, tjpgdInputCb, work, TJPGD_WORKSPACE, &io);
    if (jres != JDR_OK) {
        AgriLog(TAG_AI, LEVEL_WARN, "aiAnalyzeFrame: JPEG prepare failed (%d)", (int)jres);
        free(work);
        return res;
    }

    uint32_t srcW = jdec.width;
    uint32_t srcH = jdec.height;

    size_t rgbSize = srcW * srcH * 3;
    uint8_t* rgbBuf = (uint8_t*)(psramFound()
        ? heap_caps_malloc(rgbSize, MALLOC_CAP_SPIRAM)
        : malloc(rgbSize));
    if (!rgbBuf) {
        AgriLog(TAG_AI, LEVEL_ERR, "aiAnalyzeFrame: RGB malloc failed (%u bytes)", (unsigned)rgbSize);
        free(work);
        return res;
    }

    // Fill output side of the context
    io.rgbBuf = rgbBuf;
    io.width  = srcW;
    io.height = srcH;

    jres = jd_decomp(&jdec, tjpgdOutputCb, 0); // scale=0 → 1:1
    free(work);

    if (jres != JDR_OK || !io.ok) {
        AgriLog(TAG_AI, LEVEL_WARN, "aiAnalyzeFrame: JPEG decompress failed (%d)", (int)jres);
        free(rgbBuf);
        return res;
    }

    // ── 2. Resize to EI model input (nearest-neighbour) ─────────────────
    const uint32_t dstW = EI_CLASSIFIER_INPUT_WIDTH;
    const uint32_t dstH = EI_CLASSIFIER_INPUT_HEIGHT;
    const size_t   floatCount = dstW * dstH * 3;

    float* floatBuf = (float*)(psramFound()
        ? heap_caps_malloc(floatCount * sizeof(float), MALLOC_CAP_SPIRAM)
        : malloc(floatCount * sizeof(float)));
    if (!floatBuf) {
        AgriLog(TAG_AI, LEVEL_ERR, "aiAnalyzeFrame: float malloc failed");
        free(rgbBuf);
        return res;
    }

    for (uint32_t row = 0; row < dstH; row++) {
        uint32_t srcRow = (uint32_t)((float)row / dstH * srcH);
        for (uint32_t col = 0; col < dstW; col++) {
            uint32_t srcCol  = (uint32_t)((float)col / dstW * srcW);
            uint32_t srcIdx  = (srcRow * srcW + srcCol) * 3;
            uint32_t dstIdx  = (row * dstW + col) * 3;
            floatBuf[dstIdx + 0] = rgbBuf[srcIdx + 0] / 255.0f;
            floatBuf[dstIdx + 1] = rgbBuf[srcIdx + 1] / 255.0f;
            floatBuf[dstIdx + 2] = rgbBuf[srcIdx + 2] / 255.0f;
        }
    }
    free(rgbBuf);

    // ── 3. Run inference ──────────────────────────────────────────
    res = aiRunInference(floatBuf, dstW * dstH);
    free(floatBuf);

    return res;
}

