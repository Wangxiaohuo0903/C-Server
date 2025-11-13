# AI-Chats-Linux 部署检查清单

使用此清单确保部署的每个步骤都正确完成。

---

## 📋 部署前准备

### ✅ 环境检查

- [ ] Docker 已安装并运行
  ```bash
  docker --version
  # 预期: Docker version 20.10+
  ```

- [ ] Docker Compose 已安装
  ```bash
  docker-compose --version
  # 预期: Docker Compose version 2.0+
  ```

- [ ] 系统资源充足
  - [ ] CPU: 4核心或以上
  - [ ] 内存: 8GB 或以上
  - [ ] 磁盘: 20GB 可用空间

- [ ] 网络连接正常
  ```bash
  curl -I https://huggingface.co
  # 预期: HTTP/2 200
  ```

### ✅ 项目文件准备

- [ ] 项目目录结构完整
  ```bash
  ls AI-chats-linux/
  # 应包含: Dockerfile, docker-compose.yml, src/, include/
  ```

- [ ] 关键文件存在
  - [ ] `AI-chats-linux/Dockerfile`
  - [ ] `AI-chats-linux/docker-compose.yml`
  - [ ] `AI-chats-linux/CMakeLists.txt`
  - [ ] `AI-chats-linux/src/main.cpp`
  - [ ] `AI-chats-linux/include/HttpServer.h`

- [ ] 代码已适配 Linux
  - [ ] HttpServer.h 使用 epoll（不是 kqueue）
  - [ ] ModelManager.cpp 使用新 llama.cpp API

### ✅ 模型文件准备

- [ ] models 目录已创建
  ```bash
  mkdir -p models
  ```

- [ ] 模型文件已下载
  ```bash
  ls -lh models/tinyllama-q4.gguf
  # 预期: 约 638MB
  ```

- [ ] 模型文件完整性
  ```bash
  du -h models/tinyllama-q4.gguf
  # 预期: 638M 或 607M（不同系统显示可能略有差异）
  ```

---

## 📋 构建阶段

### ✅ 镜像构建

- [ ] 开始构建
  ```bash
  cd AI-chats-linux
  docker-compose build
  ```

- [ ] 构建成功标志
  - [ ] 看到 "Built target ai_infra_server_mac"
  - [ ] 看到 "Successfully built" 或 "=> exporting to image"
  - [ ] 无致命错误（ERROR）

- [ ] 镜像已创建
  ```bash
  docker images | grep ai-chats-linux
  # 预期: 看到 ai-chats-linux-ai-server 镜像
  ```

- [ ] 镜像大小合理
  ```bash
  docker images | grep ai-chats-linux
  # 预期: 约 2GB
  ```

---

## 📋 启动阶段

### ✅ 容器启动

- [ ] 启动容器
  ```bash
  docker-compose up -d
  ```

- [ ] 容器运行中
  ```bash
  docker ps | grep ai-chats
  # 预期: 状态为 "Up"
  ```

- [ ] 容器健康状态
  ```bash
  docker ps | grep ai-chats
  # 预期: 状态包含 "healthy"
  ```

### ✅ 日志检查

- [ ] 查看启动日志
  ```bash
  docker-compose logs
  ```

- [ ] 关键日志标志
  - [ ] 看到 "[Model] loaded ok"
  - [ ] 看到 "Cache Warmup Started"
  - [ ] 看到 "Cache Warmup Complete"
  - [ ] 无错误信息（ERROR）

- [ ] 模型加载成功
  ```bash
  docker logs ai-chats-server | grep "loaded ok"
  # 预期: [Model] loaded ok: ../../models/tinyllama-q4.gguf
  ```

---

## 📋 功能测试

### ✅ 网络连通性

- [ ] 端口监听正常
  ```bash
  # Windows
  netstat -ano | findstr :8081

  # Linux/Mac
  netstat -tuln | grep 8081
  ```

- [ ] TCP 连接成功
  ```bash
  # Windows
  Test-NetConnection -ComputerName 127.0.0.1 -Port 8081

  # Linux/Mac
  nc -zv 127.0.0.1 8081
  ```

### ✅ HTTP 接口测试

- [ ] 根路径可访问
  ```bash
  curl http://localhost:8081/
  # 预期: 返回 302 重定向或其他 HTTP 响应
  ```

- [ ] 推理接口可用
  ```bash
  curl -X POST http://localhost:8081/infer \
    -H "Content-Type: application/json" \
    -d '{"prompt":"Hello","chat_id":"test"}'
  # 预期: 返回 JSON 格式的推理结果
  ```

- [ ] 推理结果正确
  - [ ] 响应包含 "answer" 字段
  - [ ] answer 内容非空
  - [ ] 响应时间合理（<30秒）

### ✅ API 完整性测试

- [ ] /infer 接口（推理）
  ```bash
  curl -X POST http://localhost:8081/infer \
    -H "Content-Type: application/json" \
    -d '{"user_message":"测试","chat_id":"test","max_tokens":64}'
  ```

- [ ] /reset 接口（重置会话）
  ```bash
  curl -X POST http://localhost:8081/reset \
    -H "Content-Type: application/json" \
    -d '{"chat_id":"test"}'
  ```

- [ ] /register 接口（用户注册）
  ```bash
  curl -X POST http://localhost:8081/register \
    -H "Content-Type: application/json" \
    -d '{"username":"testuser","password":"test123"}'
  ```

- [ ] /login 接口（用户登录）
  ```bash
  curl -X POST http://localhost:8081/login \
    -H "Content-Type: application/json" \
    -d '{"username":"testuser","password":"test123"}'
  ```

---

## 📋 性能检查

### ✅ 资源使用

- [ ] CPU 使用率正常
  ```bash
  docker stats ai-chats-server --no-stream
  # 预期: CPU < 100% (空闲时), < 400% (推理时，4核心)
  ```

- [ ] 内存使用率正常
  ```bash
  docker stats ai-chats-server --no-stream
  # 预期: MEM USAGE < 2GB (空闲), < 4GB (推理)
  ```

- [ ] 无内存泄漏迹象
  - [ ] 长时间运行后内存使用稳定
  - [ ] 无 OOM (Out of Memory) 错误

### ✅ 推理性能

- [ ] 推理速度可接受
  ```bash
  time curl -X POST http://localhost:8081/infer \
    -H "Content-Type: application/json" \
    -d '{"prompt":"test","max_tokens":64}'
  # 预期: < 30 秒
  ```

- [ ] 并发处理正常
  - [ ] 可以同时处理多个请求
  - [ ] 不会因并发而崩溃

---

## 📋 稳定性检查

### ✅ 重启测试

- [ ] 容器可以正常重启
  ```bash
  docker-compose restart
  ```

- [ ] 重启后服务自动恢复
  ```bash
  # 等待 30 秒
  sleep 30
  curl http://localhost:8081/
  ```

- [ ] 数据持久化正常
  - [ ] 重启后用户数据仍存在
  - [ ] 会话信息正确保存

### ✅ 错误恢复

- [ ] 异常请求不会导致崩溃
  ```bash
  # 发送无效 JSON
  curl -X POST http://localhost:8081/infer \
    -H "Content-Type: application/json" \
    -d 'invalid json'
  # 预期: 返回错误信息，服务继续运行
  ```

- [ ] 服务器日志正常
  ```bash
  docker logs ai-chats-server --tail 20
  # 预期: 无严重错误
  ```

---

## 📋 安全检查

### ✅ 访问控制

- [ ] 端口映射正确
  ```bash
  docker port ai-chats-server
  # 预期: 8080/tcp -> 0.0.0.0:8081
  ```

- [ ] 仅暴露必要端口
  - [ ] 数据库端口未暴露
  - [ ] 调试端口未暴露

### ✅ 数据安全

- [ ] 数据卷权限正确
  ```bash
  docker volume inspect ai-chats-linux_ai-chats-data
  ```

- [ ] 敏感信息未泄露
  - [ ] 环境变量不包含密码
  - [ ] 日志不输出敏感信息

---

## 📋 文档检查

### ✅ 文档完整性

- [ ] 必要文档存在
  - [ ] `DEPLOYMENT.md` - 部署手册
  - [ ] `QUICKREF.md` - 快速参考
  - [ ] `CHECKLIST.md` - 本检查清单
  - [ ] `README.md` - 项目说明
  - [ ] `DOCKER_GUIDE.md` - Docker 指南

- [ ] 文档内容准确
  - [ ] 端口号与实际配置一致
  - [ ] 命令示例可以正常执行
  - [ ] 路径配置正确

---

## 📋 生产环境额外检查

### ✅ 监控和日志

- [ ] 日志轮转配置
  ```yaml
  # docker-compose.yml
  logging:
    driver: "json-file"
    options:
      max-size: "10m"
      max-file: "3"
  ```

- [ ] 监控工具配置（可选）
  - [ ] Prometheus
  - [ ] Grafana
  - [ ] 告警规则

### ✅ 备份策略

- [ ] 数据备份脚本准备
  ```bash
  # 测试备份
  docker run --rm \
    -v ai-chats-linux_ai-chats-data:/data \
    -v $(pwd):/backup \
    ubuntu tar czf /backup/test-backup.tar.gz /data
  ```

- [ ] 备份自动化（可选）
  - [ ] 定时备份脚本
  - [ ] 备份验证流程

### ✅ 高可用性（可选）

- [ ] 负载均衡配置
- [ ] 故障转移机制
- [ ] 健康检查端点

---

## 📋 最终验证

### ✅ 完整流程测试

- [ ] 端到端测试
  1. [ ] 用户注册成功
  2. [ ] 用户登录成功
  3. [ ] 发起推理请求成功
  4. [ ] 多轮对话正常
  5. [ ] 重置会话成功

- [ ] 压力测试（可选）
  ```bash
  # 使用 ab (Apache Bench) 测试
  ab -n 100 -c 10 -p post.json -T application/json \
    http://localhost:8081/infer
  ```

### ✅ 运维文档

- [ ] 操作手册已准备
- [ ] 故障排查指南已准备
- [ ] 联系人信息已记录
- [ ] 升级计划已制定

---

## 🎉 部署完成

所有检查项都通过后，部署即完成！

### 下一步行动：

1. **记录部署信息**
   - 部署日期: ___________
   - 服务地址: http://localhost:8081
   - 模型版本: TinyLlama-1.1B-Q4
   - Docker 镜像: ai-chats-linux-ai-server:latest

2. **通知相关人员**
   - [ ] 开发团队
   - [ ] 运维团队
   - [ ] 测试团队

3. **持续监控**
   - [ ] 设置监控告警
   - [ ] 定期检查日志
   - [ ] 跟踪性能指标

4. **定期维护**
   - [ ] 每周检查日志
   - [ ] 每月备份数据
   - [ ] 季度性能评估

---

## 📞 获取帮助

如果遇到问题，请查阅：

1. **DEPLOYMENT.md** - 详细部署指南和故障排除
2. **QUICKREF.md** - 常用命令快速参考
3. **DOCKER_GUIDE.md** - Docker 详细说明

---

**检查清单版本**: v1.0
**最后更新**: 2025-11-10
**检查人**: ___________
**检查日期**: ___________
