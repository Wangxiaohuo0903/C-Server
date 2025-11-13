#!/usr/bin/env python3
import socket

# 创建 TCP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)

try:
    # 连接到服务器
    sock.connect(('127.0.0.1', 8081))
    print("[OK] TCP connection established")

    # 发送 HTTP GET 请求
    request = b"GET / HTTP/1.1\r\nHost: localhost:8081\r\nConnection: close\r\n\r\n"
    sock.sendall(request)
    print("[OK] HTTP request sent")

    # 接收响应
    response = b""
    while True:
        data = sock.recv(4096)
        if not data:
            break
        response += data

    # 打印响应
    print("\n[HTTP Response]")
    print("=" * 60)
    print(response.decode('utf-8', errors='ignore'))
    print("=" * 60)

except socket.timeout:
    print("[ERROR] Connection timed out")
except Exception as e:
    print(f"[ERROR] {e}")
finally:
    sock.close()
