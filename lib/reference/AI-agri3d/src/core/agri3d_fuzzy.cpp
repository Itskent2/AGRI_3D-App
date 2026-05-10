#include "agri3d_fuzzy.h"
#include <algorithm>

float Agri3DFuzzyController::fuzL(float val, float a, float b) {
    if (val <= a) return 1.0f;
    if (val >= b) return 0.0f;
    return (b - val) / (b - a);
}

float Agri3DFuzzyController::fuzR(float val, float a, float b) {
    if (val <= a) return 0.0f;
    if (val >= b) return 1.0f;
    return (val - a) / (b - a);
}

float Agri3DFuzzyController::fuzTri(float val, float a, float b, float c) {
    if (val <= a || val >= c) return 0.0f;
    if (val == b) return 1.0f;
    if (val < b) return (val - a) / (b - a);
    return (c - val) / (c - b);
}

float Agri3DFuzzyController::fuzTrap(float val, float a, float b, float c, float d) {
    if (val <= a || val >= d) return 0.0f;
    if (val >= b && val <= c) return 1.0f;
    if (val < b) return (val - a) / (b - a);
    return (d - val) / (d - c);
}

void Agri3DFuzzyController::evaluate(float moisture, float ec, float n, float p, float k, float ph, float predictedDosage, float &outWaterVol, float &outFertVol) {
    // 1. Fuzzification
    
    // Moisture
    float moistDry = fuzL(moisture, 20.0f, 40.0f);
    float moistOpt = fuzTrap(moisture, 30.0f, 40.0f, 60.0f, 70.0f);
    float moistWet = fuzR(moisture, 60.0f, 80.0f);

    // EC
    float ecHigh = fuzR(ec, 1000.0f, 1500.0f);
    float ecCaution = fuzR(ec, 1500.0f, 2000.0f); // Severe salt build-up

    // Nutrients (using average of N, P, K for general nutrient richness)
    float avgNPK = (n + p + k) / 3.0f;
    float nutLow = fuzL(avgNPK, 50.0f, 100.0f);
    float nutHigh = fuzR(avgNPK, 150.0f, 200.0f);

    // pH
    float phAcidic = fuzL(ph, 5.5f, 6.5f);
    float phAlkaline = fuzR(ph, 7.5f, 8.5f);

    // Predicted Dosage (0 - 100)
    float doseLow = fuzL(predictedDosage, 10.0f, 30.0f);
    float doseMed = fuzTri(predictedDosage, 20.0f, 50.0f, 80.0f);
    float doseHigh = fuzR(predictedDosage, 70.0f, 90.0f);

    // 2. Rule Evaluation (Min-Max inference)
    
    // Water output singletons (ml)
    float W_ZERO = 0.0f, W_LOW = 25.0f, W_MED = 50.0f, W_HIGH = 100.0f;
    
    // Water Rules:
    // R1: IF Moisture is Wet -> Vw = Zero
    float wRule1 = moistWet;
    // R2: IF Moisture is Optimal -> Vw = Low
    float wRule2 = moistOpt;
    // R3: IF Moisture is Dry -> Vw = High
    float wRule3 = moistDry;
    // R4: IF EC is High -> Vw = Medium
    float wRule4 = ecHigh;
    // R5: IF Moisture is Wet AND EC is High -> Vw = Zero
    float wRule5 = std::min(moistWet, ecHigh);

    // Aggregate Water Rules (Max per singleton)
    float weightWZero = std::max(wRule1, wRule5);
    float weightWLow  = wRule2;
    float weightWMed  = wRule4;
    float weightWHigh = wRule3;

    // Fertilizer output singletons (ml)
    float F_ZERO = 0.0f, F_LOW = 10.0f, F_MED = 20.0f, F_HIGH = 40.0f;

    // Fertilizer Rules:
    // R1: IF Nutrient High -> Vf = Zero
    float fRule1 = nutHigh;
    // R2: IF EC High -> Vf = Zero
    float fRule2 = ecHigh;
    // R3: IF Dose High AND Nutrient Low -> Vf = High
    float fRule3 = std::min(doseHigh, nutLow);
    // R4: IF Dose Medium AND Nutrient Low -> Vf = Medium
    float fRule4 = std::min(doseMed, nutLow);
    // R5: IF Dose Low -> Vf = Low
    float fRule5 = doseLow;
    // R6: IF Moisture Wet AND EC Caution -> Vf = Zero
    float fRule6 = std::min(moistWet, ecCaution);
    // R7: IF pH Acidic OR pH Alkaline -> Vf = Medium
    float fRule7 = std::max(phAcidic, phAlkaline);

    // Aggregate Fertilizer Rules (Max per singleton)
    float weightFZero = std::max({fRule1, fRule2, fRule6});
    float weightFLow  = fRule5;
    float weightFMed  = std::max(fRule4, fRule7);
    float weightFHigh = fRule3;

    // 3. Defuzzification (Sugeno Weighted Average)
    
    float sumW = weightWZero + weightWLow + weightWMed + weightWHigh;
    if (sumW > 0.0f) {
        outWaterVol = (weightWZero * W_ZERO + weightWLow * W_LOW + weightWMed * W_MED + weightWHigh * W_HIGH) / sumW;
    } else {
        outWaterVol = 0.0f;
    }

    float sumF = weightFZero + weightFLow + weightFMed + weightFHigh;
    if (sumF > 0.0f) {
        outFertVol = (weightFZero * F_ZERO + weightFLow * F_LOW + weightFMed * F_MED + weightFHigh * F_HIGH) / sumF;
    } else {
        outFertVol = 0.0f;
    }
}
