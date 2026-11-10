import time
import warnings
import joblib
import numpy as np
import pandas as pd
import shap
import matplotlib.pyplot as plt

warnings.filterwarnings("ignore")

def generate_shap_explanations():
    print("Loading engineered features and dynamic test set...")
    df = pd.read_csv("processed_ml_features.csv")
    
    # 1. Robust datetime parsing
    df['timestamp'] = pd.to_datetime(df['timestamp'], utc=True, format='mixed').dt.tz_localize(None)
    df = df.sort_values(by=["timestamp", "Coin_ID"]).reset_index(drop=True)
    
    features = ['pt_value', 'roll_skew', 'roll_max', 'roll_vol', 'turnover']
    
    # 2. Dynamic 80/20 Split
    split_date = df['timestamp'].quantile(0.8)
    test_mask = df['timestamp'] > split_date
    X_test = df.loc[test_mask, features].reset_index(drop=True)
    
    print(f"Test Set Extracted: {X_test.shape[0]} rows (Newest 20%)")
    
    # 3. Load the winning Random Forest model
    model_path = "rf_lottery_model.pkl"
    print(f"Loading winning model: {model_path}...")
    model = joblib.load(model_path)
    
    # 4. Calculate SHAP values
    print("\nCalculating SHAP values (C-optimized TreeExplainer)...")
    start_time = time.time()
    
    explainer = shap.TreeExplainer(model)
    shap_values = explainer.shap_values(X_test)
    
    # Handle both older SHAP (list of 2D arrays) and newer SHAP (3D array: samples x features x classes)
    if isinstance(shap_values, list):
        shap_values_target = shap_values[1]
    elif isinstance(shap_values, np.ndarray) and shap_values.ndim == 3:
        shap_values_target = shap_values[:, :, 1]
    else:
        shap_values_target = shap_values
        
    print(f"✓ SHAP values calculated in {time.time() - start_time:.2f}s | Target Matrix Shape: {shap_values_target.shape}")
    
    # 5. Generate and Save Beeswarm Summary Plot (Class 1)
    print("\nGenerating SHAP Summary Beeswarm Plot...")
    plt.figure(figsize=(10, 6))
    shap.summary_plot(shap_values_target, X_test, show=False)
    plt.title("SHAP Feature Importance: Lottery-Seeking Behavior (Class 1)", pad=15)
    plt.tight_layout()
    plt.savefig("shap_summary_plot.png", dpi=300, bbox_inches='tight')
    plt.close()
    print("✓ Saved 'shap_summary_plot.png'")
    
    # 6. Generate and Save Dependence Plot for Prospect Theory (pt_value)
    print("Generating SHAP Dependence Plot for pt_value...")
    fig, ax = plt.subplots(figsize=(8, 6))
    shap.dependence_plot(
        "pt_value",
        shap_values_target,
        X_test,
        interaction_index="auto",
        ax=ax,
        show=False
    )
    ax.set_title("Dependence Plot: How Prospect Theory Scores Drive Speculation", pad=12)
    plt.tight_layout()
    plt.savefig("shap_dependence_pt.png", dpi=300, bbox_inches='tight')
    plt.close()
    print("✓ Saved 'shap_dependence_pt.png'")
    
    print("\n==================================================")
    print("Success! Both SHAP plots have been generated and saved.")
    print("==================================================")

if __name__ == "__main__":
    generate_shap_explanations()