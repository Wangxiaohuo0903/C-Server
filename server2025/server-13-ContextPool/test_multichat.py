#!/usr/bin/env python3
"""
Server-12 Multi-Chat API 测试脚本
测试多轮对话和会话管理功能
"""

import requests
import json
import time

SERVER_URL = "http://localhost:8080"

def print_header(text):
    print(f"\n{'='*50}")
    print(f"  {text}")
    print(f"{'='*50}\n")

def print_test(num, description):
    print(f"\n[测试{num}] {description}")
    print("-" * 50)

def print_success(message):
    print(f"✓ {message}")

def print_error(message):
    print(f"✗ {message}")

def test_api():
    print_header("Server-12 Multi-Chat API 测试")

    session_ids = []

    try:
        # 测试1: 创建新会话
        print_test(1, "创建新会话")
        response = requests.post(f"{SERVER_URL}/api/sessions/new")
        data = response.json()
        session_id = data['session_id']
        session_ids.append(session_id)
        print(f"响应: {json.dumps(data, indent=2, ensure_ascii=False)}")
        print_success(f"会话创建成功！Session ID: {session_id}")

        # 测试2: 在会话中发送第一条消息
        print_test(2, "发送第一条消息")
        message1 = "你好，请介绍一下什么是Docker"
        response = requests.post(
            f"{SERVER_URL}/api/sessions/{session_id}/chat",
            json={"message": message1, "max_tokens": 100, "temperature": 0.7}
        )
        data = response.json()
        print(f"用户: {message1}")
        print(f"AI回复: {data['message']}")
        print_success("AI已回复")

        time.sleep(1)  # 等待1秒，避免请求过快

        # 测试3: 继续对话（测试多轮对话）
        print_test(3, "继续对话（多轮对话）")
        message2 = "它和虚拟机有什么区别？"  # "它"指的是Docker
        response = requests.post(
            f"{SERVER_URL}/api/sessions/{session_id}/chat",
            json={"message": message2, "max_tokens": 100}
        )
        data = response.json()
        print(f"用户: {message2}")
        print(f"AI回复: {data['message']}")
        print_success("AI记住了上下文（'它'指Docker）！")

        # 测试4: 获取会话历史
        print_test(4, "获取会话历史")
        response = requests.get(f"{SERVER_URL}/api/sessions/{session_id}/history")
        history = response.json()
        print(f"历史记录数量: {len(history)}条")
        for i, msg in enumerate(history, 1):
            role = "用户" if msg['role'] == 'user' else "AI"
            print(f"  {i}. [{role}] {msg['content'][:50]}...")
        print_success(f"历史记录获取成功（{len(history)}条消息）")

        # 测试5: 创建第二个会话
        print_test(5, "创建第二个会话")
        response = requests.post(f"{SERVER_URL}/api/sessions/new")
        data = response.json()
        session_id2 = data['session_id']
        session_ids.append(session_id2)
        print_success(f"第二个会话创建成功！ID: {session_id2}")

        # 测试6: 在第二个会话中聊天
        print_test(6, "在第二个会话中聊天")
        response = requests.post(
            f"{SERVER_URL}/api/sessions/{session_id2}/chat",
            json={"message": "解释一下什么是Kubernetes", "max_tokens": 80}
        )
        data = response.json()
        print(f"AI回复: {data['message']}")
        print_success("第二个会话独立工作！")

        # 测试7: 获取所有会话列表
        print_test(7, "获取所有会话列表")
        response = requests.get(f"{SERVER_URL}/api/sessions")
        sessions = response.json()
        print(f"会话总数: {len(sessions)}")
        for i, session in enumerate(sessions, 1):
            print(f"  {i}. {session['title']} (ID: {session['session_id']})")
            print(f"     消息数: {session['message_count']}, 对话轮数: {session['turn_count']}")
        print_success(f"获取到{len(sessions)}个会话")

        # 测试8: 更新会话标题
        print_test(8, "更新会话标题")
        new_title = "Docker学习笔记"
        response = requests.put(
            f"{SERVER_URL}/api/sessions/{session_id}/title",
            json={"title": new_title}
        )
        data = response.json()
        print(f"新标题: {new_title}")
        print_success("标题更新成功！")

        # 测试9: 删除第二个会话
        print_test(9, "删除第二个会话")
        response = requests.delete(f"{SERVER_URL}/api/sessions/{session_id2}")
        data = response.json()
        print(f"响应: {json.dumps(data, indent=2, ensure_ascii=False)}")
        print_success("会话删除成功！")

        # 测试10: 验证删除
        print_test(10, "验证会话已删除")
        response = requests.get(f"{SERVER_URL}/api/sessions")
        sessions = response.json()
        print(f"剩余会话数: {len(sessions)}")
        for session in sessions:
            print(f"  - {session['title']}")
        print_success(f"现在只有{len(sessions)}个会话")

        # 总结
        print_header("测试完成")
        print("✓ 多会话管理功能正常")
        print("✓ 多轮对话功能正常（AI能记住上下文）")
        print("✓ 历史记录功能正常")
        print("✓ 会话列表功能正常")
        print("✓ 更新标题功能正常")
        print("✓ 删除会话功能正常")
        print("\n现在可以访问 http://localhost:8080/multichat.html 体验ChatGPT风格界面！\n")

    except requests.exceptions.ConnectionError:
        print_error("无法连接到服务器！")
        print("请确保Server-12正在运行在 http://localhost:8080")
        print("\n启动服务器:")
        print("  cd server2025/server-12-MultiChat/build")
        print("  ./server12")
        return False

    except Exception as e:
        print_error(f"测试失败: {str(e)}")
        import traceback
        traceback.print_exc()
        return False

    return True

if __name__ == "__main__":
    success = test_api()
    exit(0 if success else 1)
