#include "agri3d_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

SemaphoreHandle_t logMutex = NULL;

void loggerInit() {
    if (logMutex == NULL) {
        logMutex = xSemaphoreCreateMutex();
    }
}
