#include <Arduino.h>
#include "AI_Agri3D.h"
#include "drivers/agri3d_sd.h"

// ── Task Handlers ──────────────────────────────────────────────────────────
TaskHandle_t CommTaskHandle = NULL;

/**
 * @brief Core 0 Task: Communication Bridge.
 * Grouping WiFi and Nano Serial here ensures that the link between 
 * Flutter and the Motors is never interrupted by heavy logic on Core 1.
 */
void commTask(void *pvParameters) {
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Serial Communication Task started on Core %d (Low Priority)", xPortGetCoreID());
    for (;;) {
        grblLoop();     // Continuous autonomous polling of the Nano/GRBL status
        sysState.refreshHeartbeats(); // Standardized watchdog & pro-active pings
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to allow background WiFi stack processing
    }
}

void setup() {
    Serial.begin(115200);
    loggerInit();
    delay(3000);
    
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "========================================");
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "ESP32-S3 BOOTING UP (COMM BRIDGE)");
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "========================================\n");
    
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising network layer...");
    networkInit();      

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising GRBL bridge...");
    grblInit();         

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising SD card...");
    sdInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising sensors (Rain/NPK)...");
    sensorsInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising camera...");
    cameraInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising AI engine (Weed Detection)...");
    aiInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising Core 1 Routine Brain...");
    routineInit();

    xTaskCreatePinnedToCore(commTask, "CommTask", 8192, NULL, 1, &CommTaskHandle, 0); // Priority 1 (Below WebSockets)

    AgriLog(TAG_SYSTEM, LEVEL_SUCCESS, "SETUP COMPLETE. Communication Bridge active on Core 0.");
}

void loop() {
    // Routine logic and background polling on Core 1
    npkLoop(); 
    vTaskDelay(pdMS_TO_TICKS(10));
}
