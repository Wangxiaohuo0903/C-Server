#!/usr/bin/env python3
import requests
import json
import time

BASE_URL = "http://localhost:8080"

def test_register():
    print("=" * 50)
    print("测试用户注册")
    print("=" * 50)
    data = {
        "username": f"test_{int(time.time())}",
        "password": "123456"
    }
    response = requests.post(f"{BASE_URL}/register", json=data)
    print(f"Status: {response.status_code}")
    print(f"Response: {response.text}")
    return data

def test_login(username, password):
    print("\n" + "=" * 50)
    print("测试用户登录")
    print("=" * 50)
    data = {
        "username": username,
        "password": password
    }
    response = requests.post(f"{BASE_URL}/login", json=data)
    print(f"Status: {response.status_code}")
    print(f"Response: {response.text}")

def test_infer():
    print("\n" + "=" * 50)
    print("测试AI推理")
    print("=" * 50)
    data = {
        "chat_id": f"session_{int(time.time())}",
        "prompt": "你好，请介绍一下自己",
        "max_tokens": 50,
        "temperature": 0.7
    }
    print(f"Request: {json.dumps(data, ensure_ascii=False)}")
    response = requests.post(f"{BASE_URL}/infer", json=data)
    print(f"Status: {response.status_code}")
    print(f"Response: {response.text}")

if __name__ == "__main__":
    # 测试注册
    user_data = test_register()

    # 测试登录
    test_login(user_data["username"], user_data["password"])

    # 测试AI推理
    test_infer()

    print("\n" + "=" * 50)
    print("✅ 所有测试完成！")
    print("=" * 50)
