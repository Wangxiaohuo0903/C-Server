#!/usr/bin/env python3
"""
前缀缓存性能测试脚本

测试场景：
1. 相同前缀（80%请求共享相同system prompt）
2. 变化前缀（每次不同）
3. 多模板轮换（5种常见模板）

指标：
- 首token延迟（TTFT: Time To First Token）
- 总响应时间
- 吞吐量
- 缓存命中率
"""

import requests
import time
import json
import statistics
from typing import List, Dict

BASE_URL = "http://localhost:8080"

# 测试用户
TEST_USER = "bench_user"
TEST_PASSWORD = "test123"

# 共享前缀（模拟常见的system prompt场景）
SHARED_PREFIX = "你是一个helpful AI assistant，擅长回答编程问题。"

# 5种不同的system prompt模板
TEMPLATES = [
    "你是一个Python专家，请简洁回答。",
    "你是一个数据科学顾问，请给出实用建议。",
    "你是一个系统架构师，请从架构角度分析。",
    "你是一个算法工程师，请优先考虑性能。",
    "你是一个代码审查专家，请指出潜在问题。",
]

# 测试问题（简短以加快测试速度）
QUESTIONS = [
    "什么是递归？",
    "如何优化循环？",
    "解释一下闭包",
    "什么是装饰器？",
    "列表和元组区别？",
    "什么是GIL？",
    "如何处理异常？",
    "什么是生成器？",
]

class BenchmarkClient:
    def __init__(self):
        self.session = requests.Session()
        self.chat_ids = []

    def register_and_login(self):
        """注册并登录"""
        # 尝试注册（可能失败因为用户已存在）
        try:
            self.session.post(f"{BASE_URL}/register", data={
                "username": TEST_USER,
                "password": TEST_PASSWORD
            })
        except:
            pass

        # 登录
        resp = self.session.post(f"{BASE_URL}/login", data={
            "username": TEST_USER,
            "password": TEST_PASSWORD
        })
        assert resp.status_code == 200, "Login failed"
        print(f"✓ Logged in as {TEST_USER}")

    def create_chat(self) -> int:
        """创建新聊天"""
        resp = self.session.post(f"{BASE_URL}/chat", data={"user": TEST_USER})
        chat_id = int(resp.text)
        self.chat_ids.append(chat_id)
        return chat_id

    def send_message(self, chat_id: int, message: str) -> Dict:
        """发送消息并返回性能数据"""
        start = time.time()

        resp = self.session.post(f"{BASE_URL}/infer",
            json={
                "chat_id": chat_id,
                "user": TEST_USER,
                "prompt": message
            },
            headers={"Content-Type": "application/json"}
        )

        end = time.time()
        latency = (end - start) * 1000  # ms

        assert resp.status_code == 200, f"Request failed: {resp.text}"
        answer = resp.json().get("answer", "")

        return {
            "latency_ms": latency,
            "answer_len": len(answer),
            "timestamp": start
        }

    def get_cache_stats(self) -> Dict:
        """获取缓存统计"""
        resp = self.session.get(f"{BASE_URL}/api/cache_stats")
        return resp.json()

    def clear_cache(self):
        """清空缓存"""
        self.session.post(f"{BASE_URL}/api/clear_cache")
        print("✓ Cache cleared")

def run_scenario_shared_prefix(client: BenchmarkClient, num_requests: int = 20) -> List[Dict]:
    """
    场景1：80%请求共享相同前缀
    模拟大量用户使用同一个system prompt的情况
    """
    print(f"\n=== 场景1：共享前缀测试 ({num_requests} requests) ===")

    client.clear_cache()
    results = []

    # 创建一个chat用于所有测试
    chat_id = client.create_chat()

    # 发送带有共享前缀的多轮对话
    for i in range(num_requests):
        # 第一轮：设置前缀
        question = QUESTIONS[i % len(QUESTIONS)]
        if i == 0:
            # 首次包含system prompt
            message = f"{SHARED_PREFIX}\n\n{question}"
        else:
            # 后续只有用户问题（前缀已缓存）
            message = question

        result = client.send_message(chat_id, message)
        result["request_id"] = i
        result["is_first"] = (i == 0)
        results.append(result)

        if (i + 1) % 5 == 0:
            stats = client.get_cache_stats()
            print(f"  Progress: {i+1}/{num_requests} | "
                  f"Avg latency: {statistics.mean([r['latency_ms'] for r in results[-5:]]):.1f}ms | "
                  f"Cache hit rate: {stats['hit_rate']:.1f}%")

    # 最终统计
    stats = client.get_cache_stats()
    print(f"\n  Final cache stats:")
    print(f"    Cache size: {stats['cache_size']}")
    print(f"    Total hits: {stats['total_hits']}")
    print(f"    Hit rate: {stats['hit_rate']:.2f}%")

    return results

def run_scenario_no_cache(client: BenchmarkClient, num_requests: int = 10) -> List[Dict]:
    """
    场景2：无缓存（每次都是新对话）
    作为baseline对比
    """
    print(f"\n=== 场景2：无缓存基线测试 ({num_requests} requests) ===")

    client.clear_cache()
    results = []

    for i in range(num_requests):
        # 每次创建新chat（避免历史累积）
        chat_id = client.create_chat()

        question = QUESTIONS[i % len(QUESTIONS)]
        message = f"{SHARED_PREFIX}\n\n{question}"

        result = client.send_message(chat_id, message)
        result["request_id"] = i
        results.append(result)

        if (i + 1) % 5 == 0:
            print(f"  Progress: {i+1}/{num_requests} | "
                  f"Avg latency: {statistics.mean([r['latency_ms'] for r in results[-5:]]):.1f}ms")

    return results

def run_scenario_multi_template(client: BenchmarkClient, num_requests: int = 25) -> List[Dict]:
    """
    场景3：多模板轮换
    5种不同模板轮流使用，测试多缓存条目场景
    """
    print(f"\n=== 场景3：多模板轮换测试 ({num_requests} requests, {len(TEMPLATES)} templates) ===")

    client.clear_cache()
    results = []

    # 为每个模板创建一个chat
    template_chats = [client.create_chat() for _ in TEMPLATES]

    for i in range(num_requests):
        template_idx = i % len(TEMPLATES)
        chat_id = template_chats[template_idx]
        template = TEMPLATES[template_idx]
        question = QUESTIONS[i % len(QUESTIONS)]

        # 轮换中同一模板的第一次包含完整前缀
        if i < len(TEMPLATES):
            message = f"{template}\n\n{question}"
        else:
            message = question

        result = client.send_message(chat_id, message)
        result["request_id"] = i
        result["template_id"] = template_idx
        results.append(result)

        if (i + 1) % 5 == 0:
            stats = client.get_cache_stats()
            print(f"  Progress: {i+1}/{num_requests} | "
                  f"Avg latency: {statistics.mean([r['latency_ms'] for r in results[-5:]]):.1f}ms | "
                  f"Cache size: {stats['cache_size']}")

    # 最终统计
    stats = client.get_cache_stats()
    print(f"\n  Final cache stats:")
    print(f"    Cache size: {stats['cache_size']}")
    print(f"    Hit rate: {stats['hit_rate']:.2f}%")

    return results

def print_summary(scenario_name: str, results: List[Dict]):
    """打印结果摘要"""
    latencies = [r['latency_ms'] for r in results]

    # 分离第一次和后续请求（第一次无缓存）
    first_requests = [r for r in results if r.get('is_first', False)]
    subsequent_requests = [r for r in results if not r.get('is_first', False)]

    print(f"\n{'='*60}")
    print(f"  {scenario_name} - 结果摘要")
    print(f"{'='*60}")
    print(f"  总请求数: {len(results)}")
    print(f"  平均延迟: {statistics.mean(latencies):.2f} ms")
    print(f"  中位延迟 (P50): {statistics.median(latencies):.2f} ms")
    if len(latencies) > 1:
        print(f"  P95延迟: {sorted(latencies)[int(len(latencies)*0.95)]:.2f} ms")
        print(f"  P99延迟: {sorted(latencies)[int(len(latencies)*0.99)]:.2f} ms")
    print(f"  最小延迟: {min(latencies):.2f} ms")
    print(f"  最大延迟: {max(latencies):.2f} ms")

    if subsequent_requests:
        sub_latencies = [r['latency_ms'] for r in subsequent_requests]
        print(f"\n  后续请求 (缓存生效):")
        print(f"    平均延迟: {statistics.mean(sub_latencies):.2f} ms")
        print(f"    中位延迟: {statistics.median(sub_latencies):.2f} ms")

        if first_requests:
            first_latency = first_requests[0]['latency_ms']
            improvement = (first_latency - statistics.mean(sub_latencies)) / first_latency * 100
            print(f"    性能提升: {improvement:.1f}%")

    # 计算吞吐量
    if len(results) > 1:
        duration = results[-1]['timestamp'] - results[0]['timestamp']
        throughput = len(results) / duration
        print(f"\n  吞吐量: {throughput:.2f} req/s")

def main():
    print("🚀 前缀缓存性能测试")
    print("="*60)

    client = BenchmarkClient()
    client.register_and_login()

    # 运行三个场景
    results_shared = run_scenario_shared_prefix(client, num_requests=20)
    time.sleep(2)  # 间隔

    results_no_cache = run_scenario_no_cache(client, num_requests=10)
    time.sleep(2)

    results_multi = run_scenario_multi_template(client, num_requests=25)

    # 打印汇总
    print("\n" + "="*60)
    print("  🎯 性能对比总结")
    print("="*60)

    print_summary("场景1: 共享前缀", results_shared)
    print_summary("场景2: 无缓存基线", results_no_cache)
    print_summary("场景3: 多模板轮换", results_multi)

    # 计算加速比
    print("\n" + "="*60)
    print("  📊 加速比分析")
    print("="*60)

    baseline_avg = statistics.mean([r['latency_ms'] for r in results_no_cache])
    shared_avg = statistics.mean([r['latency_ms'] for r in results_shared[1:]])  # 跳过第一次
    multi_avg = statistics.mean([r['latency_ms'] for r in results_multi[len(TEMPLATES):]])  # 跳过预热

    print(f"  无缓存基线: {baseline_avg:.2f} ms")
    print(f"  共享前缀 (有缓存): {shared_avg:.2f} ms")
    print(f"    加速比: {baseline_avg/shared_avg:.2f}x")
    print(f"    延迟降低: {(baseline_avg-shared_avg)/baseline_avg*100:.1f}%")
    print(f"\n  多模板 (有缓存): {multi_avg:.2f} ms")
    print(f"    加速比: {baseline_avg/multi_avg:.2f}x")
    print(f"    延迟降低: {(baseline_avg-multi_avg)/baseline_avg*100:.1f}%")

    print("\n✅ 测试完成！")

if __name__ == "__main__":
    main()
