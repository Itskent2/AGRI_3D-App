import pandas as pd
import numpy as np
import xgboost as xgb
from sklearn.model_selection import train_test_split
import m2cgen as m2c
import re
import os

DATASET_PATH = r"c:\Users\Joshua B. Ygot\Downloads\Farmbot Ver3\Flutter\AGRI_3D-App\lib\reference\xgboost thing\Agri3D_LeafyGreens_Dataset_CLEANED.csv"
C_MODEL_PATH = r"c:\Users\Joshua B. Ygot\Downloads\Farmbot Ver3\Flutter\AGRI_3D-App\lib\reference\AI-agri3d\src\XGBoost\xgboost_model.c"

print("Loading dataset...")
df = pd.read_csv(DATASET_PATH)

df['total_dosage_kg_per_ha'] = (
    df['N_dosage_kg_per_ha'].fillna(0) + 
    df['P2O5_dosage_kg_per_ha'].fillna(0) + 
    df['K2O_dosage_kg_per_ha'].fillna(0)
)
df['total_dosage_ml_per_ha'] = (df['total_dosage_kg_per_ha'] / 1.30) * 1000

sensor_features = ['N', 'P', 'K', 'temperature', 'humidity', 'pH']
data = df[sensor_features + ['total_dosage_ml_per_ha']].dropna()
X = data[sensor_features]
y = data['total_dosage_ml_per_ha']

print("Training constrained XGBoost model (n_estimators=60, max_depth=5)")
# Medium constraint for embedded!
model = xgb.XGBRegressor(n_estimators=60, max_depth=5, learning_rate=0.15, random_state=42)
model.fit(X, y)

print("Exporting to C...")
c_code = m2c.export_to_c(model)
c_code = c_code.replace("double", "float")
c_code = re.sub(r'\bnan\b', 'NAN', c_code)

esp32_header = """// Auto-generated Optimized XGBoost Micro-Model C code for ESP32
// Input features: N, P, K, temperature, humidity, pH

#include <math.h>
#include <stdint.h>

#ifndef NAN
    #define NAN (0.0f/0.0f)
#endif
"""

with open(C_MODEL_PATH, "w") as f:
    f.write(esp32_header)
    f.write(c_code)

size_kb = os.path.getsize(C_MODEL_PATH) / 1024
print(f"Done generating new tiny XGBoost model! Size: {size_kb:.2f} KB")
