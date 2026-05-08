/**
 * @file agri3d_ai.cpp
 * @brief AI analysis stub — Edge Impulse SDK disabled.
 *
 * All inference functions are no-ops that return empty results.
 * Re-enable by integrating the Edge Impulse SDK and restoring the
 * full implementation.
 */

#include "agri3d_ai.h"
#include "agri3d_logger.h"

// ── Module state ──────────────────────────────────────────────────────────
static bool _aiReady = false;

// ============================================================================
// INITIALIZATION
// ============================================================================

void aiInit() {
    AgriLog(TAG_AI, LEVEL_WARN, "[AI] SDK disabled — running without inference engine.");
    _aiReady = false;
}

bool aiIsReady() {
    return _aiReady;
}

// ============================================================================
// RAW RGB INFERENCE STUB
// ============================================================================

AiResult aiAnalyzeFrame(uint8_t* buf, size_t len) {
    (void)buf;
    (void)len;

    AiResult res = {};
    res.foundPlant      = false;
    res.foundWeed       = false;
    res.confidence      = 0.0f;
    res.xOffset         = 0;
    res.yOffset         = 0;
    res.cropCount       = 0;
    res.weedCount       = 0;
    res.totalDetections = 0;

    AgriLog(TAG_AI, LEVEL_WARN, "[AI] aiAnalyzeFrame() called but AI is disabled.");
    return res;
}

// ============================================================================
// JPEG CONVENIENCE WRAPPER STUB
// ============================================================================

AiResult aiAnalyzeJpeg(uint8_t* jpegBuf, size_t jpegLen) {
    (void)jpegBuf;
    (void)jpegLen;

    AiResult res = {};
    res.foundPlant      = false;
    res.foundWeed       = false;
    res.confidence      = 0.0f;
    res.xOffset         = 0;
    res.yOffset         = 0;
    res.cropCount       = 0;
    res.weedCount       = 0;
    res.totalDetections = 0;

    AgriLog(TAG_AI, LEVEL_WARN, "[AI] aiAnalyzeJpeg() called but AI is disabled.");
    return res;
}
