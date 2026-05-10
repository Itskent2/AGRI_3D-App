# Agri3D Autonomous Farming Logic Guide

This guide explains the step-by-step logic executed by the ESP32-S3 firmware during the Autonomous Farming routine (`AUTO_FARM`). The core logic resides in `agri3d_routine.cpp`.

---

## Overview

The Autonomous Farming routine is a loop that iterates over all registered plants in the field. For each plant, it performs motion, data acquisition, weather analysis, AI inference, and precise actuation of water and fertilizer.

---

## Step-by-Step Execution

### 1. Targeting and Motion
For each registered plant:
*   The system extracts the target coordinates $(X, Y)$ from the plant registry.
*   It enqueues a GRBL G-code command: `G0 X[targetX] Y[targetY] F[Feedrate]`.
*   It waits for the gantry to reach the position (`waitForGrblIdle`).
*   **Safety Check:** If the physical rain sensor triggers during motion, the movement aborts, and the system proceeds to home.

### 2. Data Acquisition (NPK Dip)
Once the gantry is positioned over the plant:
*   The system executes `executeNpkDip()`.
*   The Z-axis lowers the NPK sensor into the soil.
*   It reads soil parameters: **Nitrogen (N), Phosphorus (P), Potassium (K), Temperature, Moisture, EC, and pH**.
*   The Z-axis retracts to safety height.
*   If the reading is invalid or rain is detected during the dip, the routine skips the plant or aborts.

### 3. Weather-Adaptive Gating
The system checks environmental conditions to prevent drowning plants or wasting resources:
*   **API Data:** Fetches Precipitation Probability (`pPop`), Cloud Cover (`cc`), and Humidity (`rh`) from Open-Meteo.
*   **Sensor Data:** Checks the physical rain sensor.
*   **Conditions:**
    *   **ABORT:** If physically raining OR `pPop > 70%`, the routine stops and the gantry returns home.
    *   **SCALED:** If `pPop >= 40%` OR (`cc >= 60%` AND `rh >= 70%`), a weather gate flag ($Gf = 0$) is activated, reducing irrigation by half later.

### 4. XGBoost Nutrient Prediction
The system uses an embedded XGBoost machine learning model to predict optimal nutrient dosage:
*   **Inputs (6):** Nitrogen, Phosphorus, Potassium, Soil Temperature, Soil Moisture, and pH.
*   **Inference:** The `score(inputs)` function runs the decision trees defined in `xgboost_model.c`.
*   **Output:** A predicted base dosage value.

### 5. Mamdani Fuzzy Logic Controller (FLC)
To handle non-linear relationships and safely calculate final outputs:
*   The system passes the raw sensor readings and the XGBoost predicted dosage into the FLC (`flc.evaluate(...)`).
*   **Outputs:**
    *   `waterVolML`: Volume of water needed in milliliters.
    *   `fertVolML`: Volume of fertilizer needed in milliliters.

### 6. Safety Overrides & Adjustments
*   **Weather Gate Application:** If the weather gate was triggered ($Gf = 0$), `waterVolML` is cut in half (`waterVolML *= 0.5f`).
*   **EC (Salinity) Limit:** If soil Electrical Conductivity (EC) is $\ge 1500\,\mu\text{S/cm}$:
    *   Fertilizer volume is set to strictly `0`.
    *   Water volume is increased by `50ml` to flush the soil.

### 7. Actuation (Pumps)
The system converts volumes to time based on flow rates:
*   $\text{Duration (sec)} = \frac{\text{Volume (ml)}}{\text{Flow Rate (ml/s)}}$

It uses GRBL custom M-codes to toggle the relays:
*   **Fertilizer:** `M102` (Turn ON) $\rightarrow$ Delay for calculated time $\rightarrow$ `M103` (Turn OFF).
*   **Water:** `M100` (Turn ON) $\rightarrow$ Delay for calculated time $\rightarrow$ `M101` (Turn OFF).

---

## M-Codes Reference

| M-Code | Action |
| :--- | :--- |
| `M100` | Turn Water Pump **ON** |
| `M101` | Turn Water Pump **OFF** |
| `M102` | Turn Fertilizer Pump **ON** |
| `M103` | Turn Fertilizer Pump **OFF** |

---

## Configuration

*   **Weather Check Interval:** Currently set to 30 seconds for rapid testing (should be restored to 15 minutes for production).
*   **Safety Heights:** Handled by `executeNpkDip()` to ensure the sensor does not crash into the ground during XY moves.
