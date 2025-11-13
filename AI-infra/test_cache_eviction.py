#!/usr/bin/env python3
"""
测试缓存淘汰和序列ID回收功能

目标：
1. 填满缓存（32个条目）
2. 继续添加新缓存触发淘汰
3. 验证序列清理和ID回收
"""

import requests
import time

BASE_URL = "http://localhost:8081/infer"

def send_request(chat_id, prompt, max_tokens=20):
    """发送推理请求"""
    data = {
        "chat_id": chat_id,
        "prompt": prompt,
        "max_tokens": max_tokens
    }
    try:
        response = requests.post(BASE_URL, json=data, timeout=30)
        return response.json()
    except Exception as e:
        print(f"Request failed: {e}")
        return None

def test_cache_eviction():
    """测试缓存淘汰功能"""
    print("=" * 60)
    print("测试缓存淘汰和序列ID回收")
    print("=" * 60)

    # 步骤1: 创建40个不同的对话，每个对话2轮
    # 这将创建40个缓存条目，触发淘汰
    print("\n[步骤1] 创建40个对话，触发缓存淘汰...")
    for i in range(40):
        chat_id = f"test-eviction-{i}"

        # 第一轮：创建对话
        prompt1 = f"Hello, this is conversation {i}"
        response1 = send_request(chat_id, prompt1)
        if response1:
            print(f"  [{i+1}/40] Chat {chat_id}: First message sent")

        # 第二轮：触发缓存保存
        prompt2 = f"Tell me about topic {i}"
        response2 = send_request(chat_id, prompt2)
        if response2:
            print(f"  [{i+1}/40] Chat {chat_id}: Second message sent (cache saved)")

        # 每10个请求休息一下
        if (i + 1) % 10 == 0:
            print(f"\n  已完成 {i+1}/40 个对话\n")
            time.sleep(2)

    print("\n[完成] 已发送40个对话的请求")
    print("请检查Docker日志查看缓存淘汰和序列回收消息")
    print("\n运行以下命令查看日志:")
    print("  docker logs ai-chats-server 2>&1 | grep -E '(Evicted|Cleared|recycled)'")

if __name__ == "__main__":
    test_cache_eviction()
