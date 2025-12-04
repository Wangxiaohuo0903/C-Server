#!/usr/bin/env python3
"""
Phase 2 Confidence-Accept Rate Correlation Analysis
Analyzes the relationship between token-level confidence and draft acceptance rate
"""

import numpy as np
from scipy import stats
import matplotlib.pyplot as plt

# Data extracted from PHASE2_CONFIDENCE_ENABLED_RESULTS.log
data = {
    'test_names': [
        'CODE_GENERATION',
        'QA_CONVERSATION',
        'CREATIVE_WRITING',
        'JSON_GENERATION',
        'MATH_REASONING',
        'TRANSLATION (misclassified)'
    ],
    'confidence': np.array([0.974, 0.811, 0.800, 0.869, 0.789, 0.841]),
    'accept_rate': np.array([100.0, 37.3, 25.0, 81.0, 50.0, 69.4]),
    'adjustments': np.array([1, 14, 5, 6, 5, 6])
}

def calculate_correlation():
    """Calculate Pearson correlation coefficient"""
    confidence = data['confidence']
    accept_rate = data['accept_rate'] / 100.0  # Normalize to [0, 1]

    # Pearson correlation
    r, p_value = stats.pearsonr(confidence, accept_rate)

    print("=" * 60)
    print("📊 Confidence vs Accept Rate Correlation Analysis")
    print("=" * 60)
    print()

    print("Raw Data:")
    print("-" * 60)
    for i, name in enumerate(data['test_names']):
        print(f"{name:30s} | Conf: {confidence[i]:.3f} | Accept: {accept_rate[i]*100:5.1f}%")
    print()

    print("Statistical Analysis:")
    print("-" * 60)
    print(f"Pearson Correlation (r):  {r:.4f}")
    print(f"P-value:                   {p_value:.6f}")
    print(f"R-squared (r²):            {r**2:.4f}")
    print()

    # Interpretation
    print("Interpretation:")
    print("-" * 60)
    if r > 0.7:
        strength = "Strong positive"
    elif r > 0.4:
        strength = "Moderate positive"
    elif r > 0.2:
        strength = "Weak positive"
    else:
        strength = "Very weak"

    print(f"Correlation strength: {strength}")

    if p_value < 0.05:
        print(f"✅ Statistically significant (p < 0.05)")
    else:
        print(f"⚠️  Not statistically significant (p >= 0.05)")

    print()
    print(f"About {r**2*100:.1f}% of accept rate variance is explained by confidence.")
    print()

    return r, p_value

def analyze_outliers():
    """Identify potential outliers"""
    confidence = data['confidence']
    accept_rate = data['accept_rate'] / 100.0

    # Linear regression to find residuals
    slope, intercept = np.polyfit(confidence, accept_rate, 1)
    predicted = slope * confidence + intercept
    residuals = accept_rate - predicted

    print("Outlier Analysis:")
    print("-" * 60)

    threshold = 1.5 * np.std(residuals)
    for i, name in enumerate(data['test_names']):
        if abs(residuals[i]) > threshold:
            direction = "above" if residuals[i] > 0 else "below"
            print(f"⚠️  {name:30s} | Residual: {residuals[i]:+.3f} ({direction} trend)")

    print()

def recommendation():
    """Provide recommendations based on analysis"""
    print("Recommendations for Thesis:")
    print("=" * 60)
    print()
    print("1. **Data Collection**: Current sample size (n=6) is small.")
    print("   - Recommendation: Run 5-10 additional tests per task type")
    print("   - Target: n ≥ 30 for robust statistical analysis")
    print()
    print("2. **Confidence Threshold Tuning**:")
    print("   - High confidence (≥0.85): Shows best accept rates (81-100%)")
    print("   - Consider raising aggressive threshold to 0.87")
    print()
    print("3. **Investigation Needed**:")
    print("   - Test 1 (CODE_GENERATION): 100% accept rate seems anomalous")
    print("   - Verify if this is due to small sample (24 tokens)")
    print()
    print("4. **Thesis Experiment Design**:")
    print("   - 5 task types × 10 trials = 50 data points")
    print("   - Control for: prompt length, temperature, model size")
    print("   - Measure: confidence, accept rate, speedup, n_draft adjustments")
    print()

if __name__ == "__main__":
    r, p_value = calculate_correlation()
    analyze_outliers()
    recommendation()

    # Save results
    with open('../PHASE2_CORRELATION_ANALYSIS.txt', 'w', encoding='utf-8') as f:
        f.write(f"Pearson Correlation: r = {r:.4f}\n")
        f.write(f"P-value: {p_value:.6f}\n")
        f.write(f"R-squared: {r**2:.4f}\n")
        f.write(f"\nSample size: n = 6\n")
        f.write(f"Recommendation: Collect more data (target n ≥ 30)\n")

    print()
    print(f"✅ Results saved to: PHASE2_CORRELATION_ANALYSIS.txt")
