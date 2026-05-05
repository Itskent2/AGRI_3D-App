/**
 * @file AI_Agri3D.h
 * @brief Master include for the AI-agri3d library.
 *
 * In your .ino sketch, just write:
 *   #include <AI_Agri3D.h>
 *
 * This pulls in every module in the correct dependency order.
 */

#pragma once

// 1. Config must come first — everything else reads from it
#include "agri3d_config.h"

// 2. State machine — defines sysState and all enums used by other modules
#include "agri3d_state.h"
#include "agri3d_commands.h"
#include "agri3d_ai.h"
#include "agri3d_logger.h"
#include "../drivers/agri3d_camera.h"
#include "../drivers/agri3d_grbl.h"
#include "../drivers/agri3d_sd.h"
#include "../drivers/agri3d_sensors.h"
#include "../network/agri3d_network.h"

// 3. Network — WiFi, WebSocket, UDP
#include "agri3d_network.h"

// 4. GRBL Bridge — Nano serial, parser, poll, watchdog
#include "agri3d_grbl.h"

// 5. Camera — init, stream task, frame capture helper
#include "agri3d_camera.h"

// 6. Plant map — SCAN_PLANT snake grid
#include "agri3d_plant_map.h"

// 7. NPK sensor — 7-in-1 soil readings, heatmap history
#include "agri3d_npk.h"

// 8. Routine orchestrator — farming cycle, plant registry, standalone scans
#include "agri3d_routine.h"

// 9. Environment — rain sensor, weather API task
#include "agri3d_environment.h"

// 8. SD Card — file streaming, flow control
#include "agri3d_sd.h"

// 9. Commands — central WebSocket command router (must come last)
#include "agri3d_commands.h"
