#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The auto-generated XGBoost model function
// Input features: N, P, K, temperature, humidity, pH
double score(double * input);

#ifdef __cplusplus
}
#endif
