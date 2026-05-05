/**
 * @file agri3d_environment.cpp
 * @brief Rain sensor monitoring and Open-Meteo weather gate.
 */

#include "agri3d_environment.h"
#include "agri3d_config.h"
#include "agri3d_state.h"
#include "agri3d_grbl.h"
#include "agri3d_network.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── Coordinates ───────────────────────────────────────────────────────────────
static float _lat = 10.3157f;   // Default: Cebu City, Philippines
static float _lon = 123.8854f;
static Preferences _prefs;

// ── Internal state ─────────────────────────────────────────────────────────────
static bool _rainPinHigh     = false; // Last debounced rain pin state
static bool _weatherGated    = false; // Last weather API result

// ============================================================================
// WEATHER TASK (FreeRTOS, Core 0)
// ============================================================================

static void weatherTask(void* /*pvParameters*/) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WEATHER_API_INTERVAL_MS));

        if (WiFi.status() != WL_CONNECTED) continue;

        HTTPClient http;
        // Fetch current weather code + hourly precipitation probability
        String url = "http://api.open-meteo.com/v1/forecast"
                     "?latitude="  + String(_lat, 4) +
                     "&longitude=" + String(_lon, 4) +
                     "&current=weather_code,precipitation_probability"
                     "&forecast_days=1";

        http.begin(url);
        http.setTimeout(8000);
        int code = http.GET();

        if (code == HTTP_CODE_OK) {
            String body = http.getString();

            // Parse weather_code from JSON (simple substring search avoids full parse)
            int wcIdx = body.indexOf("\"weather_code\":");
            int ppIdx = body.indexOf("\"precipitation_probability\":");

            int weatherCode = (wcIdx != -1) ?
                body.substring(wcIdx + 15).toInt() : 0;
            int precipProb  = (ppIdx != -1) ?
                body.substring(ppIdx + 28).toInt() : 0;

            // Gate if: WMO code ≥ 51 (drizzle/rain/storm) OR precip > 70%
            _weatherGated = (weatherCode >= WEATHER_RAIN_CODE_MIN || precipProb > 70);

            AgriLog(TAG_ENV, "Weather: code=%d precip=%d%% gated=%s",
                          weatherCode, precipProb, _weatherGated ? "YES" : "NO");

            // Broadcast weather info to Flutter
            StaticJsonDocument<128> doc;
            doc["evt"]         = "WEATHER";
            doc["code"]        = weatherCode;
            doc["precipProb"]  = precipProb;
            doc["gated"]       = _weatherGated;
            String out; serializeJson(doc, out);
            webSocket.broadcastTXT(out);
        } else {
            AgriLog(TAG_ENV, "Weather API failed: HTTP %d", code);
        }
        http.end();

        // Recalculate combined environment state
        // (rain sensor state is handled by environmentLoop, but we
        //  need to update here too if weather gate changes)
        bool rainSensor = _rainPinHigh;
        if      (rainSensor && _weatherGated) setEnvironment(ENV_RAIN_AND_WEATHER);
        else if (rainSensor)                  setEnvironment(ENV_RAIN_SENSOR);
        else if (_weatherGated)               setEnvironment(ENV_WEATHER_GATED);
        else                                  setEnvironment(ENV_CLEAR);
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void setWeatherLocation(float lat, float lon) {
    _lat = lat; _lon = lon;
    _prefs.begin("env", false);
    _prefs.putFloat("lat", lat);
    _prefs.putFloat("lon", lon);
    _prefs.end();
    AgriLog(TAG_ENV, "Location set: %.4f, %.4f", lat, lon);
}

void environmentInit() {
    // Load saved coordinates
    _prefs.begin("env", true);
    _lat = _prefs.getFloat("lat", 10.3157f);
    _lon = _prefs.getFloat("lon", 123.8854f);
    _prefs.end();

    pinMode(RAIN_PIN, INPUT_PULLDOWN);
    AgriLog(TAG_ENV, "Rain pin=%d  Location=%.4f,%.4f",
                  RAIN_PIN, _lat, _lon);

    // Weather task on Core 0 so it doesn't interfere with camera on Core 1
    xTaskCreatePinnedToCore(weatherTask, "weatherTask",
                            6144, nullptr, 1, nullptr, 0);
}

void environmentLoop() {
#if !HW_RAIN_CONNECTED
    // Rain sensor not wired yet — only weather API gate applies
    // (weatherTask still runs and updates _weatherGated via setEnvironment)
    return;
#endif
    bool rainNow = (digitalRead(RAIN_PIN) == HIGH);

    if (rainNow == _rainPinHigh) return; // No change — skip
    _rainPinHigh = rainNow;

    // Compute new combined environment state
    EnvironmentState newEnv;
    if      (rainNow && _weatherGated) newEnv = ENV_RAIN_AND_WEATHER;
    else if (rainNow)                  newEnv = ENV_RAIN_SENSOR;
    else if (_weatherGated)            newEnv = ENV_WEATHER_GATED;
    else                               newEnv = ENV_CLEAR;

    if (newEnv == sysState.environment) return;

    if (rainNow && sysState.getEnvironment() == ENV_CLEAR) {
        AgriLog(TAG_ENV, "Rain detected — sending feed-hold to Nano");
        NanoSerial.print('!'); // Real-time bypass
        sysState.setOperation(OP_RAIN_PAUSED);
    }

    if (!rainNow && !_weatherGated &&
        sysState.getOperation() == OP_RAIN_PAUSED) {
        AgriLog(TAG_ENV, "Rain cleared — resuming");
        NanoSerial.print('~'); // Real-time bypass
        sysState.setOperation(OP_IDLE);
    }

    setEnvironment(newEnv);
}
