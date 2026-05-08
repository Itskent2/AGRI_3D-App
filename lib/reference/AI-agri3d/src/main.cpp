#include <Arduino.h>
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_logger.h"
#include "agri3d_ai.h"
#include "drivers/agri3d_camera.h"
#include "drivers/agri3d_grbl.h"
#include "drivers/agri3d_sensors.h"
#include "network/agri3d_network.h"

// ── Task Handlers ──────────────────────────────────────────────────────────
TaskHandle_t CommTaskHandle = NULL;

/**
 * @brief Core 0 Task: Communication Bridge.
 * Grouping WiFi and Nano Serial here ensures that the link between 
 * Flutter and the Motors is never interrupted by heavy logic on Core 1.
 */
void commTask(void *pvParameters) {
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Communication Task started on Core %d", xPortGetCoreID());
    for (;;) {
        networkLoop();  // Handles WebSocket, WiFi, and Discovery
        grblLoop();     // Continuous autonomous polling of the Nano/GRBL status
        sysState.refreshHeartbeats(); // Standardized watchdog & pro-active pings
        vTaskDelay(pdMS_TO_TICKS(1)); // Yield to allow background WiFi stack processing
    }
}

void setup() {
    Serial.begin(115200);
    delay(3000);
    
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "========================================");
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "ESP32-S3 BOOTING UP (COMM BRIDGE)");
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "========================================\n");
    
    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising network layer...");
    networkInit();      

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising GRBL bridge...");
    grblInit();         

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising sensors (Rain/NPK)...");
    sensorsInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising camera...");
    cameraInit();

    AgriLog(TAG_SYSTEM, LEVEL_INFO, "Initialising AI engine...");
    aiInit();

    xTaskCreatePinnedToCore(commTask, "CommTask", 8192, NULL, 3, &CommTaskHandle, 0);

    AgriLog(TAG_SYSTEM, LEVEL_SUCCESS, "SETUP COMPLETE. Communication Bridge active on Core 0.");
}

void loop() {
    // Core 1 is now reserved for the Brain/Routine task (to be implemented).
    // For now, we yield to prevent WDT triggers.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
