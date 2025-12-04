#!/usr/bin/env python3
"""
计算Phase 2置信度-接受率的精确Pearson相关系数和p-value
"""

import math

# 已知的6个数据点 (来自PHASE2_DATA_SUMMARY.md)
data_points = [
    {"task": "CODE_GENERATION", "confidence": 0.974, "accept_rate": 100.0},
    {"task": "JSON_GENERATION", "confidence": 0.869, "accept_rate": 81.0},
    {"task": "TRANSLATION", "confidence": 0.841, "accept_rate": 69.4},
    {"task": "QA_CONVERSATION", "confidence": 0.811, "accept_rate": 37.3},
    {"task": "CREATIVE_WRITING", "confidence": 0.800, "accept_rate": 25.0},
    {"task": "MATH_REASONING", "confidence": 0.789, "accept_rate": 50.0},
]

def calculate_pearson(x, y):
    """计算Pearson相关系数"""
    n = len(x)

    # 计算均值
    mean_x = sum(x) / n
    mean_y = sum(y) / n

    # 计算协方差和标准差
    cov_xy = sum((x[i] - mean_x) * (y[i] - mean_y) for i in range(n))
    var_x = sum((xi - mean_x) ** 2 for xi in x)
    var_y = sum((yi - mean_y) ** 2 for yi in y)

    # Pearson r
    r = cov_xy / math.sqrt(var_x * var_y)

    return r

def calculate_t_statistic(r, n):
    """计算t统计量"""
    if abs(r) >= 0.9999:
        return float('inf')

    t = r * math.sqrt(n - 2) / math.sqrt(1 - r**2)
    return t

def estimate_p_value(t, df):
    """
    估算双尾p-value (基于t分布)
    使用简化的查表法
    """
    t = abs(t)

    # 自由度df=4 (n-2=6-2=4)的t分布临界值
    critical_values = {
        0.10: 2.132,   # p=0.10 (双尾)
        0.05: 2.776,   # p=0.05 (双尾)
        0.02: 3.747,   # p=0.02 (双尾)
        0.01: 4.604,   # p=0.01 (双尾)
        0.001: 8.610,  # p=0.001 (双尾)
    }

    # 根据t值估算p-value
    if t < critical_values[0.10]:
        return "> 0.10 (不显著)"
    elif t < critical_values[0.05]:
        return "0.05 < p < 0.10 (边界)"
    elif t < critical_values[0.02]:
        return "0.02 < p < 0.05 (显著 *)"
    elif t < critical_values[0.01]:
        return "0.01 < p < 0.02 (显著 **)"
    elif t < critical_values[0.001]:
        return "0.001 < p < 0.01 (非常显著 ***)"
    else:
        return "p < 0.001 (极显著 ***)"

def main():
    print("=" * 80)
    print("  Phase 2 Token置信度引导 - Pearson相关性分析")
    print("=" * 80)
    print()

    # 提取数据
    confidences = [dp["confidence"] for dp in data_points]
    accept_rates = [dp["accept_rate"] for dp in data_points]
    n = len(data_points)

    # 打印原始数据
    print("[DATA] Original Data (n=6)")
    print("-" * 80)
    print(f"{'Task Type':<20} {'Confidence':<15} {'Accept Rate(%)':<15}")
    print("-" * 80)
    for dp in data_points:
        print(f"{dp['task']:<20} {dp['confidence']:<15.3f} {dp['accept_rate']:<15.1f}")
    print()

    # 计算Pearson相关系数
    r = calculate_pearson(confidences, accept_rates)
    r_squared = r ** 2

    # 计算t统计量
    t = calculate_t_statistic(r, n)

    # 估算p-value (df = n-2 = 4)
    df = n - 2
    p_value_range = estimate_p_value(t, df)

    # 输出结果
    print("[RESULT] Correlation Analysis")
    print("-" * 80)
    print(f"Pearson r:               {r:.4f}")
    print(f"R-squared:               {r_squared:.4f}  ({r_squared*100:.1f}% variance explained)")
    print(f"t-statistic:             {t:.4f}")
    print(f"Degrees of freedom:      {df}")
    print(f"p-value (two-tailed):    {p_value_range}")
    print()

    # 解释
    print("[INTERPRETATION] Statistical Significance")
    print("-" * 80)

    if r > 0.8:
        strength = "非常强的正相关"
    elif r > 0.6:
        strength = "强正相关"
    elif r > 0.4:
        strength = "中等正相关"
    elif r > 0.2:
        strength = "弱正相关"
    else:
        strength = "几乎无相关"

    print(f"相关性强度: {strength} (r = {r:.4f})")
    print()

    print("[SIGNIFICANCE] Assessment:")
    # 检查是否显著 (p < 0.05)
    is_significant = (
        "p < 0.05" in p_value_range or
        "p < 0.02" in p_value_range or
        "p < 0.01" in p_value_range or
        "p < 0.001" in p_value_range or
        "0.01 < p < 0.02" in p_value_range or
        "0.02 < p < 0.05" in p_value_range
    )

    if is_significant:
        print("   [OK] Statistically significant (p < 0.05)")
        print("   -> Can confidently claim significant positive correlation in paper")
    else:
        print("   [WARNING] Not statistically significant (p >= 0.05)")
        print("   -> Current sample size (n=6) is too small, need n>=30")
        print("   -> But the trend is clear, can be used as preliminary evidence")

    print()
    print("[WRITING] Suggestions for Paper:")
    print("-" * 80)
    if is_significant:
        print(f"   \"Experimental results show {strength} between")
        print(f"    Token confidence and accept rate (Pearson r = {r:.3f}, p < 0.05, n = {n}),")
        print(f"    validating the confidence-guided strategy. R-squared = {r_squared:.3f} indicates")
        print(f"    approximately {r_squared*100:.0f}% of accept rate variance can be explained")
        print(f"    by confidence.\"")
    else:
        print(f"   \"Preliminary experiments (n={n}) show {strength} trend between")
        print(f"    Token confidence and accept rate (Pearson r = {r:.3f}), but statistical")
        print(f"    significance (p >= 0.05) was not reached due to limited sample size.")
        print(f"    Future work will expand to n>=30 to validate this hypothesis.\"")

    print()
    print("[NEXT STEPS] Action Plan:")
    print("-" * 80)
    print(f"   Current: n = {n} (insufficient statistical power)")
    print("   Target: n >= 30 (Cohen's guidelines for correlation studies)")
    print("   Method: Run each task type 5 times -> 6x5 = 30 data points")
    print()

    # 保存结果
    output_file = "C:/Users/实习生/Documents/Code/server/C-Server/AI-infra/PHASE2_PEARSON_ANALYSIS.txt"
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("=" * 80 + "\n")
        f.write("  Phase 2 Token Confidence Guidance - Pearson Correlation Analysis\n")
        f.write("=" * 80 + "\n\n")
        f.write("Core Results:\n")
        f.write(f"  Pearson r = {r:.4f}\n")
        f.write(f"  R-squared = {r_squared:.4f}\n")
        f.write(f"  t({df}) = {t:.4f}\n")
        f.write(f"  p-value: {p_value_range}\n")
        f.write(f"  Correlation strength: {strength}\n\n")
        f.write("Raw Data:\n")
        for dp in data_points:
            f.write(f"  {dp['task']}: conf={dp['confidence']:.3f}, accept={dp['accept_rate']:.1f}%\n")

    print(f"[SAVED] Results saved to: PHASE2_PEARSON_ANALYSIS.txt")
    print()
    print("=" * 80)

if __name__ == "__main__":
    main()
