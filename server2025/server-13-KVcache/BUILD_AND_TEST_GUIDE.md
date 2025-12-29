# Server-13-ContextPool 构建和测试指南

## 快速开始

### 1. 使用Docker构建（推荐）

```bash
# 进入项目目录
cd server-13-ContextPool

# 构建镜像
docker-compose -f docker-compose.server13.yml build

# 启动服务
docker-compose -f docker-compose.server13.yml up -d

# 查看日志
docker-compose -f docker-compose.server13.yml logs -f
```

### 2. 本地构建（需要CMake）

#### 前置条件
- CMake >= 3.15
- C++17 编译器 (GCC 9+, Clang 10+, MSVC 2019+)
- Git

#### 构建步骤

```bash
# 1. 克隆或确保 llama.cpp 子模块存在
git submodule update --init --recursive

# 2. 构建 llama.cpp
cd third_party/llama.cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLLAMA_CURL=OFF
cmake --build build --config Release -j$(nproc)
cd ../..

# 3. 构建 Server-13
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)

# 4. 运行
./build/server2025 /path/to/model.gguf
```

---

## 验证优化效果

### 测试1: 批处理性能测试

#### 测试目标
验证真正的多序列批处理是否生效，GPU利用率是否提升。

#### 测试脚本
```bash
#!/bin/bash
# test_batch_performance.sh

echo "测试批处理性能..."

# 启动服务器（确保启用批处理）
# docker-compose up -d

# 测试单请求延迟（基准）
echo "=== 单请求延迟测试 ==="
time curl -X POST http://localhost:8080/infer \
    -H "Content-Type: application/json" \
    -d '{"prompt":"你好", "max_tokens":50}'

# 测试批处理延迟（10个并发请求）
echo ""
echo "=== 批处理延迟测试（10个并发） ==="
start_time=$(date +%s.%N)

for i in {1..10}; do
    curl -X POST http://localhost:8080/infer \
        -H "Content-Type: application/json" \
        -d "{\"prompt\":\"Hello $i\", \"max_tokens\":50}" \
        -o /dev/null -s &
done
wait

end_time=$(date +%s.%N)
elapsed=$(echo "$end_time - $start_time" | bc)

echo "批处理总耗时: ${elapsed}s"
echo "平均每请求: $(echo "$elapsed / 10" | bc -l)s"
```

#### 预期结果
```
单请求延迟: ~100ms
批处理总延迟: ~150ms（不是1000ms！）
平均每请求: ~15ms（6.7倍提升）
```

#### 日志验证
查看服务器日志，应该看到：
```
[BatchInferenceEngine] Processed batch of 10 requests in 150ms
```

---

### 测试2: KV缓存复用测试

#### 测试目标
验证多轮对话时KV缓存是否正确复用，prompt格式是否一致。

#### 测试脚本
```bash
#!/bin/bash
# test_kv_cache.sh

echo "测试KV缓存复用..."

# 1. 创建新会话
SESSION_ID=$(curl -X POST http://localhost:8080/api/sessions/new -s | jq -r '.session_id')
echo "创建会话: $SESSION_ID"

# 2. 第一轮对话（完整推理）
echo ""
echo "=== 第1轮对话 ==="
time curl -X POST "http://localhost:8080/api/sessions/$SESSION_ID/chat" \
    -H "Content-Type: application/json" \
    -d '{"message":"你好", "max_tokens":50}' -s | jq '.message'

# 3. 第二轮对话（应该复用KV缓存）
echo ""
echo "=== 第2轮对话（应该更快） ==="
time curl -X POST "http://localhost:8080/api/sessions/$SESSION_ID/chat" \
    -H "Content-Type: application/json" \
    -d '{"message":"你叫什么名字？", "max_tokens":50}' -s | jq '.message'

# 4. 第三轮对话
echo ""
echo "=== 第3轮对话（应该更快） ==="
time curl -X POST "http://localhost:8080/api/sessions/$SESSION_ID/chat" \
    -H "Content-Type: application/json" \
    -d '{"message":"今天天气怎么样？", "max_tokens":50}' -s | jq '.message'
```

#### 预期结果
```
第1轮: 500ms（完整推理100 tokens）
第2轮: 100ms（只推理新增20 tokens，5倍提升）
第3轮: 100ms（只推理新增20 tokens，5倍提升）
```

#### 日志验证
查看服务器日志，应该看到：
```
[SessionContextPool] Session sess_XXX - Reusing 120 tokens, processing 22 new tokens
[SessionContextPool] Session sess_XXX - Reusing 142 tokens, processing 24 new tokens
```

**不应该看到**：
```
[SessionContextPool] KV cache mismatch, resetting context  ❌
```

---

### 测试3: KV缓存不一致处理测试

#### 测试目标
验证当KV缓存不一致时，是否使用精确删除而非粗暴重建。

#### 测试场景
手动构造KV缓存不一致的场景（需要修改代码或使用调试工具）。

#### 简化测试方法
```bash
# 1. 正常对话
curl -X POST ".../chat" -d '{"message":"第一条消息"}'
curl -X POST ".../chat" -d '{"message":"第二条消息"}'
curl -X POST ".../chat" -d '{"message":"第三条消息"}'

# 2. 查看会话历史
curl -X GET ".../sessions/$SESSION_ID/history"

# 3. 删除最后一条消息（需要实现DELETE API）
# curl -X DELETE ".../sessions/$SESSION_ID/messages/last"

# 4. 发送新消息（会触发KV缓存不一致处理）
curl -X POST ".../chat" -d '{"message":"新的第三条消息"}'
```

#### 日志验证
应该看到：
```
[SessionContextPool] KV cache mismatch (common_prefix=142, cached=164)
[SessionContextPool] Removing excess KV cache from position 142 to 164
```

**不应该看到**：
```
[SessionContextPool] resetting context  ❌（说明还在用旧实现）
```

---

## 性能基准测试

### 测试环境
- CPU: [填写]
- GPU: [填写]
- RAM: [填写]
- Model: [填写模型名称和大小]

### 测试结果模板

#### 批处理性能
| 并发数 | 旧实现耗时 | 新实现耗时 | 加速比 |
|--------|-----------|-----------|--------|
| 1      |           |           |        |
| 5      |           |           |        |
| 10     |           |           |        |
| 20     |           |           |        |

#### KV缓存复用性能
| 对话轮数 | 旧实现耗时 | 新实现耗时 | 加速比 |
|---------|-----------|-----------|--------|
| 1       |           |           |   1x   |
| 3       |           |           |        |
| 5       |           |           |        |
| 10      |           |           |        |

---

## 常见问题排查

### Q1: 批处理没有加速效果

**可能原因**:
1. 批处理引擎未启动
   ```bash
   # 检查启动日志
   docker logs server13_container | grep "BatchInferenceEngine started"
   ```

2. 请求间隔太长，未能聚合成batch
   ```bash
   # 调整batch_timeout_ms参数
   # 在main.cpp中: loadModel(..., batch_size=10, batch_timeout_ms=200)
   ```

3. 仍在使用旧代码
   ```bash
   # 确认编译时间
   ls -l build/server2025
   # 重新构建
   cmake --build build --clean-first
   ```

### Q2: KV缓存未复用

**可能原因**:
1. Prompt格式不一致
   ```bash
   # 检查日志中的prompt内容
   # 应该是: "User: xxx\nAssistant: xxx\n"
   ```

2. Session未正确保存
   ```bash
   # 检查session创建日志
   docker logs server13_container | grep "SessionContextPool"
   ```

3. 使用了不同的session_id
   ```bash
   # 确认使用相同的session_id
   echo $SESSION_ID
   ```

### Q3: 编译错误

#### 错误: `llama_kv_cache_seq_rm` 未定义

**原因**: llama.cpp版本过旧

**解决**:
```bash
cd third_party/llama.cpp
git pull origin master  # 更新到最新版本
cmake -B build --clean-first
cmake --build build
```

#### 错误: `llama_get_logits_ith` 未定义

**原因**: llama.cpp版本过旧

**解决**: 同上

---

## 调试技巧

### 1. 启用详细日志

修改代码，添加更多日志输出：
```cpp
// 在 BatchInferenceEngine::processBatch 开头
std::cout << "[DEBUG] Batch size: " << batch.size() << std::endl;
for (const auto& seq : sequences) {
    std::cout << "[DEBUG] Seq " << seq.seq_id
              << " prompt_tokens: " << seq.prompt_tokens.size() << std::endl;
}
```

### 2. 使用GDB调试

```bash
# 编译debug版本
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# 运行GDB
gdb ./build/server2025
(gdb) break BatchInferenceEngine::processBatch
(gdb) run /path/to/model.gguf
```

### 3. 监控GPU利用率

```bash
# NVIDIA GPU
watch -n 1 nvidia-smi

# 期望看到GPU利用率在批处理时接近80-100%
```

### 4. 性能分析

```bash
# 使用perf（Linux）
perf record -g ./build/server2025 /path/to/model.gguf
perf report

# 使用valgrind（内存泄漏检测）
valgrind --leak-check=full ./build/server2025 /path/to/model.gguf
```

---

## 回滚到旧版本

如果新实现出现问题，可以回滚到优化前的版本：

```bash
# 1. 检出优化前的commit
git log --oneline
git checkout <commit_hash_before_optimization>

# 2. 重新构建
cmake --build build --clean-first

# 3. 或者使用git stash临时保存修改
git stash
# 测试旧版本...
git stash pop  # 恢复修改
```

---

## 生产部署建议

### 1. 资源配置
```yaml
# docker-compose.yml
services:
  server13:
    deploy:
      resources:
        limits:
          cpus: '4'
          memory: 8G
        reservations:
          cpus: '2'
          memory: 4G
```

### 2. 健康检查
```yaml
healthcheck:
  test: ["CMD", "curl", "-f", "http://localhost:8080/health"]
  interval: 30s
  timeout: 10s
  retries: 3
```

### 3. 日志管理
```yaml
logging:
  driver: "json-file"
  options:
    max-size: "100m"
    max-file: "3"
```

### 4. 监控指标
- 批处理大小（avg, p50, p95, p99）
- 批处理延迟（avg, p50, p95, p99）
- KV缓存命中率
- GPU利用率
- 内存占用

---

## 技术支持

如有问题，请：
1. 查看 `OPTIMIZATION_SUMMARY.md` 了解优化详情
2. 查看服务器日志: `docker logs server13_container`
3. 提交Issue并附上：
   - 测试脚本
   - 服务器日志
   - 系统环境信息
   - 预期行为 vs 实际行为

---

## 更新日志

### 2025-12-25
- ✅ 实现真正的多序列批处理推理
- ✅ 统一Prompt格式，确保KV缓存复用有效
- ✅ 优化KV缓存不一致处理，使用精确删除
- 📝 添加构建和测试指南
- 📝 添加性能基准测试模板
