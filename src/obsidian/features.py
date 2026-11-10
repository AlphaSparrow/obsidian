import pandas as pd
import numpy as np
import warnings

# Suppress runtime warnings for division by zero in empty rolling windows
warnings.filterwarnings('ignore')

# 1. Tversky & Kahneman (1992) Parameters
ALPHA = 0.88  # Diminishing sensitivity
LAMBDA = 2.25 # Loss aversion
GAMMA_POS = 0.61 # Probability weighting for gains
GAMMA_NEG = 0.69 # Probability weighting for losses

def cpt_weight_pos(p):
    """Probability weighting function for gains"""
    if p <= 0: return 0
    if p >= 1: return 1
    return (p**GAMMA_POS) / ((p**GAMMA_POS + (1-p)**GAMMA_POS)**(1/GAMMA_POS))

def cpt_weight_neg(p):
    """Probability weighting function for losses"""
    if p <= 0: return 0
    if p >= 1: return 1
    return (p**GAMMA_NEG) / ((p**GAMMA_NEG + (1-p)**GAMMA_NEG)**(1/GAMMA_NEG))

def calculate_rolling_pt(returns_array):
    """
    Calculates the Cumulative Prospect Theory (CPT) value for a window of returns.
    Passed via rolling().apply(raw=True) for NumPy speed optimizations.
    """
    returns = returns_array[~np.isnan(returns_array)]
    n = len(returns)
    
    # Require at least 30 valid days in a 60-day window to calculate a reliable score
    if n < 30:
        return np.nan

    # Sort returns to apply cumulative probability weights
    losses = np.sort(returns[returns < 0])
    gains = np.sort(returns[returns >= 0])
    
    cpt_value = 0.0
    
    # Process Gains (ranked from best/highest to worst/lowest)
    # Reversing the sorted gains array so index 0 is the maximum gain
    gains_reversed = gains[::-1]
    for i, g in enumerate(gains_reversed):
        p_current = (i + 1) / n
        p_prev = i / n
        decision_weight = cpt_weight_pos(p_current) - cpt_weight_pos(p_prev)
        cpt_value += decision_weight * (g**ALPHA)
        
    # Process Losses (ranked from worst/lowest to best/highest)
    for i, l in enumerate(losses):
        p_current = (i + 1) / n
        p_prev = i / n
        decision_weight = cpt_weight_neg(p_current) - cpt_weight_neg(p_prev)
        cpt_value += decision_weight * (-LAMBDA * (-l)**ALPHA)
        
    return cpt_value

def engineer_features():
    print("Loading merged dataset...")
    df = pd.read_csv("combined_crypto_data.csv", parse_dates=["timestamp"])
    df = df.sort_values(by=["Coin_ID", "timestamp"]).reset_index(drop=True)
    
    print("Calculating daily returns and base metrics...")
    df['return'] = df.groupby('Coin_ID')['close'].pct_change()
    df['turnover'] = df['volume'] / df['marketCap']
    
    # Rolling Window Size (60 days)
    WINDOW = 60
    
    print(f"Calculating rolling {WINDOW}-day statistics (this may take a minute due to CPT math)...")
    # Base rolling statistics
    df['roll_skew'] = df.groupby('Coin_ID')['return'].rolling(WINDOW, min_periods=30).skew().reset_index(0, drop=True)
    df['roll_max'] = df.groupby('Coin_ID')['return'].rolling(WINDOW, min_periods=30).max().reset_index(0, drop=True)
    df['roll_vol'] = df.groupby('Coin_ID')['return'].rolling(WINDOW, min_periods=30).std().reset_index(0, drop=True)
    
    # Rolling Prospect Theory Score
    df['pt_value'] = df.groupby('Coin_ID')['return'].rolling(WINDOW, min_periods=30)\
                       .apply(calculate_rolling_pt, raw=True).reset_index(0, drop=True)

    print("Constructing cross-sectional Target Labels...")
    # Standardize the components cross-sectionally per day (Z-score) so they are on the same scale before adding
    z_pt = df.groupby('timestamp')['pt_value'].transform(lambda x: (x - x.mean()) / x.std())
    z_skew = df.groupby('timestamp')['roll_skew'].transform(lambda x: (x - x.mean()) / x.std())
    z_max = df.groupby('timestamp')['roll_max'].transform(lambda x: (x - x.mean()) / x.std())
    
    df['composite_lottery_score'] = z_pt + z_skew + z_max
    
    # Label top 25% across the cross-section of coins each day as lottery seeking (1), else (0)
    df['y_lottery_seeking'] = df.groupby('timestamp')['composite_lottery_score']\
                                .transform(lambda x: (x >= x.quantile(0.75)).astype(float))
    
    print("Shifting features to prevent lookahead bias (Data Leakage)...")
    # We want to predict Today's label (y_t) using Yesterday's 60-day rolling features (X_{t-1})
    features_to_shift = ['pt_value', 'roll_skew', 'roll_max', 'roll_vol', 'turnover', 'composite_lottery_score']
    df[features_to_shift] = df.groupby('Coin_ID')[features_to_shift].shift(1)
    
    print("Cleaning dataset...")
    # Drop rows with NaNs caused by pct_change, rolling windows, and the feature shift
    df_clean = df.dropna().reset_index(drop=True)
    
    output_name = "processed_ml_features.csv"
    df_clean.to_csv(output_name, index=False)
    
    print(f"Success! Engineered dataset saved to '{output_name}'.")
    print(f"Final shape for modeling: {df_clean.shape}")

if __name__ == "__main__":
    engineer_features()