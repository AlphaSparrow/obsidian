# 01 · Behavioral Finance & Machine Learning Pipeline

> Architectural and theoretical overview of the Cumulative Prospect Theory (CPT) feature engineering, walk-forward chronological evaluation, and SHAP explainability pipeline.

---

## 1. Theoretical Foundation: Cumulative Prospect Theory (CPT)

The behavioral research layer operationalizes the empirical findings from quantitative finance literature regarding retail speculation and lottery-seeking behavior in crypto assets (Tversky & Kahneman, 1992; Barberis, 2013).

### 1.1 Mathematical Formulation

Traditional expected utility fails to capture observed investor skewness preference and loss aversion. Cumulative Prospect Theory replaces linear probabilities with cumulative decision weights and symmetric utility with an S-shaped value function.

#### Value Function $v(x)$
$$
v(x) = \begin{cases}
x^\alpha & \text{for } x \ge 0 \quad (\text{gains}) \\
-\lambda (-x)^\alpha & \text{for } x < 0 \quad (\text{losses})
\end{cases}
$$

- $\alpha = 0.88$: Diminishing sensitivity parameter (concave for gains, convex for losses).
- $\lambda = 2.25$: Loss aversion coefficient (losses hurt $2.25\times$ more than equal-sized gains).

#### Probability Weighting Function $w(p)$
$$
w^+(p) = \frac{p^{\gamma^+}}{\left(p^{\gamma^+} + (1-p)^{\gamma^+}\right)^{1/\gamma^+}}, \quad
w^-(p) = \frac{p^{\gamma^-}}{\left(p^{\gamma^-} + (1-p)^{\gamma^-}\right)^{1/\gamma^-}}
$$

- $\gamma^+ = 0.61$: Overweighting of extreme tail probabilities for gains.
- $\gamma^- = 0.69$: Overweighting of extreme tail probabilities for losses.

#### Decision Weights $\pi_i$
For ranked gains $x_1 \ge x_2 \ge \dots \ge x_k \ge 0$:
$$
\pi_i^+ = w^+\left(\frac{i}{n}\right) - w^+\left(\frac{i-1}{n}\right)
$$

For ranked losses $x_{-m} \le x_{-m+1} \le \dots \le x_{-1} < 0$:
$$
\pi_j^- = w^-\left(\frac{j}{n}\right) - w^-\left(\frac{j-1}{n}\right)
$$

The overall prospect value $V(f)$ of a rolling return window is:
$$
V(f) = \sum_{i=1}^k \pi_i^+ v(x_i) + \sum_{j=1}^m \pi_j^- v(x_j)
$$

---

## 2. Feature Engineering Pipeline (`src/features.py`)

The dataset spans 16 major cryptocurrency assets (`BNB`, `Bitcoin`, `BitcoinCash`, `Cardano`, `Dash`, `Decentraland`, `Dogecoin`, `Ethereum`, `Litecoin`, `Monero`, `Stellar`, `TRON`, `Tether`, `USDC`, `XRP`, `Zcash`).

### 2.1 Feature Definitions

| Feature Name | Calculation Window | Mathematical Description | Economic Rationale |
|---|---|---|---|
| `pt_value` | Rolling 60-day | Cumulative Prospect Theory expected value | Quantifies retail psychological valuation of recent return profile |
| `roll_skew` | Rolling 60-day | Fisher-Pearson standardized 3rd moment | Positive skewness preference (lottery-like upside payoff) |
| `roll_max` | Rolling 60-day | $\max(R_{t-59}, \dots, R_t)$ | Extreme payoff salience / recency bias |
| `roll_vol` | Rolling 60-day | Sample standard deviation | Asset risk baseline |
| `turnover` | Daily | $\frac{\text{Volume}_t}{\text{MarketCap}_t}$ | Retail trading intensity and speculative attention |

### 2.2 Composite Target Formulation (`y_lottery_seeking`)

To isolate assets exhibiting extreme speculative demand independent of market-wide regime shifts:
1. Standardize cross-sectionally per timestamp:
   $$z_{\text{PT}} = \frac{\text{pt\_value} - \mu_t}{\sigma_t}, \quad z_{\text{skew}} = \frac{\text{roll\_skew} - \mu_t}{\sigma_t}, \quad z_{\text{max}} = \frac{\text{roll\_max} - \mu_t}{\sigma_t}$$
2. Compute composite lottery score:
   $$\text{Score}_{\text{composite}} = z_{\text{PT}} + z_{\text{skew}} + z_{\text{max}}$$
3. Assign binary classification label:
   $$y_t = \begin{cases} 1 & \text{if } \text{Score}_{\text{composite}} \ge Q_{75}(\text{Score}_{\text{composite}}) \\ 0 & \text{otherwise} \end{cases}$$

### 2.3 Strict Lookahead Bias Elimination

All features ($X_{t}$) are shifted forward by 1 period ($t-1 \to t$). Features at market close $t-1$ strictly predict speculative classification at time $t$.

---

## 3. Walk-Forward Chronological Evaluation (`src/train.py`)

Random cross-validation produces optimistic data leakage in non-stationary time series. The evaluation strictly enforces:
- **80/20 Dynamic Chronological Split**: Historical training and cross-validation on the oldest 80% of timestamps; evaluation strictly on the newest 20% out-of-sample test set.
- **Walk-Forward Validation**: 5-fold expanding-window `TimeSeriesSplit`.
- **Class Imbalance Handling**: Dynamic `scale_pos_weight = 3.0` for XGBoost and `class_weight="balanced"` for Random Forest and Logistic Regression.

### 3.1 Benchmark Leaderboard

| Model | Walk-Forward CV AUC | Out-of-Sample Test AUC | Test F1-Score | Precision | Recall | Accuracy | Training Time |
|---|---|---|---|---|---|---|---|
| **Random Forest** | 0.8760 | **0.9215** | **0.7074** | 0.6258 | 0.8135 | 83.17% | 49.5 s |
| **AdaBoost** | 0.8805 | **0.9189** | 0.6628 | 0.8450 | 0.5452 | 86.13% | 18.4 s |
| **XGBoost** | 0.8760 | **0.9182** | 0.7045 | 0.6074 | 0.8385 | 82.41% | 8.0 s |
| **Logistic Regression** | 0.8933 | **0.9144** | 0.7262 | 0.6304 | 0.8563 | 83.86% | 2.8 s |

---

## 4. Model Explainability & SHAP Analysis (`src/explain_shap.py`)

Using Lundberg & Lee (2017) Shapley Additive exPlanations (`shap.TreeExplainer`):
- **Primary Driver**: `pt_value` (Cumulative Prospect Theory valuation) exhibits the highest mean absolute SHAP value across out-of-sample observations.
- **Non-Linear Dynamics**: High `pt_value` values combined with elevated `roll_max` exponentially increase the probability of an asset entering a speculative retail frenzy window.
- **Visual Artifacts**:
  - `plots/shap_summary_plot.png`: Beeswarm feature impact distribution.
  - `plots/shap_dependence_pt.png`: Feature interaction between `pt_value` and rolling volatility.
  - `plots/plot_roc_curve.png`: Out-of-sample ROC curves.
  - `plots/plot_pr_curve.png`: Precision-Recall curves.
  - `plots/plot_time_series_overlay.png`: Bitcoin price overlay highlighting predicted speculative windows.

---

## 5. High-Performance C++20 Convergence Engine

To transition the validated empirical findings into a production trading environment:

1. **C++20 SIMD Prospect Theory Engine** (`compute/simd/prospect_theory.hpp`):
   - Implements identical Tversky & Kahneman (1992) value and probability weighting functions.
   - Computes rolling 60-day windows in native C++ with SIMD acceleration, reducing computation time from minutes to sub-50 milliseconds.
2. **Multi-Asset Lakehouse Universe Ingestion** (`storage/dataset/universe_ingestor.hpp`):
   - Parses the 16-asset historical crypto universe and writes 64-byte aligned columnar `.obsd` files partitioned by `symbol=<SYM>/year=<YYYY>/data-001.obsd`.
   - Generates min/max price and timestamp statistics for fast partition pruning.
3. **Native Streaming ML Inference Stage** (`runtime/pipeline/ml_inference_stage.hpp`):
   - Evaluates a compiled Decision Tree and Random Forest ensemble directly inside the low-latency streaming pipeline (`IStage`).
   - Zero Python runtime dependencies in production.
   - Emits `SignalGenerated` events onto the `EventBus` when speculative regime probabilities breach threshold ($\ge 0.70$).

