/**
 * @file agri3d_environment.h
 * @brief Rain sensor monitoring and Open-Meteo weather API gating.
 *
 * Environment state is updated from two independent sources:
 *   1. RAIN_PIN digital read in environmentLoop() (immediate, hardware)
 *   2. weatherTask() FreeRTOS task (every 15 min, API-based)
 *
 * Both sources update sysState.environment via setEnvironment().
 * If rain clears AND weather gate clears → ENV_CLEAR → auto-resume SD jobs.
 *
 * Location is set by Flutter via SET_LOCATION:lat,lon command.
 * If not set, defaults to the last NVS-saved coordinates (or Cebu City).
 */

#pragma once
#include <Arduino.h>

/** Initialise rain pin and launch weatherTask on Core 0. Call from setup(). */
void environmentInit();

/**
 * Must be called from loop(). Checks the rain sensor pin and updates
 * EnvironmentState. Triggers feed-hold on the Nano if rain starts.
 */
void environmentLoop();

/**
 * Update the geographic coordinates used for the weather API query.
 * Saved to NVS so they persist across reboots.
 * @param lat Latitude  (e.g. 10.3157)
 * @param lon Longitude (e.g. 123.8854)
 */
void setWeatherLocation(float lat, float lon);

/** Get latest precipitation probability (0-100%) */
int getWeatherPrecipProb();

/** Get latest cloud cover (0-100%) */
int getWeatherCloudCover();

/** Get latest relative humidity (0-100%) */
int getWeatherHumidity();

/** Returns true if physical rain pin is currently triggered */
bool isPhysicalRainDetected();
