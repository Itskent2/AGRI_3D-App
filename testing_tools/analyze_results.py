import csv
import math
import statistics
import os
import glob
import sys

def analyze_dataset(filepath):
    filename = os.path.basename(filepath)
    print(f"{'='*50}")
    print(f" Analyzing Dataset: {filename}")
    print(f"{'='*50}")
    
    with open(filepath, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration:
            print("File is empty.\n")
            return
            
        # Identify which columns to compare
        if 'Target_Diagonal' in header and 'Actual_Diagonal' in header:
            target_idx = header.index('Target_Diagonal')
            actual_idx = header.index('Actual_Diagonal')
            test_type = "Concurrent XY (Diagonal)"
        else:
            target_idx = -1
            actual_idx = -1
            for i, col in enumerate(header):
                if col.startswith('Target_') and not col.endswith('Diagonal'):
                    target_idx = i
                if col.startswith('Actual_') and not col.endswith('Diagonal'):
                    actual_idx = i
            
            if target_idx != -1:
                test_type = f"Isolated {header[target_idx].replace('Target_', '')}"
            else:
                test_type = "Unknown"
                
        if target_idx == -1 or actual_idx == -1:
            print("Error: Could not find 'Target' and 'Actual' columns in the CSV header.")
            print(f"Header found: {header}\n")
            return
            
        errors = []
        abs_errors = []
        
        for row_num, row in enumerate(reader, start=2):
            if not row or len(row) <= max(target_idx, actual_idx):
                continue # Skip empty rows or incomplete rows
            try:
                target = float(row[target_idx])
                actual = float(row[actual_idx])
                
                # Calculate Error (Actual - Target)
                # Positive error means it overshot, negative means it undershot.
                error = actual - target
                
                errors.append(error)
                abs_errors.append(abs(error))
            except ValueError:
                print(f"  [Warning] Skipping row {row_num} due to non-numeric data: {row}")
                
        n = len(errors)
        if n == 0:
            print("No valid numerical data found to analyze.\n")
            return
            
        # --- Statistical Analysis ---
        # 1. Mean Error (Accuracy / Bias)
        mean_error = sum(errors) / n
        
        # 2. Mean Absolute Error (MAE)
        mae = sum(abs_errors) / n
        
        # 3. Root Mean Square Error (RMSE)
        rmse = math.sqrt(sum(e**2 for e in errors) / n)
        
        # 4. Standard Deviation of the Error (Precision)
        # Uses sample standard deviation (n-1). If n=1, stdev is 0.
        if n > 1:
            stdev = statistics.stdev(errors)
        else:
            stdev = 0.0
            
        # 5. Maximum Error observed
        max_err = max(abs_errors)
        
        print(f"Test Type              : {test_type}")
        print(f"Total Trials (N)       : {n}")
        print(f"--------------------------------------------------")
        print(f"Mean Error (Bias)      : {mean_error:8.4f} mm  <-- (Accuracy: closer to 0 is better)")
        print(f"Mean Abs Error (MAE)   : {mae:8.4f} mm")
        print(f"Root Mean Sq Err (RMSE): {rmse:8.4f} mm")
        print(f"Std Dev (Precision)    : {stdev:8.4f} mm  <-- (Consistency: closer to 0 is better)")
        print(f"Max Absolute Error     : {max_err:8.4f} mm")
        print(f"{'='*50}\n")

def main():
    print("\n" + "*"*50)
    print("   AGRI-3D Automated Data Analysis Tool")
    print("*"*50 + "\n")
    
    # 1. Check if user passed specific files via command line args
    if len(sys.argv) > 1:
        files_to_process = sys.argv[1:]
    else:
        # 2. Otherwise, automatically discover results_*.csv files in the directory
        # Check current directory and parent directory (where flutter app root usually is)
        current_dir = os.getcwd()
        parent_dir = os.path.dirname(os.path.abspath(__file__))
        app_root_dir = os.path.dirname(parent_dir)
        
        search_paths = [
            os.path.join(current_dir, "results_*.csv"),
            os.path.join(app_root_dir, "results_*.csv")
        ]
        
        files_to_process = []
        seen_paths = set()
        for path in search_paths:
            for filepath in glob.glob(path):
                norm_path = os.path.normcase(os.path.abspath(filepath))
                if norm_path not in seen_paths:
                    seen_paths.add(norm_path)
                    files_to_process.append(filepath)
        
    if not files_to_process:
        print("No result CSV files found.")
        print("Please ensure you run this script from the directory containing the CSVs,")
        print("or pass the CSV file paths as arguments:")
        print("  python analyze_results.py ../results_X_123.csv")
        return

    # Sort files by name so they appear in a consistent order
    files_to_process.sort()

    print(f"Found {len(files_to_process)} dataset(s) to analyze.\n")
    for filepath in files_to_process:
        analyze_dataset(filepath)
        
    print("Analysis complete. You can use these metrics for your research paper.")

if __name__ == "__main__":
    main()
