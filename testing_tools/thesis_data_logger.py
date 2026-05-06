import csv
from datetime import datetime
import os

# ANSI Colors for a premium CLI experience
CLR_HEADER = "\033[95m"
CLR_PLANT = "\033[94m"
CLR_DATA = "\033[96m"
CLR_HIST = "\033[90m" # Grey for history
CLR_SUCCESS = "\033[92m"
CLR_WARNING = "\033[93m"
CLR_RESET = "\033[0m"
CLR_BOLD = "\033[1m"

# Configuration
FILENAME = "thesis_plant_data.csv"
PLANT_COUNT = 16
HEADERS = [
    "Day",
    "Date", 
    "Plant_ID", 
    "Height_mm", 
    "Leaf_Count", 
    "Rosette_Diameter_mm", 
    "Max_Leaf_Length_mm", 
    "Max_Leaf_Width_mm"
]

def get_session_info():
    """Calculates the current experimental day based on the first entry in the CSV."""
    day_number = 1
    current_date = datetime.now().date()
    date_str = current_date.strftime("%Y-%m-%d")
    
    if os.path.exists(FILENAME):
        try:
            with open(FILENAME, mode='r') as file:
                reader = csv.DictReader(file)
                first_row = next(reader, None)
                if first_row and 'Date' in first_row:
                    # Parse the first date recorded to establish 'Day 1'
                    start_date_str = first_row['Date'].split(' ')[0] # Handle old format if exists
                    start_date = datetime.strptime(start_date_str, "%Y-%m-%d").date()
                    day_number = (current_date - start_date).days + 1
        except Exception as e:
            print(f"[Warning] Could not calculate Day number: {e}")
            
    return day_number, date_str

def get_history(plant_id, count=3):
    """Retrieves the last N entries for a specific plant from the CSV."""
    history = []
    if os.path.exists(FILENAME):
        try:
            with open(FILENAME, mode='r') as file:
                # Read all lines to find the last N matches for this plant
                reader = list(csv.DictReader(file))
                # Filter for this plant and take the last 'count' items
                plant_rows = [row for row in reader if row['Plant_ID'] == plant_id]
                history = plant_rows[-count:]
        except Exception as e:
            pass # Silently fail for history display
    return history

def display_history(history):
    """Prints a compact table of historical data."""
    if not history:
        return
    
    print(f"  {CLR_HIST}Recent History:{CLR_RESET}")
    print(f"  {CLR_HIST}{'Day':<4} | {'Height':<8} | {'Leaves':<6} | {'Rosette':<8} | {'Max L':<6} | {'Max W':<6}{CLR_RESET}")
    print(f"  {CLR_HIST}{'-'*50}{CLR_RESET}")
    for row in history:
        print(f"  {CLR_HIST}{row['Day']:<4} | {row['Height_mm']:<8} | {row['Leaf_Count']:<6} | {row['Rosette_Diameter_mm']:<8} | {row['Max_Leaf_Length_mm']:<6} | {row['Max_Leaf_Width_mm']:<6}{CLR_RESET}")
    print()

def initialize_csv():
    if not os.path.exists(FILENAME):
        with open(FILENAME, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(HEADERS)
        print(f"Initialized new data file: {FILENAME}")
    else:
        print(f"Using existing data file: {FILENAME}")

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
    day_number, date_str = get_session_info()
    
    print(f"\n{CLR_BOLD}{CLR_HEADER}" + "="*45)
    print(f"   AGRI_3D THESIS DATA LOGGER")
    print(f"   Date: {date_str} | Experimental Day: {day_number}")
    print("="*45 + f"{CLR_RESET}")
    print(f"Recording data for {CLR_BOLD}{PLANT_COUNT}{CLR_RESET} plants.")
    print(f"Type {CLR_WARNING}'q'{CLR_RESET} to skip a plant if needed.\n")

    records = []
    
    try:
        for i in range(1, PLANT_COUNT + 1):
            plant_id = f"Plant_{i:02d}"
            print(f"\n{CLR_PLANT}{CLR_BOLD}>>> {plant_id} / {PLANT_COUNT}{CLR_RESET}")
            
            # Show historical data for context
            history = get_history(plant_id)
            display_history(history)
            
            height = get_input(f"  {CLR_DATA}Height (mm): {CLR_RESET}")
            if height == 'q': continue
            
            leaf_count = get_input(f"  {CLR_DATA}Leaf Count: {CLR_RESET}", int)
            if leaf_count == 'q': continue
            
            rosette_dia = get_input(f"  {CLR_DATA}Rosette Diameter (mm): {CLR_RESET}")
            if rosette_dia == 'q': continue
            
            leaf_len = get_input(f"  {CLR_DATA}Max Leaf Length (mm): {CLR_RESET}")
            if leaf_len == 'q': continue
            
            leaf_wid = get_input(f"  {CLR_DATA}Max Leaf Width (mm): {CLR_RESET}")
            if leaf_wid == 'q': continue
            
            records.append([
                day_number,
                date_str,
                plant_id,
                height,
                leaf_count,
                rosette_dia,
                leaf_len,
                leaf_wid
            ])
            print(f"  {CLR_SUCCESS}[✓] Data captured for {plant_id}{CLR_RESET}")

        if records:
            with open(FILENAME, mode='a', newline='') as file:
                writer = csv.writer(file)
                writer.writerows(records)
            print(f"\n{CLR_SUCCESS}{CLR_BOLD}[SUCCESS] {len(records)} plant records appended to {FILENAME}{CLR_RESET}")
        else:
            print(f"\n{CLR_WARNING}[INFO] No records were saved this session.{CLR_RESET}")

    except KeyboardInterrupt:
        print("\n\n[EXIT] Session terminated by user. Data not saved.")

if __name__ == "__main__":
    main()
