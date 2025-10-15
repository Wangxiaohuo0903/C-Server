#!/usr/bin/env python3
"""
简化版前缀缓存测试
只测试3轮对话，验证缓存是否生效
"""

import requests
import time

BASE_URL = "http://localhost:8080"

def register_and_login():
    """注册并登录"""
    # 尝试注册（如果已存在会失败，没关系）
    resp = requests.post(f"{BASE_URL}/register",
                        data={"username": "bench_user", "password": "test123"})
    if resp.status_code == 200:
        print("✓ Registered bench_user")

    # 登录
    resp = requests.post(f"{BASE_URL}/login",
                        data={"username": "bench_user", "password": "test123"})
    if resp.status_code == 200:
        print("✓ Logged in as bench_user")
        return True
    else:
        print(f"✗ Login failed: {resp.status_code}")
        return False

def create_chat():
    """创建聊天会话"""
    resp = requests.post(f"{BASE_URL}/chat", data={"user": "bench_user"})
    if resp.status_code == 200:
        chat_id = int(resp.text.strip())
        print(f"✓ Created chat_id: {chat_id}")
        return chat_id
    else:
        print(f"✗ Create chat failed: {resp.status_code}")
        return None

def send_message(chat_id, message):
    """发送消息并返回延迟(毫秒)"""
    start = time.time()
    resp = requests.post(f"{BASE_URL}/infer",
                        json={
                            "chat_id": str(chat_id),
                            "user": "bench_user",
                            "prompt": message
                        },
                        timeout=30)
    latency_ms = (time.time() - start) * 1000

    if resp.status_code == 200:
        result = resp.json()
        answer = result.get("answer", "")
        print(f"  Round latency: {latency_ms:.0f}ms")
        print(f"  Answer preview: {answer[:80]}...")
        return latency_ms
    else:
        print(f"✗ Infer failed: {resp.status_code}")
        return None

def get_cache_stats():
    """获取缓存统计"""
    resp = requests.get(f"{BASE_URL}/api/cache_stats")
    if resp.status_code == 200:
        stats = resp.json()
        print(f"\n📊 Cache stats:")
        print(f"  Cache size: {stats['cache_size']}")
        print(f"  Total hits: {stats['total_hits']}")
        print(f"  Total requests: {stats['total_requests']}")
        print(f"  Hit rate: {stats['hit_rate']:.1f}%")  # 已经是百分比，不需要乘100
        return stats
    return None

def clear_cache():
    """清空缓存"""
    resp = requests.post(f"{BASE_URL}/api/clear_cache")
    if resp.status_code == 200:
        print("✓ Cache cleared")
    return resp.status_code == 200

def main():
    print("🔍 简化版前缀缓存测试")
    print("=" * 60)

    # 注册并登录
    if not register_and_login():
        return

    # 清空缓存
    clear_cache()

    # 创建聊天
    chat_id = create_chat()
    if not chat_id:
        return

    # 第1轮：有system prompt的第一个问题（不会命中缓存）
    print("\n第1轮：初始对话（无缓存）")
    system_prompt = "You are a helpful AI assistant. Please answer concisely."
    msg1 = f"{system_prompt}\n\nWhat is Python?"
    latency1 = send_message(chat_id, msg1)

    # 第2轮：第二个问题（应该保存第一轮作为前缀）
    print("\n第2轮：继续对话（保存前缀到缓存）")
    msg2 = "Tell me about variables"
    latency2 = send_message(chat_id, msg2)

    # 第3轮：第三个问题（应该命中缓存！）
    print("\n第3轮：继续对话（应该命中缓存！）")
    msg3 = "Explain loops"
    latency3 = send_message(chat_id, msg3)

    # 获取缓存统计
    stats = get_cache_stats()

    # 分析结果
    print("\n" + "=" * 60)
    print("📈 结果分析:")
    print(f"  Round 1 latency: {latency1:.0f}ms (baseline, no cache)")
    print(f"  Round 2 latency: {latency2:.0f}ms (saving prefix)")
    print(f"  Round 3 latency: {latency3:.0f}ms (cache hit expected!)")

    if stats and stats['total_requests'] >= 2:
        hit_rate = stats['hit_rate']  # 已经是百分比
        if hit_rate > 0:
            speedup = latency2 / latency3 if latency3 > 0 else 1.0
            print(f"\n✅ 缓存生效！")
            print(f"  Hit rate: {hit_rate:.1f}%")
            print(f"  Speedup (Round 2 vs 3): {speedup:.2f}x")
            print(f"  Latency reduction: {((latency2 - latency3) / latency2 * 100):.1f}%")
        else:
            print(f"\n⚠️  缓存未命中（可能需要≥2轮才启用前缀缓存）")

    print("\n✅ 测试完成！")

if __name__ == "__main__":
    main()
