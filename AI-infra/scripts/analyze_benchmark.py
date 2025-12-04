#!/usr/bin/env python3
"""
基准测试结果分析脚本
分析 benchmark_comparison 生成的 JSON 结果，生成统计报告和可视化图表

用法:
    python analyze_benchmark.py benchmark_results.json
"""

import json
import sys
from pathlib import Path
from typing import Dict, List
from collections import defaultdict
import statistics

# 尝试导入可视化库（可选）
try:
    import matplotlib.pyplot as plt
    import matplotlib
    matplotlib.use('Agg')  # 无头模式，不需要显示窗口
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("⚠️  matplotlib not installed. Charts will not be generated.")
    print("   Install with: pip install matplotlib")


def load_results(json_file: str) -> List[Dict]:
    """加载 JSON 测试结果"""
    with open(json_file, 'r', encoding='utf-8') as f:
        return json.load(f)


def analyze_by_scenario(results: List[Dict]) -> Dict:
    """按场景分组统计"""
    by_scenario = defaultdict(list)

    for r in results:
        by_scenario[r['scenario']].append(r)

    stats = {}
    for scenario, items in by_scenario.items():
        accept_rates = [r['speculative']['accept_rate'] for r in items]
        speedups = [r['speculative']['speedup'] for r in items]
        normal_tps = [r['normal']['tokens_per_sec'] for r in items]
        spec_tps = [r['speculative']['tokens_per_sec'] for r in items]

        stats[scenario] = {
            'count': len(items),
            'avg_accept_rate': statistics.mean(accept_rates),
            'std_accept_rate': statistics.stdev(accept_rates) if len(accept_rates) > 1 else 0,
            'avg_speedup': statistics.mean(speedups),
            'std_speedup': statistics.stdev(speedups) if len(speedups) > 1 else 0,
            'avg_normal_tps': statistics.mean(normal_tps),
            'avg_spec_tps': statistics.mean(spec_tps),
            'min_speedup': min(speedups),
            'max_speedup': max(speedups),
        }

    return stats


def calculate_global_stats(results: List[Dict]) -> Dict:
    """计算全局统计"""
    accept_rates = [r['speculative']['accept_rate'] for r in results]
    speedups = [r['speculative']['speedup'] for r in results]

    total_normal_time = sum(r['normal']['time_ms'] for r in results)
    total_spec_time = sum(r['speculative']['time_ms'] for r in results)
    total_tokens = sum(r['speculative']['tokens'] for r in results)
    total_drafted = sum(r['speculative']['drafted'] for r in results)
    total_accepted = sum(r['speculative']['accepted'] for r in results)

    return {
        'total_tests': len(results),
        'avg_accept_rate': statistics.mean(accept_rates),
        'std_accept_rate': statistics.stdev(accept_rates) if len(accept_rates) > 1 else 0,
        'avg_speedup': statistics.mean(speedups),
        'std_speedup': statistics.stdev(speedups) if len(speedups) > 1 else 0,
        'min_speedup': min(speedups),
        'max_speedup': max(speedups),
        'total_normal_time_ms': total_normal_time,
        'total_spec_time_ms': total_spec_time,
        'total_speedup': total_normal_time / total_spec_time if total_spec_time > 0 else 0,
        'total_tokens': total_tokens,
        'total_drafted': total_drafted,
        'total_accepted': total_accepted,
        'global_accept_rate': total_accepted / total_drafted if total_drafted > 0 else 0,
    }


def generate_charts(results: List[Dict], scenario_stats: Dict, output_path: str):
    """生成性能对比图表"""
    if not HAS_MATPLOTLIB:
        return

    # 创建 2x2 子图布局
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle('Speculative Decoding Performance Analysis', fontsize=16, fontweight='bold')

    # 1. 各场景平均接受率
    ax1 = axes[0, 0]
    scenarios = list(scenario_stats.keys())
    accept_rates = [scenario_stats[s]['avg_accept_rate'] * 100 for s in scenarios]
    accept_stds = [scenario_stats[s]['std_accept_rate'] * 100 for s in scenarios]

    bars1 = ax1.bar(scenarios, accept_rates, yerr=accept_stds, capsize=5, color='skyblue', alpha=0.8)
    ax1.set_ylabel('Accept Rate (%)', fontweight='bold')
    ax1.set_title('Average Accept Rate by Scenario')
    ax1.set_ylim(0, 100)
    ax1.grid(axis='y', alpha=0.3)

    # 在柱状图上标注数值
    for bar, rate in zip(bars1, accept_rates):
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height,
                f'{rate:.1f}%', ha='center', va='bottom', fontsize=9)

    # 2. 各场景平均加速比
    ax2 = axes[0, 1]
    speedups = [scenario_stats[s]['avg_speedup'] for s in scenarios]
    speedup_stds = [scenario_stats[s]['std_speedup'] for s in scenarios]

    bars2 = ax2.bar(scenarios, speedups, yerr=speedup_stds, capsize=5, color='lightcoral', alpha=0.8)
    ax2.set_ylabel('Speedup (x)', fontweight='bold')
    ax2.set_title('Average Speedup by Scenario')
    ax2.axhline(y=1.0, color='red', linestyle='--', alpha=0.5, label='Baseline (1x)')
    ax2.grid(axis='y', alpha=0.3)
    ax2.legend()

    for bar, speedup in zip(bars2, speedups):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                f'{speedup:.2f}x', ha='center', va='bottom', fontsize=9)

    # 3. 吞吐量对比 (Normal vs Speculative)
    ax3 = axes[1, 0]
    x = range(len(scenarios))
    width = 0.35

    normal_tps = [scenario_stats[s]['avg_normal_tps'] for s in scenarios]
    spec_tps = [scenario_stats[s]['avg_spec_tps'] for s in scenarios]

    bars3a = ax3.bar([i - width/2 for i in x], normal_tps, width, label='Normal', color='orange', alpha=0.8)
    bars3b = ax3.bar([i + width/2 for i in x], spec_tps, width, label='Speculative', color='green', alpha=0.8)

    ax3.set_ylabel('Tokens/sec', fontweight='bold')
    ax3.set_title('Throughput Comparison')
    ax3.set_xticks(x)
    ax3.set_xticklabels(scenarios)
    ax3.legend()
    ax3.grid(axis='y', alpha=0.3)

    # 4. 加速比散点图（所有测试）
    ax4 = axes[1, 1]

    scenario_colors = {
        'code_generation': 'blue',
        'qa_conversation': 'green',
        'creative_writing': 'red',
        'json_generation': 'purple'
    }

    for scenario in scenarios:
        scenario_results = [r for r in results if r['scenario'] == scenario]
        accept_rates_scatter = [r['speculative']['accept_rate'] * 100 for r in scenario_results]
        speedups_scatter = [r['speculative']['speedup'] for r in scenario_results]

        ax4.scatter(accept_rates_scatter, speedups_scatter,
                   label=scenario, color=scenario_colors.get(scenario, 'gray'),
                   alpha=0.6, s=50)

    ax4.set_xlabel('Accept Rate (%)', fontweight='bold')
    ax4.set_ylabel('Speedup (x)', fontweight='bold')
    ax4.set_title('Speedup vs Accept Rate')
    ax4.axhline(y=1.0, color='red', linestyle='--', alpha=0.3)
    ax4.grid(alpha=0.3)
    ax4.legend(fontsize=8)

    # 调整布局并保存
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"📊 Charts saved to: {output_path}")


def generate_markdown_report(results: List[Dict], scenario_stats: Dict,
                             global_stats: Dict, output_path: str):
    """生成 Markdown 格式报告"""

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("# Speculative Decoding Benchmark Report\n\n")

        # 全局统计
        f.write("## Overall Statistics\n\n")
        f.write(f"- **Total Tests**: {global_stats['total_tests']}\n")
        f.write(f"- **Average Accept Rate**: {global_stats['avg_accept_rate']*100:.2f}% ± {global_stats['std_accept_rate']*100:.2f}%\n")
        f.write(f"- **Average Speedup**: {global_stats['avg_speedup']:.2f}x ± {global_stats['std_speedup']:.2f}x\n")
        f.write(f"- **Speedup Range**: {global_stats['min_speedup']:.2f}x - {global_stats['max_speedup']:.2f}x\n")
        f.write(f"- **Total Time Saved**: {(global_stats['total_normal_time_ms'] - global_stats['total_spec_time_ms']):.0f} ms\n")
        f.write(f"- **Overall Speedup**: {global_stats['total_speedup']:.2f}x\n\n")

        # 按场景分组
        f.write("## Performance by Scenario\n\n")

        for scenario, stats in scenario_stats.items():
            f.write(f"### {scenario.replace('_', ' ').title()}\n\n")
            f.write(f"- **Test Count**: {stats['count']}\n")
            f.write(f"- **Accept Rate**: {stats['avg_accept_rate']*100:.2f}% ± {stats['std_accept_rate']*100:.2f}%\n")
            f.write(f"- **Speedup**: {stats['avg_speedup']:.2f}x ± {stats['std_speedup']:.2f}x (range: {stats['min_speedup']:.2f}x - {stats['max_speedup']:.2f}x)\n")
            f.write(f"- **Normal Throughput**: {stats['avg_normal_tps']:.2f} tokens/sec\n")
            f.write(f"- **Speculative Throughput**: {stats['avg_spec_tps']:.2f} tokens/sec\n\n")

        # 详细结果表格
        f.write("## Detailed Results\n\n")

        for scenario in scenario_stats.keys():
            scenario_results = [r for r in results if r['scenario'] == scenario]

            f.write(f"### {scenario.replace('_', ' ').title()}\n\n")
            f.write("| Prompt | Accept Rate | Speedup | Normal (ms) | Spec (ms) |\n")
            f.write("|--------|-------------|---------|-------------|----------|\n")

            for r in scenario_results:
                prompt = r['prompt'][:40] + "..." if len(r['prompt']) > 40 else r['prompt']
                accept_rate = r['speculative']['accept_rate'] * 100
                speedup = r['speculative']['speedup']
                normal_time = r['normal']['time_ms']
                spec_time = r['speculative']['time_ms']

                f.write(f"| {prompt} | {accept_rate:.1f}% | {speedup:.2f}x | {normal_time:.0f} | {spec_time:.0f} |\n")

            f.write("\n")

        # 结论
        f.write("## Conclusions\n\n")

        best_scenario = max(scenario_stats.items(), key=lambda x: x[1]['avg_speedup'])
        worst_scenario = min(scenario_stats.items(), key=lambda x: x[1]['avg_speedup'])

        f.write(f"1. **Best Performance**: {best_scenario[0].replace('_', ' ').title()} achieved {best_scenario[1]['avg_speedup']:.2f}x average speedup\n")
        f.write(f"2. **Worst Performance**: {worst_scenario[0].replace('_', ' ').title()} achieved {worst_scenario[1]['avg_speedup']:.2f}x average speedup\n")
        f.write(f"3. **Accept Rate Correlation**: {'High' if global_stats['avg_accept_rate'] > 0.5 else 'Moderate'} accept rates lead to better speedups\n")
        f.write(f"4. **Overall Assessment**: Speculative decoding achieved {global_stats['avg_speedup']:.2f}x average speedup with {global_stats['avg_accept_rate']*100:.1f}% average accept rate\n\n")

        f.write("---\n")
        f.write("*Generated by analyze_benchmark.py*\n")

    print(f"📄 Markdown report saved to: {output_path}")


def print_terminal_summary(scenario_stats: Dict, global_stats: Dict):
    """在终端打印简要总结"""
    print("\n" + "="*60)
    print("BENCHMARK ANALYSIS SUMMARY".center(60))
    print("="*60 + "\n")

    print(f"📊 Total Tests: {global_stats['total_tests']}")
    print(f"📈 Average Accept Rate: {global_stats['avg_accept_rate']*100:.2f}%")
    print(f"🚀 Average Speedup: {global_stats['avg_speedup']:.2f}x")
    print(f"⏱️  Total Time Saved: {(global_stats['total_normal_time_ms'] - global_stats['total_spec_time_ms'])/1000:.2f}s")
    print()

    print("Performance by Scenario:")
    print("-" * 60)
    print(f"{'Scenario':<25} {'Accept Rate':<15} {'Speedup':<10}")
    print("-" * 60)

    for scenario, stats in sorted(scenario_stats.items()):
        scenario_name = scenario.replace('_', ' ').title()[:24]
        accept = f"{stats['avg_accept_rate']*100:.1f}%"
        speedup = f"{stats['avg_speedup']:.2f}x"
        print(f"{scenario_name:<25} {accept:<15} {speedup:<10}")

    print("="*60 + "\n")


def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_benchmark.py <benchmark_results.json>")
        sys.exit(1)

    json_file = sys.argv[1]

    if not Path(json_file).exists():
        print(f"❌ File not found: {json_file}")
        sys.exit(1)

    print(f"📖 Loading results from: {json_file}\n")

    # 加载数据
    results = load_results(json_file)

    if not results:
        print("❌ No results found in JSON file")
        sys.exit(1)

    print(f"✅ Loaded {len(results)} test results\n")

    # 分析数据
    print("🔍 Analyzing results...")
    scenario_stats = analyze_by_scenario(results)
    global_stats = calculate_global_stats(results)

    # 输出文件路径
    base_path = Path(json_file).with_suffix('')
    chart_path = f"{base_path}_charts.png"
    report_path = f"{base_path}_report.md"

    # 生成图表
    if HAS_MATPLOTLIB:
        print("📊 Generating charts...")
        generate_charts(results, scenario_stats, chart_path)

    # 生成报告
    print("📝 Generating markdown report...")
    generate_markdown_report(results, scenario_stats, global_stats, report_path)

    # 打印终端总结
    print_terminal_summary(scenario_stats, global_stats)

    print("✅ Analysis complete!\n")


if __name__ == '__main__':
    main()
