import json
import os
import random
import time

def load_calibration():
    flow_rates = {
        "Water": 24.0, # Default from ESP32
        "Fertilizer": 2.0
    }
    filename = "pump_calibration.json"
    if os.path.exists(filename):
        try:
            with open(filename, 'r') as f:
                data = json.load(f)
                flow_rates.update(data)
                print(f"[✓] Loaded calibration from {filename}")
        except Exception as e:
            print(f"[⚠] Error loading calibration: {e}")
    return flow_rates

def main():
    print("="*40)
    print("     PUMP TESTER - DRY RUN")
    print("="*40)
    
    flow_rates = load_calibration()
    
    seed_input = input("Enter random seed (optional): ")
    if seed_input.strip():
        try:
            seed = int(seed_input)
            random.seed(seed)
            print(f"[✓] Using random seed: {seed}")
        except ValueError:
            print("[⚠] Invalid seed, using random sequence.")
            
    print("\nSelect Sector/Pump to Test:")
    print("1. Water")
    print("2. Fertilizer")
    choice = input("Choice (1-2): ")
    
    pump_name = "Water" if choice == "1" else "Fertilizer"
    
    num_trials_input = input("Number of trials [Default 25]: ")
    num_trials = int(num_trials_input) if num_trials_input.strip() else 25
    
    print(f"\nUsing flow rate for {pump_name}: {flow_rates[pump_name]} ml/sec")
    print("\n" + "="*50)
    print(f"{'Trial':<6} | {'Duration (s)':<12} | {'Target Vol (ml)':<15}")
    print("="*50)
    
    for trial in range(1, num_trials + 1):
        if pump_name == "Water":
            duration = round(random.uniform(1.0, 8.0), 2)
        else:
            duration = round(random.uniform(1.0, 30.0), 2)
            
        target_volume = duration * flow_rates[pump_name]
        print(f"{trial:<6} | {duration:<12.2f} | {target_volume:<15.1f}")
        
    print("="*50)
    print("\nThis is a dry run. To use these exact values in the real test,")
    print("we would need to save them to a plan file. Let me know if you want that!")

if __name__ == "__main__":
    main()
