/**
 * @file agri3d_config.h
 * @brief Compile-time constants, pin definitions, and credentials for AGRI-3D.
 *
 * EDIT THIS FILE to match your hardware wiring and network setup.
 * All other library files read their hardware parameters from here.
 */

#pragma once

// ── Debug ─────────────────────────────────────────────────────────────────
#define AGRI3D_DEBUG true // Set false to silence all Serial output

// ============================================================================
// HARDWARE CAPABILITY FLAGS
// Set to true only when the physical hardware is connected and verified.
// Disabled modules compile but are no-ops, so code structure is preserved.
// ============================================================================
#define HW_CAMERA_CONNECTED true // OV5640 on custom ESP32-S3 N16R8 board
#define HW_NPK_CONNECTED true    // RS485 7-in-1 soil sensor wired
#define HW_RAIN_CONNECTED true   // Enabled for testing - wire to pin 1
#define HW_SD_CONNECTED true     // SD_MMC slot on board
#define HW_Z_AXIS_BROKEN false // Z-axis hardware fixed — allow moves

// ── WiFi Networks ─────────────────────────────────────────────────────────
// The ESP32 will try each of these in order at boot.
// If all fail it starts an AP (hotspot) and retries in the background.
// To add a network: copy a WIFI_NET_x block and increment WIFI_NET_COUNT.
#define WIFI_NET_COUNT 3

#define WIFI_NET_0_SSID "ESPTEST2"
#define WIFI_NET_0_PASS "xxxxxxxx"

#define WIFI_NET_2_SSID "AdminAccess"
#define WIFI_NET_2_PASS "Admin@CTU.2024"

#define WIFI_NET_1_SSID "ESPTEST"
#define WIFI_NET_1_PASS "clarkgwapo"

#define WIFI_CONNECT_TIMEOUT_MS 8000 // ms to wait per network attempt
#define WIFI_RETRY_INTERVAL_MS 30000 // ms between background reconnect attempts
#define MDNS_HOSTNAME "farmbot"

// ── AP Fallback (hotspot when no WiFi is found) ───────────────────────────
#define AP_SSID "AGRI3D_Hotspot"
#define AP_PASS "agri3d123" // min 8 chars for WPA2
#define AP_CHANNEL 6
#define AP_MAX_CONN 1 // Singleton: only 1 device on the AP

// ── Singleton Client ──────────────────────────────────────────────────────
// Only one Flutter client may be connected to the WebSocket at a time.
// A second connection attempt will be rejected with an error message.
#define WS_SINGLETON true

// ── WebSocket Server ───────────────────────────────────────────────────────
#define WS_PORT 80
#define AGRI3D_SECURE_TOKEN "AGRI3D_SECURE_TOKEN_V1"

// ── UDP Discovery ──────────────────────────────────────────────────────────
#define UDP_DISCOVERY_PORT 4210
#define UDP_BROADCAST_INTERVAL 3000 // ms between UDP beacons
#define HEARTBEAT_INTERVAL_MS 5000  // ms between proactive state broadcasts
#define FLUTTER_WATCHDOG_FLOOR_MS 10000 // min app-silence window before warning
#define FLUTTER_WATCHDOG_WARN_INTERVAL_MS 10000 // min gap between warning logs
#define FLUTTER_FORCE_DISCONNECT_ON_TIMEOUT true // auto-disconnect stale app link
#define FLUTTER_DISCONNECT_FLOOR_MS 20000 // min silence window before disconnect

// ── GRBL / Nano Serial Bridge ──────────────────────────────────────────────
#define NANO_RX_PIN 44
#define NANO_TX_PIN 43
#define NANO_BAUD 115200
#define GRBL_DEFAULT_FEEDRATE 1000

// ── Adaptive Poll Intervals (ms) ──────────────────────────────────────────
#define POLL_INTERVAL_IDLE 2000  // Machine at rest
#define POLL_INTERVAL_RUN 250    // Active move — fast position updates
#define POLL_INTERVAL_HOME 500   // Homing cycle
#define POLL_INTERVAL_ALARM 3000 // Error state — minimal traffic

// ── Nano Watchdog ──────────────────────────────────────────────────────────
// Nano is flagged UNRESPONSIVE if silent for: max(4 * poll_interval, this
// floor)
#define NANO_WATCHDOG_FLOOR_MS 10000 // Minimum 10 s before UNRESPONSIVE
#define NANO_WATCHDOG_HOME_MS 90000  // 90 seconds timeout during homing

// ── Camera Pins (ESP32-S3 N16R8 + OV5640) ────────────────────────────────
#define CAM_PWDN -1
#define CAM_RESET -1
#define CAM_XCLK 15
#define CAM_SIOD 4
#define CAM_SIOC 5
#define CAM_Y9 16
#define CAM_Y8 17
#define CAM_Y7 18
#define CAM_Y6 12
#define CAM_Y5 10
#define CAM_Y4 8
#define CAM_Y3 9
#define CAM_Y2 11
#define CAM_VSYNC 6
#define CAM_HREF 7
#define CAM_PCLK 13

// ── Camera Streaming ───────────────────────────────────────────────────────
#define STREAM_FPM_DEFAULT 180 // Default: 3 frames per second (QVGA recommended)
#define STREAM_FPM_MIN 1       // Slowest allowed rate
#define STREAM_FPM_MAX 600     // Fastest allowed (= 10 FPS @ 100 ms)
#define STREAM_IDLE_DELAY 150 // ms to wait when stream is inactive

// ── OV5640 Field of View ───────────────────────────────────────────────────
// Used to calculate ground coverage per frame at a given Z height.
// OV5640 at 69° diagonal FOV, 4:3 aspect ratio (1600×1200 UXGA)
#define CAM_FOV_DIAG_DEG 69.0f // Diagonal FOV in degrees
#define CAM_FOV_H_DEG 55.0f    // Approximate horizontal FOV
#define CAM_FOV_V_DEG 41.0f    // Approximate vertical FOV
// Ground width/height covered at Z mm above soil:
//   groundW = 2 * Z * tan(FOV_H / 2)  (mm)
//   groundH = 2 * Z * tan(FOV_V / 2)  (mm)
// At Z=200mm: ~205mm wide, ~148mm tall per frame
// Flutter uses these constants for image stitching alignment.

// ── NPK Sensor (RS485 / UART2) ────────────────────────────────────────────
// NOTE: HW_NPK_CONNECTED = true — wired to truly safe pins 41, 42, 2
#define NPK_RX_PIN 41 // RO
#define NPK_TX_PIN 42 // DI
#define NPK_DERE 2    // Direction
#define NPK_BAUD 4800

// NPK polling interval (ms between automatic sensor reads)
#define NPK_POLL_INTERVAL_MS (5UL * 60UL * 1000UL) // Every 5 minutes

// ── Rain Sensor ───────────────────────────────────────────────────────────
// NOTE: HW_RAIN_CONNECTED = false — sensor not yet wired
#define RAIN_PIN 1 // Digital input with INPUT_PULLDOWN

// ── SD Card (1-Bit MMC) ───────────────────────────────────────────────────
#define SD_MMC_CLK_PIN 39
#define SD_MMC_CMD_PIN 38
#define SD_MMC_D0_PIN 40

// ── SD Image Storage Paths ────────────────────────────────────────────────
// All images saved by ESP32 follow this directory structure:
//   /sdcard/<category>/YYYYMMDD/<filename>_x<X>_y<Y>_<ts>.jpg
#define SD_IMG_PLANTMAP "/plantmap" // Plant map scan frames
#define SD_IMG_WEEDING "/weeding"   // Weed detection frames
#define SD_IMG_DETECT "/detect"     // Auto-detect plant scan frames
#define SD_IMG_STREAM "/snapshots"  // Manual snapshots
#define SD_GCODE_DIR "/gcode"       // G-code job files
#define SD_LOG_DIR "/logs"          // NPK CSV logs (future)

// ── Weather API ───────────────────────────────────────────────────────────
#define WEATHER_API_INTERVAL_MS (30UL * 1000UL) // 30 seconds (fast for testing)
#define WEATHER_RAIN_CODE_MIN 51 // WMO code: drizzle or worse

// ── Plant Map / Scan ──────────────────────────────────────────────────────
#define SCAN_MOVE_TIMEOUT_MS 60000 // Max wait for gantry to reach each point (increased to 60s)
#define SCAN_HOME_TIMEOUT_MS 120000 // Max wait for homing ($HX / $HY) (increased to 120s)
#define DEFAULT_CAM_OFFSET_MM 100.0f // Physical camera-to-gantry-centre offset in mm
