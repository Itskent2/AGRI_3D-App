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
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

// ── Hardware ──────────────────────────────────────────────────────────────────
HardwareSerial NpkSerial(2);

// Standard Modbus RTU query for NPK sensor (function 03, reg 0x001E, count 3)
static const uint8_t NPK_QUERY[] = { 0x01, 0x03, 0x00, 0x1E, 0x00, 0x03, 0x65, 0xCD };
static const int     NPK_RESP_LEN = 11; // 01 03 06 N_H N_L P_H P_L K_H K_L CRC_L CRC_H

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
    if (sysState.flutter == FLUTTER_DISCONNECTED) return;

    // ── 1. Unified NPK packet (matches NpkLevel.fromJson in plot_model.dart) ──
    {
        StaticJsonDocument<192> doc;
        doc["evt"]  = "NPK";
        doc["x"]    = r.gridX;    // grid col (matches plot_model x)
        doc["y"]    = r.gridY;    // grid row (matches plot_model y)
        doc["n"]    = serialized(String(r.n, 1));
        doc["p"]    = serialized(String(r.p, 1));
        doc["k"]    = serialized(String(r.k, 1));
        doc["mmX"]  = serialized(String(r.x, 1));  // raw mm position
        doc["mmY"]  = serialized(String(r.y, 1));
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

    Serial.printf("[NPK] Grid(%d,%d) N=%.1f P=%.1f K=%.1f\n",
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
    while (NpkSerial.available() < NPK_RESP_LEN) {
        if (millis() - t > 500) {
            Serial.println("[NPK] Timeout — no response from sensor");
            return false;
        }
        delay(10);
    }

    // ── Read response bytes ────────────────────────────────────────────────
    uint8_t buf[NPK_RESP_LEN];
    NpkSerial.readBytes(buf, NPK_RESP_LEN);

    // Basic validation: check slave addr and function code
    if (buf[0] != 0x01 || buf[1] != 0x03 || buf[2] != 0x06) {
        Serial.printf("[NPK] Bad frame: %02X %02X %02X\n", buf[0], buf[1], buf[2]);
        return false;
    }

    // ── Parse N, P, K ─────────────────────────────────────────────────────
    float n = (float)((buf[3] << 8) | buf[4]);  // Nitrogen   mg/kg
    float p = (float)((buf[5] << 8) | buf[6]);  // Phosphorus mg/kg
    float k = (float)((buf[7] << 8) | buf[8]);  // Potassium  mg/kg

    // ── Build reading with current gantry position ─────────────────────────
    NpkReading r;
    r.n         = n;
    r.p         = p;
    r.k         = k;
    r.x         = sysState.grblX;
    r.y         = sysState.grblY;
    r.gridX     = mmToGridX(sysState.grblX);
    r.gridY     = mmToGridY(sysState.grblY);
    r.timestamp = time(nullptr);
    r.valid     = true;

    latestSoil = r;

    saveToNVS(r);
    broadcastNpkLive(r);
    return true;
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
    Serial.printf("[NPK] Sent full history for %s (%d readings)\n",
                  date.c_str(), readings.size());
}

// ============================================================================
// PUBLIC API
// ============================================================================

void npkInit() {
    pinMode(NPK_DERE, OUTPUT);
    digitalWrite(NPK_DERE, LOW);  // Default to receive mode
    NpkSerial.begin(NPK_BAUD, SERIAL_8N1, NPK_RX_PIN, NPK_TX_PIN);
    Serial.println("[NPK] Sensor initialised (RS485 UART2)");
}

void npkLoop() {
#if !HW_NPK_CONNECTED
    return;  // Sensor not wired yet
#endif
    if (millis() - _lastPollMs < NPK_POLL_INTERVAL_MS) return;
    _lastPollMs = millis();

    // Don't poll during alarm recovery
    if (sysState.operation == OP_ALARM_RECOVERY) return;

    npkReadNow();
}
