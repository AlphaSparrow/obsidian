import time
import warnings
import contextlib
import joblib
import numpy as np
import pandas as pd
import xgboost as xgb
from tqdm import tqdm
from sklearn.ensemble import AdaBoostClassifier, RandomForestClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import (
    accuracy_score,
    classification_report,
    f1_score,
    precision_score,
    recall_score,
    roc_auc_score,
)
from sklearn.model_selection import RandomizedSearchCV, TimeSeriesSplit
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler

warnings.filterwarnings("ignore")

@contextlib.contextmanager
def tqdm_joblib(tqdm_object):
    """Context manager to patch joblib so tqdm updates on every completed CV fit."""
    class TqdmBatchCompletionCallback(joblib.parallel.BatchCompletionCallBack):
        def __call__(self, *args, **kwargs):
            tqdm_object.update(n=self.batch_size)
            return super().__call__(*args, **kwargs)

    old_batch_callback = joblib.parallel.BatchCompletionCallBack
    joblib.parallel.BatchCompletionCallBack = TqdmBatchCompletionCallback
    try:
        yield tqdm_object
    finally:
        joblib.parallel.BatchCompletionCallBack = old_batch_callback
        tqdm_object.close()

def check_multicollinearity(df, features, threshold=0.85):
    """Prints correlation matrix and warns if any features are highly collinear."""
    print("\n--- 1. Multicollinearity Check (Pearson Correlation) ---")
    corr_matrix = df[features].corr()
    print(corr_matrix.round(3))
    
    high_corr = []
    for i in range(len(corr_matrix.columns)):
        for j in range(i):
            if abs(corr_matrix.iloc[i, j]) > threshold:
                col1 = corr_matrix.columns[i]
                col2 = corr_matrix.columns[j]
                high_corr.append((col1, col2, corr_matrix.iloc[i, j]))
                
    if high_corr:
        print(f"\n[WARNING] High collinearity (> {threshold}) detected between:")
        for c1, c2, val in high_corr:
            print(f"  - {c1} & {c2}: {val:.3f}")
    else:
        print(f"\n[OK] All feature correlations are below the {threshold} threshold.")

def train_and_benchmark():
    pipeline_start = time.time()
    print("Loading engineered features...")
    df = pd.read_csv("processed_ml_features.csv")
    
    # Robust datetime parsing to prevent NaT dropping
    df['timestamp'] = pd.to_datetime(df['timestamp'], utc=True, format='mixed').dt.tz_localize(None)
    
    # Sort strictly by timestamp first, then Coin_ID for chronological TimeSeriesSplit
    df = df.sort_values(by=["timestamp", "Coin_ID"]).reset_index(drop=True)
    
    features = ['pt_value', 'roll_skew', 'roll_max', 'roll_vol', 'turnover']
    target = 'y_lottery_seeking'
    
    # Dynamic 80/20 Chronological Split (Guarantees Train/Test sets are never empty)
    split_date = df['timestamp'].quantile(0.8)
    print(f"\nDynamically splitting data at 80% mark: {split_date.date()}")
    
    train_val_mask = df['timestamp'] <= split_date
    test_mask = df['timestamp'] > split_date
    
    X_train_val = df.loc[train_val_mask, features]
    y_train_val = df.loc[train_val_mask, target]
    X_test = df.loc[test_mask, features]
    y_test = df.loc[test_mask, target]
    
    print(f"Train/Val Shape (Oldest 80%): {X_train_val.shape}")
    print(f"Out-of-Sample Test Shape (Newest 20%): {X_test.shape}")
    
    # 1. Check Multicollinearity on historical training data
    check_multicollinearity(df.loc[train_val_mask], features)
    
    # 2. Class Imbalance Ratio
    neg_count = (y_train_val == 0).sum()
    pos_count = (y_train_val == 1).sum()
    imbalance_ratio = neg_count / pos_count if pos_count > 0 else 1
    print(f"\nClass Imbalance Ratio (Neg/Pos): {imbalance_ratio:.2f}")
    
    # 3. Walk-Forward Time-Series Cross-Validator
    N_SPLITS = 5
    N_ITER = 12
    tscv = TimeSeriesSplit(n_splits=N_SPLITS)
    
    # 4. Define Models and Hyperparameter Grids
    models_config = {
        "Logistic_Regression": {
            "estimator": Pipeline([
                ("scaler", StandardScaler()),
                ("clf", LogisticRegression(class_weight="balanced", max_iter=1000, random_state=42))
            ]),
            "param_grid": {
                "clf__C": [0.01, 0.1, 1.0, 10.0],
                "clf__penalty": ["l2"]
            },
            "max_iter": 4
        },
        "Random_Forest": {
            "estimator": RandomForestClassifier(class_weight="balanced", random_state=42, n_jobs=-1),
            "param_grid": {
                "n_estimators": [200, 400, 600],
                "max_depth": [5, 10, 15, None],
                "min_samples_split": [2, 5, 10],
                "min_samples_leaf": [1, 2, 4],
                "max_features": ["sqrt", "log2"]
            },
            "max_iter": N_ITER
        },
        "AdaBoost": {
            "estimator": AdaBoostClassifier(random_state=42),
            "param_grid": {
                "n_estimators": [50, 100, 200, 300],
                "learning_rate": [0.01, 0.05, 0.1, 0.5, 1.0]
            },
            "max_iter": N_ITER
        },
        "XGBoost": {
            "estimator": xgb.XGBClassifier(
                scale_pos_weight=imbalance_ratio,
                eval_metric="auc",
                random_state=42,
                n_jobs=-1
            ),
            "param_grid": {
                "n_estimators": [200, 400, 600],
                "max_depth": [3, 5, 7],
                "learning_rate": [0.01, 0.05, 0.1],
                "subsample": [0.7, 0.8, 0.9],
                "colsample_bytree": [0.7, 0.8, 0.9],
                "gamma": [0, 0.1, 0.3],
                "min_child_weight": [1, 3, 5]
            },
            "max_iter": N_ITER
        }
    }
    
    # 5. Run RandomizedSearchCV with Live Progress Bars
    print("\n--- 2. Training & Tuning Models via Walk-Forward CV ---")
    results = []
    trained_models = {}
    total_models = len(models_config)
    
    for idx, (name, config) in enumerate(models_config.items(), start=1):
        model_start = time.time()
        n_iter = config["max_iter"]
        total_fits = n_iter * N_SPLITS
        
        print(f"\n[{idx}/{total_models}] Tuning {name} ({n_iter} candidates × {N_SPLITS} CV folds = {total_fits} fits)...")
        
        search = RandomizedSearchCV(
            estimator=config["estimator"],
            param_distributions=config["param_grid"],
            n_iter=n_iter,
            scoring="roc_auc",
            cv=tscv,
            n_jobs=-1,
            random_state=42
        )
        
        with tqdm_joblib(tqdm(total=total_fits, desc=f"  {name}", unit="fit", ncols=85)):
            search.fit(X_train_val, y_train_val)
            
        best_model = search.best_estimator_
        trained_models[name] = best_model
        
        # Out-of-Sample Predictions
        y_pred = best_model.predict(X_test)
        y_proba = best_model.predict_proba(X_test)[:, 1] if hasattr(best_model, "predict_proba") else y_pred
        
        # Calculate Metrics
        cv_auc = search.best_score_
        test_auc = roc_auc_score(y_test, y_proba)
        f1 = f1_score(y_test, y_pred, zero_division=0)
        prec = precision_score(y_test, y_pred, zero_division=0)
        rec = recall_score(y_test, y_pred, zero_division=0)
        acc = accuracy_score(y_test, y_pred)
        elapsed = time.time() - model_start
        
        print(f"  ✓ Done in {elapsed:.1f}s | Best Params: {search.best_params_}")
        print(f"  ✓ CV AUC: {cv_auc:.4f} | Test AUC: {test_auc:.4f} | Test F1: {f1:.4f}")
        
        results.append({
            "Model": name,
            "CV_AUC": round(cv_auc, 4),
            "Test_AUC": round(test_auc, 4),
            "Test_F1": round(f1, 4),
            "Test_Precision": round(prec, 4),
            "Test_Recall": round(rec, 4),
            "Test_Accuracy": round(acc, 4),
            "Train_Time_Sec": round(elapsed, 1)
        })
        
    # 6. Compile and Save Benchmarking Leaderboard
    results_df = pd.DataFrame(results).sort_values(by="Test_AUC", ascending=False).reset_index(drop=True)
    print("\n==================================================================================")
    print("FINAL OUT-OF-SAMPLE BENCHMARK LEADERBOARD")
    print("==================================================================================")
    print(results_df.to_string(index=False))
    
    results_df.to_csv("model_comparison_metrics.csv", index=False)
    print("\nSaved benchmark table to 'model_comparison_metrics.csv'.")
    
    # 7. Detailed Report & Model Export for SHAP
    best_xgb = trained_models["XGBoost"]
    print("\n--- Detailed Classification Report for Tuned XGBoost ---")
    print(classification_report(y_test, best_xgb.predict(X_test), zero_division=0))
    
    joblib.dump(best_xgb, "xgboost_lottery_model.pkl")
    joblib.dump(trained_models["Random_Forest"], "rf_lottery_model.pkl")
    
    total_time = time.time() - pipeline_start
    print(f"All models trained & saved in {total_time:.1f}s total. Ready for SHAP explainer!")

if __name__ == "__main__":
    train_and_benchmark()