import pandas as pd
import numpy as np

def analyze_consistency(filename):
    print(f"\n=========================================")
    print(f"  ANALYSIS: CONSISTENCY TEST DATA")
    print(f"=========================================")
    
    try:
        df = pd.read_csv(filename)
    except FileNotFoundError:
        print(f"Error: {filename} not found.")
        return

    # Exclude the Index column for calculations
    cols = [col for col in df.columns if col != 'Index']
    
    N = len(df)
    
    summary = pd.DataFrame()
    summary['Mean'] = df[cols].mean()
    summary['Std Dev (σ)'] = df[cols].std()
    
    # Coefficient of Variation (CV%)
    summary['CV (%)'] = (summary['Std Dev (σ)'] / summary['Mean']) * 100
    
    # NEW: Standard Error of the Mean (SEM)
    # This shows how precise your estimate of the true mean is
    summary['Std Error'] = summary['Std Dev (σ)'] / np.sqrt(N)
    
    # NEW: 95% Margin of Error
    # You can say "The value is Mean ± Margin of Error" with 95% confidence
    summary['95% Margin'] = 1.96 * summary['Std Error']
    
    summary['Min'] = df[cols].min()
    summary['Max'] = df[cols].max()
    summary['Range'] = summary['Max'] - summary['Min']
    
    print(summary.to_string(float_format="%.2f"))
    print(f"\n* Sample Size (N) = {N}")
    print("* CV (%) = (Std Dev / Mean) * 100. Lower is more consistent.")
    print("* 95% Margin = The '±' error margin for your paper at 95% confidence.")

def analyze_response_time(filename):
    print(f"\n=========================================")
    print(f"  ANALYSIS: RESPONSE & STABILIZATION TIME")
    print(f"=========================================")
    
    try:
        df = pd.read_csv(filename)
    except FileNotFoundError:
        print(f"Error: {filename} not found.")
        return

    # Exclude the Trial column
    cols = [col for col in df.columns if col != 'Trial']
    
    N = len(df)
    
    summary = pd.DataFrame()
    summary['Mean (s)'] = df[cols].mean()
    summary['Std Dev (s)'] = df[cols].std()
    
    # NEW: Standard Error for Time
    summary['Std Error (s)'] = summary['Std Dev (s)'] / np.sqrt(N)
    
    # NEW: 95% Margin of Error for Time
    summary['95% Margin (s)'] = 1.96 * summary['Std Error (s)']
    
    summary['Min (s)'] = df[cols].min()
    summary['Max (s)'] = df[cols].max()
    
    print(summary.to_string(float_format="%.2f"))
    print(f"\n* Sample Size (N) = {N}")
    print("* You can report: 'The T90 time was [Mean] ± [Margin] seconds'")

if __name__ == "__main__":
    print("Loading data files...")
    
    # Analyze Consistency Data
    analyze_consistency('consistency_test_data.csv')
    
    # Analyze Response Time Data
    analyze_response_time('t90_response_time_data.csv')
    
    print("\n=========================================")
    print("Tip: You can copy these tables directly into your thesis!")
    print("=========================================")
