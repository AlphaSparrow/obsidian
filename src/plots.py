import os
import warnings
from pathlib import Path
import joblib
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from sklearn.metrics import auc, average_precision_score, precision_recall_curve, roc_curve

warnings.filterwarnings("ignore")

def find_file(project_root: Path, filename: str, subfolders: list) -> Path:
    """Search for a file in specified subfolders first, then root."""
    for folder in subfolders:
        candidate = project_root / folder / filename
        if candidate.exists():
            return candidate
    candidate_root = project_root / filename
    if candidate_root.exists():
        return candidate_root
    return None

def generate_report_plots():
    # Resolve directory paths relative to this script's location
    current_dir = Path(__file__).resolve().parent
    project_root = current_dir.parent if current_dir.name == "src" else current_dir
    plots_dir = project_root / "plots"
    plots_dir.mkdir(parents=True, exist_ok=True)

    # Locate dataset (checks dataset/ then root)
    data_path = find_file(project_root, "processed_ml_features.csv", ["dataset", "data"])
    if data_path is None:
        print(f"Error: 'processed_ml_features.csv' not found in '{project_root / 'dataset'}' or root '{project_root}'.")
        return

    # Locate model files (checks models/, artifacts/, then root)
    rf_model_path = find_file(project_root, "rf_lottery_model.pkl", ["models", "artifacts"])
    xgb_model_path = find_file(project_root, "xgboost_lottery_model.pkl", ["models", "artifacts"])

    if rf_model_path is None or xgb_model_path is None:
        print(f"Error: Model .pkl files not found in models/, artifacts/, or root. Run train.py first.")
        return

    print(f"Loading data from: {data_path}")
    print(f"Loading RF model from: {rf_model_path}")
    print(f"Loading XGB model from: {xgb_model_path}")
    
    df = pd.read_csv(data_path)
    
    # Robust datetime parsing
    df['timestamp'] = pd.to_datetime(df['timestamp'], utc=True, format='mixed').dt.tz_localize(None)
    df = df.sort_values(by=["timestamp", "Coin_ID"]).reset_index(drop=True)
    
    features = ['pt_value', 'roll_skew', 'roll_max', 'roll_vol', 'turnover']
    target = 'y_lottery_seeking'
    
    # Extract unseen 20% test set
    split_date = df['timestamp'].quantile(0.8)
    test_mask = df['timestamp'] > split_date
    df_test = df.loc[test_mask].copy()
    
    X_test = df_test[features]
    y_test = df_test[target]
    
    rf_model = joblib.load(rf_model_path)
    xgb_model = joblib.load(xgb_model_path)

    # Get Probabilities for ROC/PR
    rf_probs = rf_model.predict_proba(X_test)[:, 1]
    xgb_probs = xgb_model.predict_proba(X_test)[:, 1]

    print("Generating ROC Curve...")
    plt.figure(figsize=(8, 6))
    
    fpr_rf, tpr_rf, _ = roc_curve(y_test, rf_probs)
    fpr_xgb, tpr_xgb, _ = roc_curve(y_test, xgb_probs)
    
    plt.plot(fpr_rf, tpr_rf, color='blue', lw=2, label=f'Random Forest (AUC = {auc(fpr_rf, tpr_rf):.4f})')
    plt.plot(fpr_xgb, tpr_xgb, color='green', lw=2, label=f'XGBoost (AUC = {auc(fpr_xgb, tpr_xgb):.4f})')
    plt.plot([0, 1], [0, 1], color='gray', lw=2, linestyle='--', label='Random Guess')
    
    plt.xlim([0.0, 1.0])
    plt.ylim([0.0, 1.05])
    plt.xlabel('False Positive Rate (1 - Specificity)', fontsize=12)
    plt.ylabel('True Positive Rate (Recall)', fontsize=12)
    plt.title('ROC Curve: Lottery-Seeking Prediction (Out-of-Sample)', fontsize=14)
    plt.legend(loc="lower right")
    plt.grid(alpha=0.3)
    
    plt.tight_layout()
    roc_out = plots_dir / "plot_roc_curve.png"
    plt.savefig(roc_out, dpi=300)
    plt.close()
    print(f"✓ Saved '{roc_out}'")

    print("Generating Precision-Recall Curve...")
    plt.figure(figsize=(8, 6))
    
    prec_rf, rec_rf, _ = precision_recall_curve(y_test, rf_probs)
    prec_xgb, rec_xgb, _ = precision_recall_curve(y_test, xgb_probs)
    
    rf_ap = average_precision_score(y_test, rf_probs)
    xgb_ap = average_precision_score(y_test, xgb_probs)
    
    plt.plot(rec_rf, prec_rf, color='blue', lw=2, label=f'Random Forest (AP = {rf_ap:.4f})')
    plt.plot(rec_xgb, prec_xgb, color='green', lw=2, label=f'XGBoost (AP = {xgb_ap:.4f})')
    
    plt.xlabel('Recall (True Positive Rate)', fontsize=12)
    plt.ylabel('Precision (Positive Predictive Value)', fontsize=12)
    plt.title('Precision-Recall Curve (Handling 75/25 Imbalance)', fontsize=14)
    plt.legend(loc="upper right")
    plt.grid(alpha=0.3)
    
    plt.tight_layout()
    pr_out = plots_dir / "plot_pr_curve.png"
    plt.savefig(pr_out, dpi=300)
    plt.close()
    print(f"✓ Saved '{pr_out}'")

    print("Generating Time-Series Overlay for Bitcoin...")
    btc_test = df_test[df_test['Coin_ID'].str.lower().isin(['bitcoin', 'btc'])].copy()
    
    if len(btc_test) > 0 and 'close' in btc_test.columns:
        btc_test['predicted_lottery'] = rf_model.predict(btc_test[features])
        
        fig, ax = plt.subplots(figsize=(12, 6))
        ax.plot(btc_test['timestamp'], btc_test['close'], color='black', lw=1.5, label='BTC Closing Price')
        
        ax.fill_between(
            btc_test['timestamp'], 
            ax.get_ylim()[0], 
            ax.get_ylim()[1], 
            where=(btc_test['predicted_lottery'] == 1), 
            color='red', 
            alpha=0.25, 
            label='Predicted Speculative Window (y_pred=1)'
        )
        
        plt.xlabel('Date', fontsize=12)
        plt.ylabel('Closing Price (USD)', fontsize=12)
        plt.title('Model Prediction Overlay: Bitcoin (Out-of-Sample Test Period)', fontsize=14)
        plt.legend(loc='upper left')
        plt.grid(alpha=0.3)
        plt.xticks(rotation=45)
        
        plt.tight_layout()
        overlay_out = plots_dir / "plot_time_series_overlay.png"
        plt.savefig(overlay_out, dpi=300)
        plt.close()
        print(f"✓ Saved '{overlay_out}'")
    else:
        print("[WARNING] Could not generate Bitcoin overlay (either 'bitcoin' not in dataset or 'close' column missing).")

    print("\n==================================================")
    print(f"Success! All report visualizations have been saved in '{plots_dir}'.")
    print("==================================================")

if __name__ == "__main__":
    generate_report_plots()