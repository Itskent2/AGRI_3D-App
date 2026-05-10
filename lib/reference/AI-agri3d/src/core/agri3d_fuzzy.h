#pragma once
#include <Arduino.h>

/**
 * @brief Custom lightweight Fuzzy Logic Controller for Agri3D.
 * Computes required Irrigation Volume (ml) and Fertigation Volume (ml)
 * based on soil sensor readings and XGBoost predicted dosage.
 */
class Agri3DFuzzyController {
public:
    Agri3DFuzzyController() {}

    /**
     * Compute irrigation and fertigation volumes.
     * @param moisture Soil moisture percentage
     * @param ec Electrical conductivity (us/cm)
     * @param n Nitrogen (mg/kg)
     * @param p Phosphorus (mg/kg)
     * @param k Potassium (mg/kg)
     * @param ph Soil pH
     * @param predictedDosage Dosage output from XGBoost model (0-100 scale assumed)
     * @param outWaterVol Output reference for required water volume (ml)
     * @param outFertVol Output reference for required fertilizer volume (ml)
     */
    void evaluate(float moisture, float ec, float n, float p, float k, float ph, float predictedDosage, float &outWaterVol, float &outFertVol);

private:
    // Membership function helpers
    float fuzL(float val, float a, float b);
    float fuzR(float val, float a, float b);
    float fuzTri(float val, float a, float b, float c);
    float fuzTrap(float val, float a, float b, float c, float d);
};
