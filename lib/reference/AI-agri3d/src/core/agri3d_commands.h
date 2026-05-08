/**
 * @file agri3d_commands.h
 * @brief Central WebSocket command router declaration.
 *
 * webSocketEvent() is registered in agri3d_network.cpp via the
 * wsEventWrapper singleton enforcer. All TEXT commands arrive here.
 *
 * Command Format:
 *   Simple:     "COMMAND"
 *   With args:  "COMMAND:arg1:arg2:..."
 *   Raw G-code: anything else is forwarded directly to NanoSerial
 *
 * Full Command Reference:
 *   ── Stream ──────────────────────────────────────────────────────────
 *   START_STREAM                → begin JPEG live stream
 *   STOP_STREAM                 → stop stream
 *   SET_FPM:N                   → set frames per minute (1-300)
 *   ── State ───────────────────────────────────────────────────────────
 *   GET_STATE                   → force-send SYSTEM_STATE JSON
 *   PING                        → reply PONG (latency check)
 *   CLEAR_CRASH                 → clear NVS crash record
 *   GET_DIMS                    → broadcast cached machine dimensions
 *   ── Gantry ──────────────────────────────────────────────────────────
 *   HOME_X                      → $HX to Nano
 *   HOME_Y                      → $HY to Nano
 *   UNLOCK                      → $clr (alarm unlock) to Nano
 *   ESTOP                       → Ctrl-X (hard stop) to Nano
 *   GCODE:<line>                → forward raw G-code to Nano
 *   ── SD Card ─────────────────────────────────────────────────────────
 *   START_SD:<filename>         → stream G-code file from SD card
 *   STOP_SD                     → cancel SD stream
 *   ── Scanning / Mapping ──────────────────────────────────────────────
 *   SCAN_PLANT:c:r:sX:sY:zH    → photo-only grid scan (plant map)
 *   SCAN_NPK:sX:sY              → NPK-only grid scan (heatmap)
 *   SCAN_FULL:c:r:sX:sY:zH     → NPK + photo grid scan
 *   AUTO_DETECT_PLANTS:c:r:sX:sY:zH → AI plant detection scan
 *   ── Plant Registry ──────────────────────────────────────────────────
 *   REGISTER_PLANT:x:y:name[:N:P:K] → manually add plant
 *   CONFIRM_PLANT:x:y:name     → confirm AI-detected candidate
 *   REJECT_PLANT:x:y           → reject AI-detected candidate
 *   CLEAR_PLANTS               → clear all plant entries
 *   GET_PLANT_MAP              → broadcast plant registry JSON
 *   ── Routine ─────────────────────────────────────────────────────────
 *   RUN_FARMING_CYCLE          → full autonomous per-plant routine
 *   ── NPK ─────────────────────────────────────────────────────────────
 *   GET_NPK                    → take immediate NPK reading
 *   NPK_HISTORY:gx:gy          → send today's history for one cell
 *   NPK_HISTORY_ALL            → send today's full heatmap history
 *   ── Environment ─────────────────────────────────────────────────────
 *   SET_LOCATION:lat,lon       → update weather API coordinates
 */

#pragma once
#include <Arduino.h>
#include <WebSocketsServer.h>

/**
 * Central WebSocket event handler.
 * Registered via agri3d_network.cpp's wsEventWrapper().
 * Called for WStype_TEXT, WStype_BIN, WStype_PING, WStype_PONG.
 */
void webSocketEvent(uint8_t num, WStype_t type,
                    uint8_t* payload, size_t length);
