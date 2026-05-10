# Agri3D Fuzzy Logic Controller (FLC) Guide

This guide explains the Fuzzy Logic Controller used in the Agri3D system to determine the exact volume of water and fertilizer for each plant. This document is structured to be useful for both reading and presentation during a panel defense.

---

## Overview

The system uses a **Sugeno-style Fuzzy Logic Controller** (often referred to in the code as Mamdani with singleton outputs). It takes real-time sensor readings and the output of the XGBoost machine learning model to calculate:
1.  **Water Volume** (in milliliters)
2.  **Fertilizer Volume** (in milliliters)

Fuzzy logic is ideal here because it handles the non-linear, "grey" areas of agriculture (e.g., "how dry is dry?") better than rigid IF-THEN rules.

---

## 1. Fuzzification (Input Membership Functions)

Fuzzification converts crisp sensor readings into "degrees of membership" (from `0.0` to `1.0`) in fuzzy sets.

### A. Soil Moisture (%)
*   **Dry:** $L(20, 40)$ $\rightarrow$ Fully dry at $\le 20\%$, scales to $0$ at $40\%$.
*   **Optimal:** $\text{Trap}(30, 40, 60, 70)$ $\rightarrow$ Peak membership between $40\%$ and $60\%$.
*   **Wet:** $R(60, 80)$ $\rightarrow$ Scales from $0$ at $60\%$ to fully wet at $\ge 80\%$.

### B. Electrical Conductivity (EC in µS/cm)
*   **High:** $R(1000, 1500)$ $\rightarrow$ Indicates salt build-up.
*   **Caution:** $R(1500, 2000)$ $\rightarrow$ Severe salt build-up.

### C. Nutrients (Average NPK in ppm)
*   **Low:** $L(50, 100)$ $\rightarrow$ Soil is nutrient-deficient.
*   **High:** $R(150, 200)$ $\rightarrow$ Soil is nutrient-rich.

### D. Soil pH
*   **Acidic:** $L(5.5, 6.5)$ $\rightarrow$ Acidic soil.
*   **Alkaline:** $R(7.5, 8.5)$ $\rightarrow$ Alkaline soil.

### E. XGBoost Predicted Dosage (0 - 100)
*   **Low:** $L(10, 30)$
*   **Medium:** $\text{Tri}(20, 50, 80)$
*   **High:** $R(70, 90)$

---

## 2. Rule Evaluation (Inference)

The system applies rules using **Min-Max inference** (MIN for AND, MAX for OR).

### Water Output Rules (Singletons: Zero=0ml, Low=25ml, Med=50ml, High=100ml)

| Rule | Condition | Output |
| :--- | :--- | :--- |
| **R1** | IF Moisture is **Wet** | Water = **Zero** |
| **R2** | IF Moisture is **Optimal** | Water = **Low** |
| **R3** | IF Moisture is **Dry** | Water = **High** |
| **R4** | IF EC is **High** | Water = **Medium** |
| **R5** | IF Moisture is **Wet** AND EC is **High** | Water = **Zero** |

### Fertilizer Output Rules (Singletons: Zero=0ml, Low=10ml, Med=20ml, High=40ml)

| Rule | Condition | Output |
| :--- | :--- | :--- |
| **R1** | IF Nutrient is **High** | Fert = **Zero** |
| **R2** | IF EC is **High** | Fert = **Zero** |
| **R3** | IF Dose is **High** AND Nutrient is **Low** | Fert = **High** |
| **R4** | IF Dose is **Medium** AND Nutrient is **Low** | Fert = **Medium** |
| **R5** | IF Dose is **Low** | Fert = **Low** |
| **R6** | IF Moisture is **Wet** AND EC is **Caution** | Fert = **Zero** |
| **R7** | IF pH is **Acidic** OR pH is **Alkaline** | Fert = **Medium** |

---

## 3. Defuzzification (Weighted Average)

To get the final crisp output (the actual milliliters to pump), the system uses the **Weighted Average** method (Sugeno style). 

For each rule triggered, it takes the "weight" (the degree of truth of the condition) and multiplies it by the corresponding output volume (the singleton).

### Formula:
$$\text{Output Volume} = \frac{\sum (\text{Weight} \times \text{Singleton Volume})}{\sum \text{Weights}}$$

### Example:
If Rule 2 for Water (Moisture Optimal) has a weight of $0.8$ (Output = $25\text{ml}$) and Rule 4 (EC High) has a weight of $0.4$ (Output = $50\text{ml}$):
$$\text{Water Volume} = \frac{(0.8 \times 25) + (0.4 \times 50)}{0.8 + 0.4} = \frac{20 + 20}{1.2} = 33.3\text{ml}$$

This ensures smooth transitions between states (e.g., as soil dries, water volume increases gradually, not in sudden jumps).
