import xgboost as xgb
import os

# 1. Hardcoded exact locations
target_folder = r"C:\Users\hsu.ss_\Desktop\XGBoost training"
model_path = os.path.join(target_folder, "xgboost_fertilizer_model.json")
output_path = os.path.join(target_folder, "my_model_logic.txt")

print(f"DEBUG: Looking for model at: {model_path}")

try:
    # 2. Load model
    model = xgb.Booster()
    model.load_model(model_path)
    
    # 3. Dump the logic to the specific path
    model.dump_model(output_path)
    
    # 4. Verify
    if os.path.exists(output_path):
        print(f"✅ SUCCESS! File strictly saved to: {output_path}")
        print("Please check that folder again.")
    else:
        print("❌ ERROR: The script finished but the file was not created.")

except Exception as e:
    print(f"❌ ERROR: {e}")