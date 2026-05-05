#include "agri3d_ai.h"
#include "agri3d_state.h"

void aiInit() {
    Serial.println("[AI] Engine Initialized (Placeholder)");
    // TODO: Luna to load TFLite model here
}

AiResult aiAnalyzeFrame(uint8_t* buf, size_t len) {
    // Placeholder logic: 
    // In the future, this will run a TFLite inference on Core 1.
    
    AiResult res;
    res.foundPlant = false;
    res.foundWeed = false;
    res.confidence = 0.0f;
    res.xOffset = 0;
    res.yOffset = 0;

    if (buf == nullptr || len == 0) return res;

    // Simulate analysis time to test Core 1 parallelism
    // vTaskDelay(pdMS_TO_TICKS(50)); 

    return res;
}
