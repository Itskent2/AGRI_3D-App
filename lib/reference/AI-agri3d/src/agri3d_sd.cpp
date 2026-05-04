/**
 * @file agri3d_sd.cpp
 * @brief SD card init and "ok"-gated G-code file streaming.
 */

#include "agri3d_sd.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_grbl.h"
#include "agri3d_network.h"
#include "SD_MMC.h"
#include <ArduinoJson.h>
#include <time.h>

// ── Internal state ─────────────────────────────────────────────────────────
static File          _sdFile;
static bool          _sdActive      = false;
static volatile bool _waitingForOk  = false;
static uint8_t       _sdClientNum   = 0;
static uint32_t      _linesTotal    = 0;
static uint32_t      _linesSent     = 0;

// ============================================================================
// PUBLIC API
// ============================================================================

bool sdInit() {
    SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN);
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("[SD] No card or mount failed — SD disabled.");
        return false;
    }
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Card mounted: %llu MB\n", cardSize);
    return true;
}

void handleStartSD(uint8_t clientNum, const String& filename) {
    if (_sdActive) {
        webSocket.sendTXT(clientNum,
            "{\"evt\":\"SD_ERROR\",\"reason\":\"Stream already running\"}");
        return;
    }

    _sdFile = SD_MMC.open(filename, FILE_READ);
    if (!_sdFile) {
        StaticJsonDocument<96> doc;
        doc["evt"]  = "SD_ERROR";
        doc["reason"] = "File not found: " + filename;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(clientNum, out);
        Serial.printf("[SD] File not found: %s\n", filename.c_str());
        return;
    }

    _sdActive     = true;
    _waitingForOk = false;
    _sdClientNum  = clientNum;
    _linesSent    = 0;

    // Count lines for progress reporting (rewind after)
    _linesTotal = 0;
    while (_sdFile.available()) {
        String l = _sdFile.readStringUntil('\n');
        l.trim();
        if (l.length() > 0 && !l.startsWith(";") && !l.startsWith("("))
            _linesTotal++;
    }
    _sdFile.seek(0); // Rewind

    setOperation(OP_SD_RUNNING);

    StaticJsonDocument<128> doc;
    doc["evt"]   = "SD_START";
    doc["file"]  = filename;
    doc["lines"] = _linesTotal;
    String out; serializeJson(doc, out);
    webSocket.sendTXT(clientNum, out);
    Serial.printf("[SD] Streaming: %s (%lu lines)\n",
                  filename.c_str(), _linesTotal);
}

void handleStopSD(uint8_t clientNum) {
    if (!_sdActive) return;
    _sdActive     = false;
    _waitingForOk = false;
    if (_sdFile) _sdFile.close();
    setOperation(OP_IDLE);
    webSocket.sendTXT(clientNum, "{\"evt\":\"SD_STOPPED\"}");
    Serial.println("[SD] Stream stopped by user.");
}

void sdSignalOk() {
    _waitingForOk = false;
}

bool sdIsStreaming() { return _sdActive; }

void sdLoop() {
    if (!_sdActive) return;

    // Pause if rain/environment halted operations
    if (sysState.operation == OP_RAIN_PAUSED ||
        sysState.operation == OP_ALARM_RECOVERY) return;

    // Flow control: only send next line after receiving "ok"
    if (_waitingForOk) return;

    // File exhausted → job complete
    if (!_sdFile.available()) {
        _sdActive = false;
        _sdFile.close();
        setOperation(OP_IDLE);

        StaticJsonDocument<64> doc;
        doc["evt"]   = "SD_COMPLETE";
        doc["lines"] = _linesSent;
        String out; serializeJson(doc, out);
        webSocket.sendTXT(_sdClientNum, out);
        Serial.printf("[SD] Job complete. %lu lines sent.\n", _linesSent);
        return;
    }

    // Read next meaningful line
    String line = _sdFile.readStringUntil('\n');
    line.trim();

    // Skip comments and blanks
    if (line.length() == 0 ||
        line.startsWith(";") ||
        line.startsWith("(")) {
        return; // sdLoop() will be called again next loop() iteration
    }

    // Send line to Nano
    NanoSerial.println(line);
    _waitingForOk = true;
    _linesSent++;

    // Periodic progress broadcast (every 10 lines)
    if (_linesSent % 10 == 0 && _linesTotal > 0) {
        StaticJsonDocument<80> doc;
        doc["evt"]      = "SD_PROGRESS";
        doc["sent"]     = _linesSent;
        doc["total"]    = _linesTotal;
        doc["pct"]      = (int)((_linesSent * 100) / _linesTotal);
        String out; serializeJson(doc, out);
        webSocket.sendTXT(_sdClientNum, out);
    }
}

// ============================================================================
// IMAGE STORAGE
// ============================================================================

void sdEnsureDir(const char* path) {
    if (!SD_MMC.exists(path)) SD_MMC.mkdir(path);
}

bool sdSaveImage(const uint8_t* buf, size_t len,
                 const char* category, char prefix,
                 int idx, float x, float y,
                 time_t ts,
                 char* outPath) {
    if (!buf || len == 0) return false;

    struct tm* t = localtime(&ts);
    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%04d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    // e.g. /plantmap/20250503
    char dirPath[52];
    snprintf(dirPath, sizeof(dirPath), "%s/%s", category, dateStr);
    sdEnsureDir(category);
    sdEnsureDir(dirPath);

    // e.g. /plantmap/20250503/f_001_x310_y245_1714680000.jpg
    char filename[96];
    snprintf(filename, sizeof(filename),
             "%s/%c_%03d_x%d_y%d_%ld.jpg",
             dirPath, prefix, idx, (int)x, (int)y, (long)ts);

    File f = SD_MMC.open(filename, FILE_WRITE);
    if (!f) {
        Serial.printf("[SD] Save failed: %s\n", filename);
        return false;
    }
    f.write(buf, len);
    f.close();

    Serial.printf("[SD] Saved: %s (%u B)\n", filename, len);
    if (outPath) strncpy(outPath, filename, 96);
    return true;
}
