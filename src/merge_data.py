import os
import glob
import pandas as pd
import warnings

# Suppress date parsing warnings
warnings.filterwarnings('ignore')

# Define the target directory containing your raw CSV files
dataset_dir = "dataset"
output_file = "combined_crypto_data.csv"

# Locate all CSV files in the dataset folder
csv_files = glob.glob(os.path.join(dataset_dir, "*.csv"))
dataframes = []

print("Starting robust data merge process...\n")

for file in csv_files:
    # 1. Parse the semicolon-delimited data
    df = pd.read_csv(file, sep=";", on_bad_lines='skip')
    
    # 2. Extract and assign the Coin ID
    file_name = os.path.basename(file)
    coin_id = os.path.splitext(file_name)[0]
    df.insert(0, "Coin_ID", coin_id)
    
    # 3. Robust Datetime Parsing (Fixes the missing 2018-2024 data issue)
    date_col = 'timeOpen' if 'timeOpen' in df.columns else 'timestamp'
    
    if date_col in df.columns:
        # Handle mixed formats and standardize to timezone-naive datetimes
        df['timestamp'] = pd.to_datetime(df[date_col], utc=True, format='mixed').dt.tz_localize(None)
        
        # Diagnostic print to prove what is inside the raw CSV
        min_date = df['timestamp'].min().date()
        max_date = df['timestamp'].max().date()
        print(f"[{coin_id}] Loaded {len(df)} rows | Date Range: {min_date} to {max_date}")
        
        # Drop rows where the timestamp failed to parse entirely
        df = df.dropna(subset=['timestamp'])
    else:
        print(f"[{coin_id}] WARNING: Could not find a valid date column.")
        
    dataframes.append(df)

# 4. Combine everything into a single master dataset
if dataframes:
    master_df = pd.concat(dataframes, ignore_index=True)
    
    # Sort chronologically and by asset to maintain strict time-series order
    master_df = master_df.sort_values(by=["Coin_ID", "timestamp"]).reset_index(drop=True)
    
    # Save the combined dataset to the root directory
    master_df.to_csv(output_file, index=False)
    
    print("\n==================================================")
    print(f"Successfully processed and merged {len(csv_files)} files.")
    print(f"Combined dataset saved to '{output_file}' with {len(master_df)} total rows.")
    print(f"Global Date Range: {master_df['timestamp'].min().date()} to {master_df['timestamp'].max().date()}")
    print("==================================================")
else:
    print(f"No CSV files found in the '{dataset_dir}' directory.")