import csv
import os
from datetime import datetime

# Configuration
FILENAME = "thesis_plant_data.csv"
PLANT_COUNT = 16
HEADERS = [
    "Date", 
    "Plant_ID", 
    "Height_mm", 
    "Leaf_Count", 
    "Rosette_Diameter_mm", 
    "Max_Leaf_Length_mm", 
    "Max_Leaf_Width_mm"
]

def initialize_csv():
    if not os.path.exists(FILENAME):
        with open(FILENAME, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(HEADERS)
        print(f"Initialized {FILENAME}")

def get_input(prompt, type_func=float):
    while True:
        user_input = input(prompt).strip()
        if user_input.lower() == 'q':
            return 'q'
        if not user_input:
            print("Input cannot be empty. Enter a value or 'q' to skip.")
            continue
        try:
            return type_func(user_input)
        except ValueError:
            print(f"Invalid input. Please enter a {type_func.__name__} or 'q' to skip.")

def main():
    initialize_csv()
    date_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    print("\n" + "="*40)
    print(f"   AGRI_3D THESIS DATA LOGGER")
    print(f"   Session Date: {date_str}")
    print("="*40)
    print("Instructions: Enter measurements for 16 plants.")
    print("Type 'q' to skip a plant if needed.\n")

    records = []
    
    try:
        for i in range(1, PLANT_COUNT + 1):
            print(f"\n>>> Plant {i:02d} / {PLANT_COUNT}")
            
            height = get_input("  Height (mm): ")
            if height == 'q': continue
            
            leaf_count = get_input("  Leaf Count: ", int)
            if leaf_count == 'q': continue
            
            rosette_dia = get_input("  Rosette Diameter (mm): ")
            if rosette_dia == 'q': continue
            
            leaf_len = get_input("  Max Leaf Length (mm): ")
            if leaf_len == 'q': continue
            
            leaf_wid = get_input("  Max Leaf Width (mm): ")
            if leaf_wid == 'q': continue
            
            records.append([
                date_str,
                f"Plant_{i:02d}",
                height,
                leaf_count,
                rosette_dia,
                leaf_len,
                leaf_wid
            ])
            print(f"  [✓] Data captured for Plant {i:02d}")

        if records:
            with open(FILENAME, mode='a', newline='') as file:
                writer = csv.writer(file)
                writer.writerows(records)
            print(f"\n[SUCCESS] {len(records)} plant records appended to {FILENAME}")
        else:
            print("\n[INFO] No records were saved this session.")

    except KeyboardInterrupt:
        print("\n\n[EXIT] Session terminated by user. Data not saved.")

if __name__ == "__main__":
    main()
