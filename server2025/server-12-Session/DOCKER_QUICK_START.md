# Server-12 Docker快速开始指南

## 🚀 一键启动

### 方法1: Docker Compose（推荐）

```bash
# 1. 进入Server-12目录
cd server-12-Session

# 2. 构建并启动（测试模式）
docker-compose -f docker-compose.test.yml up -d

# 3. 查看日志
docker logs server12-test

# 4. 测试API
curl -m 5 -X POST http://localhost:8081/chat/create
```

### 方法2: PowerShell自动化脚本

```powershell
# 运行自动化测试脚本
.\test_docker.ps1
```

---

## 📡 API测试示例

### 1. 创建会话
```bash
curl -X POST http://localhost:8081/chat/create
# 响应: {"success":true,"session_id":"sess_xxx"}
```

### 2. 发送消息（多轮对话）
```bash
# 第1轮：告诉模型你的名字
curl -X POST http://localhost:8081/chat \
  -H "Content-Type: application/json" \
  -d '{"session_id":"sess_xxx","message":"My name is Alice","max_tokens":"32"}'

# 第2轮：询问名字（验证记忆）
curl -X POST http://localhost:8081/chat \
  -H "Content-Type: application/json" \
  -d '{"session_id":"sess_xxx","message":"What is my name?","max_tokens":"32"}'
```

### 3. 查询会话历史
```bash
curl "http://localhost:8081/chat/history?session_id=sess_xxx"
```

### 4. 删除会话
```bash
curl -X DELETE http://localhost:8081/chat/delete \
  -H "Content-Type: application/json" \
  -d '{"session_id":"sess_xxx"}'
```

---

## 🔧 常用Docker命令

```bash
# 启动容器
docker-compose -f docker-compose.test.yml up -d

# 停止容器
docker-compose -f docker-compose.test.yml down

# 重启容器
docker-compose -f docker-compose.test.yml restart

# 查看日志
docker logs server12-test
docker logs -f server12-test  # 实时跟踪

# 查看容器状态
docker ps | findstr server12-test

# 进入容器调试
docker exec -it server12-test bash

# 重新构建镜像
docker-compose -f docker-compose.test.yml build --no-cache
```

---

## ⚠️ 注意事项

### 测试模式说明
- **当前配置**: 使用mock llama.cpp（快速测试）
- **模型路径包含"mock"**: 自动进入测试模式
- **mock响应格式**: `[TEST MODE] Mock response to: <prompt>...`
- **功能验证**: 会话管理、上下文记忆、会话隔离

### 端口冲突
- **测试端口**: 8081（避免与ai-chats容器的8080冲突）
- 如需修改: 编辑`docker-compose.test.yml`中的`ports`配置

---

## 📊 验证清单

- [x] ✅ 容器成功启动（`docker ps`显示Up状态）
- [x] ✅ 端口8081监听中（`netstat -an | findstr :8081`）
- [x] ✅ 会话创建返回session_id
- [x] ✅ 多轮对话上下文记忆正常
- [x] ✅ 不同会话数据隔离
- [x] ✅ 会话历史查询正常

---

## 🐛 故障排查

### 容器启动失败
```bash
# 查看详细日志
docker logs server12-test --tail 100

# 检查编译错误
docker-compose -f docker-compose.test.yml build 2>&1 | tee build.log
```

### API无响应
```bash
# 1. 检查容器是否运行
docker ps | findstr server12-test

# 2. 检查端口监听
netstat -an | findstr :8081

# 3. 进入容器检查进程
docker exec server12-test ps aux
```

### 重新构建容器
```bash
# 停止并删除容器
docker-compose -f docker-compose.test.yml down

# 清理镜像
docker rmi server-12-session-server12-test

# 重新构建
docker-compose -f docker-compose.test.yml build
docker-compose -f docker-compose.test.yml up -d
```

---

## 📚 相关文档

- [DOCKER_TEST.md](./DOCKER_TEST.md) - 详细测试指南
- [DOCKER_TEST_RESULTS.md](./DOCKER_TEST_RESULTS.md) - 测试结果报告
- [test_docker.ps1](./test_docker.ps1) - PowerShell自动化脚本
- [docker-compose.test.yml](./docker-compose.test.yml) - Docker Compose配置

---

## 🎯 下一步

完成Server-12测试后，继续：
- **Server-13**: CMake构建系统 + 模块化重构
- **Server-14**: Docker生产部署（真实llama.cpp）
- **Server-15**: Docker优化（多阶段构建、健康检查、日志）
