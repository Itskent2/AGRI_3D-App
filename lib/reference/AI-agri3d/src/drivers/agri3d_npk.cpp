/**
 * @file agri3d_npk.cpp
 * @brief NPK sensor polling, NVS history, and Flutter heatmap broadcast.
 *
 * Sensor wiring: RS485 half-duplex, DERE pin controls TX/RX direction.
 *   DERE HIGH → transmit (send Modbus query)
 *   DERE LOW  → receive  (read sensor response)
 */

#include "agri3d_npk.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_network.h"
#include "agri3d_grbl.h"
#include "agri3d_logger.h"
#include "agri3d_sd.h"
#include <SD_MMC.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
HardwareSerial NpkSerial(2);

// Standard Modbus RTU query for 7-in-1 soil sensor (function 03, reg 0x0000, count 7)
static const uint8_t NPK_QUERY[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08 };
static const int     NPK_RESP_LEN = 19; // 01 03 0E (14 bytes data) CRC_L CRC_H

// ── Grid dimensions (match Flutter plot_model.dart) ───────────────────────────
// Edit these if Flutter's gridCols / gridRows change.
static const int HEATMAP_COLS = 5;
static const int HEATMAP_ROWS = 3;

// ── NVS ───────────────────────────────────────────────────────────────────────
static Preferences _prefs;
static const char* NVS_NPK_NS = "npk_hist";

// ── Globals ───────────────────────────────────────────────────────────────────
SoilReading latestSoil;
static unsigned long _lastPollMs = 0;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * @brief Calculate Modbus CRC16
 */
uint16_t calculateCRC(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/** Map gantry mm position → heatmap grid cell index. */
static int mmToGridX(float mm) {
    if (!machineDim.valid || machineDim.maxX <= 0) return 0;
    float step = machineDim.maxX / max(HEATMAP_COLS - 1, 1);
    return (int)constrain(round(mm / step), 0, HEATMAP_COLS - 1);
}

static int mmToGridY(float mm) {
    if (!machineDim.valid || machineDim.maxY <= 0) return 0;
    float step = machineDim.maxY / max(HEATMAP_ROWS - 1, 1);
    return (int)constrain(round(mm / step), 0, HEATMAP_ROWS - 1);
}

/** Returns "YYYYMMDD" string for today (requires NTP / system time). */
static String todayStr() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    return String(buf);
}

/** NVS key: "YYYYMMDD_GXX_YY" — one entry per grid cell per day. */
static String nvsKey(const String& date, int gx, int gy) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s_G%02d_%02d", date.c_str(), gx, gy);
    return String(buf);
}

/** Save one reading to NVS. Overwrites any existing entry for same cell+day. */
static void saveToNVS(const NpkReading& r) {
    String key = nvsKey(todayStr(), r.gridX, r.gridY);
    // Format: "n,p,k,timestamp"
    char val[48];
    snprintf(val, sizeof(val), "%.1f,%.1f,%.1f,%ld",
             r.n, r.p, r.k, (long)r.timestamp);
    _prefs.begin(NVS_NPK_NS, false);
    _prefs.putString(key.c_str(), val);
    _prefs.end();
}

// ============================================================================
// BROADCAST FUNCTIONS
// ============================================================================

/**
 * Live NPK update — three separate heatmap-compatible packets (N, P, K)
 * PLUS one unified NPK packet for the plot model.
 *
 * sensor_heatmaps.dart reads: { x, y, m/val }
 * plot_model.dart reads:      { evt:"NPK", x, y, n, p, k, ts }
 */
static void broadcastNpkLive(const NpkReading& r) {
    if (sysState.getFlutter() == FLUTTER_DISCONNECTED) return;

    // ── 1. Unified NPK packet (matches NpkLevel.fromJson in plot_model.dart) ──
    {
        StaticJsonDocument<256> doc;
        doc["evt"]  = "NPK";
        doc["x"]    = r.gridX;    // grid col (matches plot_model x)
        doc["y"]    = r.gridY;    // grid row (matches plot_model y)
        doc["m"]        = r.moisture;
        doc["temp"]     = r.tempC;
        doc["ec"]       = r.ec;
        doc["ph"]       = r.ph;
        doc["n"]        = r.n;
        doc["p"]        = r.p;
        doc["k"]        = r.k;
        doc["moisture"] = r.moisture;
        doc["temp"]     = r.tempC;
        doc["ec"]       = r.ec;
        doc["ph"]       = r.ph;
        doc["mmX"]  = r.x;
        doc["mmY"]  = r.y;
        doc["ts"]   = (long)r.timestamp;
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
    }

    // ── 2. Nitrogen heatmap cell (sensor_heatmaps.dart format) ──
    {
        StaticJsonDocument<64> doc;
        doc["evt"] = "NPK_N";
        doc["x"]   = r.gridX;
        doc["y"]   = r.gridY;
        doc["val"] = r.n;
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
    }

    // ── 3. Phosphorus heatmap cell ──
    {
        StaticJsonDocument<64> doc;
        doc["evt"] = "NPK_P";
        doc["x"]   = r.gridX;
        doc["y"]   = r.gridY;
        doc["val"] = r.p;
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
    }

    // ── 4. Potassium heatmap cell ──
    {
        StaticJsonDocument<64> doc;
        doc["evt"] = "NPK_K";
        doc["x"]   = r.gridX;
        doc["y"]   = r.gridY;
        doc["val"] = r.k;
        String out; serializeJson(doc, out);
        webSocket.broadcastTXT(out);
    }

    AgriLog(TAG_SENSORS, LEVEL_INFO, "Grid(%d,%d) N=%.1f P=%.1f K=%.1f",
                  r.gridX, r.gridY, r.n, r.p, r.k);
}

// ============================================================================
// SENSOR READ
// ============================================================================

bool npkReadNow() {
    // ── Send Modbus query ──────────────────────────────────────────────────
    digitalWrite(NPK_DERE, HIGH);
    delayMicroseconds(100);
    NpkSerial.write(NPK_QUERY, sizeof(NPK_QUERY));
    NpkSerial.flush();
    digitalWrite(NPK_DERE, LOW);

    // ── Wait for response ──────────────────────────────────────────────────
    unsigned long t = millis();
    while (!NpkSerial.available()) {
        if (millis() - t > 2000) {
            AgriLog(TAG_SENSORS, LEVEL_WARN, "Timeout — no response from sensor");
            return false;
        }
        delay(10);
    }

    // Read all bytes with a timeout gap of 50ms
    uint8_t buf[128];
    int len = 0;
    unsigned long lastByteTime = millis();
    
    while (len < 128) {
        if (NpkSerial.available()) {
            buf[len++] = NpkSerial.read();
            lastByteTime = millis();
        } else {
            if (millis() - lastByteTime > 50) {
                break; // End of packet
            }
            delay(2);
        }
    }

    // FORGIVING PARSER: Look for Modbus function code 03 and length 0E (14)
    for (int i = 0; i < len - 16; i++) {
        if (buf[i] == 0x03 && buf[i+1] == 0x0E) {
            AgriLog(TAG_SENSORS, LEVEL_INFO, "Found valid Modbus pattern in response.");
            
            // Extract data
            float moisture = (float)((buf[i+2] << 8) | buf[i+3]) / 10.0f;
            int16_t tempRaw = (int16_t)((buf[i+4] << 8) | buf[i+5]);
            float tempC = (float)tempRaw / 10.0f;
            float ec = (float)((buf[i+6] << 8) | buf[i+7]);
            float ph = (float)((buf[i+8] << 8) | buf[i+9]) / 10.0f;
            float n = (float)((buf[i+10] << 8) | buf[i+11]);
            float p = (float)((buf[i+12] << 8) | buf[i+13]);
            float k = (float)((buf[i+14] << 8) | buf[i+15]);

            // ── Build reading with current gantry position ─────────────────────────
            SoilReading r;
            r.moisture  = moisture;
            r.tempC     = tempC;
            r.ec        = ec;
            r.ph        = ph;
            r.n         = n;
            r.p         = p;
            r.k         = k;
            r.x         = sysState.getX();
            r.y         = sysState.getY();
            r.gridX     = mmToGridX(sysState.getX());
            r.gridY     = mmToGridY(sysState.getY());
            r.timestamp = time(nullptr);
            r.valid     = true;

            latestSoil = r;

            saveToNVS(r);

            // ── Save to SD Card ───────────────────────────────────────────────────
#if HW_SD_CONNECTED
            File file = SD_MMC.open("/soil_data.csv", FILE_APPEND);
            if (file) {
                if (file.size() == 0) {
                    file.println("Timestamp,X,Y,GridX,GridY,Moisture,TempC,EC,pH,N,P,K");
                }
                file.printf("%ld,%.1f,%.1f,%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                            (long)r.timestamp, r.x, r.y, r.gridX, r.gridY,
                            r.moisture, r.tempC, r.ec, r.ph, r.n, r.p, r.k);
                file.close();
                AgriLog(TAG_SENSORS, LEVEL_INFO, "Saved reading to SD card");
            } else {
                AgriLog(TAG_SENSORS, LEVEL_WARN, "Failed to open soil_data.csv on SD");
            }
#endif

            broadcastNpkLive(r);
            return true;
        }
    }

    AgriLog(TAG_SENSORS, LEVEL_ERR, "Could not find valid Modbus pattern in response");
    return false;
}


// ============================================================================
// HISTORY QUERIES
// ============================================================================

void npkSendHistory(uint8_t clientNum, int gridX, int gridY) {
    String date = todayStr();
    String key  = nvsKey(date, gridX, gridY);

    _prefs.begin(NVS_NPK_NS, true);
    String val = _prefs.getString(key.c_str(), "");
    _prefs.end();

    StaticJsonDocument<256> doc;
    doc["evt"]   = "NPK_CELL_HISTORY";
    doc["date"]  = date;
    doc["gridX"] = gridX;
    doc["gridY"] = gridY;

    if (val.length() > 0) {
        // Parse stored "n,p,k,ts"
        int c1 = val.indexOf(',');
        int c2 = val.indexOf(',', c1+1);
        int c3 = val.indexOf(',', c2+1);
        doc["n"]  = val.substring(0, c1).toFloat();
        doc["p"]  = val.substring(c1+1, c2).toFloat();
        doc["k"]  = val.substring(c2+1, c3).toFloat();
        doc["ts"] = val.substring(c3+1).toInt();
        doc["hasData"] = true;
    } else {
        doc["hasData"] = false;
    }

    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
}

void npkSendFullHistory(uint8_t clientNum) {
    String date = todayStr();

    // Build a JSON array of all known cells for today
    DynamicJsonDocument doc(2048);
    doc["evt"]  = "NPK_HISTORY";
    doc["date"] = date;
    JsonArray readings = doc.createNestedArray("readings");

    _prefs.begin(NVS_NPK_NS, true);

    for (int gx = 0; gx < HEATMAP_COLS; gx++) {
        for (int gy = 0; gy < HEATMAP_ROWS; gy++) {
            String key = nvsKey(date, gx, gy);
            String val = _prefs.getString(key.c_str(), "");
            if (val.length() == 0) continue;

            int c1 = val.indexOf(',');
            int c2 = val.indexOf(',', c1+1);
            int c3 = val.indexOf(',', c2+1);

            JsonObject entry = readings.createNestedObject();
            entry["x"]  = gx;
            entry["y"]  = gy;
            entry["n"]  = val.substring(0, c1).toFloat();
            entry["p"]  = val.substring(c1+1, c2).toFloat();
            entry["k"]  = val.substring(c2+1, c3).toFloat();
            entry["ts"] = val.substring(c3+1).toInt();
        }
    }

    _prefs.end();

    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    AgriLog(TAG_SENSORS, LEVEL_INFO, "Sent full history for %s (%d readings)",
                  date.c_str(), readings.size());
}

// ============================================================================
// PUBLIC API
// ============================================================================

void npkInit() {
    pinMode(NPK_DERE, OUTPUT);
    digitalWrite(NPK_DERE, LOW);  // Default to receive mode
    NpkSerial.begin(NPK_BAUD, SERIAL_8N1, NPK_RX_PIN, NPK_TX_PIN);
    AgriLog(TAG_SENSORS, LEVEL_INFO, "Sensor initialised (RS485 UART2)");
}

void npkLoop() {
#if !HW_NPK_CONNECTED
    return;  // Sensor not wired yet
#endif
    if (millis() - _lastPollMs < NPK_POLL_INTERVAL_MS) return;
    _lastPollMs = millis();

    // Don't poll during alarm recovery
    if (sysState.getOperation() == OP_ALARM_RECOVERY) return;

    npkReadNow();
}
