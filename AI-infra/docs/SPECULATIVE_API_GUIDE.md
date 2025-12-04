# 推测式解码 HTTP API 使用指南

本文档介绍如何通过 HTTP API 使用推测式解码功能。

## 目录

- [快速开始](#快速开始)
- [API 端点](#api-端点)
  - [推理接口](#1-推理接口-post-infer)
  - [加载 Draft 模型](#2-加载-draft-模型-post-load_draft_model)
  - [启用/禁用推测式解码](#3-启用禁用推测式解码-post-set_speculative_mode)
  - [查询状态](#4-查询推测式解码状态-get-speculative_status)
- [完整示例](#完整示例)
- [性能对比](#性能对比)

---

## 快速开始

### 1. 启动服务器

```bash
cd AI-chats-linux/build
./main
```

服务器将在 `http://localhost:8080` 启动。

### 2. 加载 Draft 模型

```bash
curl -X POST http://localhost:8080/load_draft_model \
  -H "Content-Type: application/json" \
  -d '{"draft_model_path": "../../models/draft/tinyllama-160m-q4.gguf"}'
```

### 3. 启用推测式解码

```bash
curl -X POST http://localhost:8080/set_speculative_mode \
  -H "Content-Type: application/json" \
  -d '{"enable": "true"}'
```

### 4. 发送推理请求

```bash
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "用Python实现快速排序算法",
    "max_tokens": 100,
    "temperature": 0.7,
    "use_speculative": "true"
  }'
```

---

## API 端点

### 1. 推理接口 `POST /infer`

主要推理接口，支持传统推理和推测式解码两种模式。

#### 请求参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `user_message` | string | ✅ | - | 用户输入消息 |
| `chat_id` | string | ❌ | "default" | 会话ID，用于多轮对话 |
| `max_tokens` | int | ❌ | 64 | 最大生成 token 数 |
| `temperature` | float | ❌ | 0.7 | 采样温度 |
| `use_speculative` | bool | ❌ | false | 是否使用推测式解码 |

**兼容性**: 也支持 `prompt` 字段（向后兼容）。

#### 响应格式

##### 传统推理模式 (`use_speculative: false`)

```json
{
  "answer": "生成的文本内容...",
  "mode": "normal"
}
```

##### 推测式解码模式 (`use_speculative: true`)

```json
{
  "answer": "生成的文本内容...",
  "mode": "speculative",
  "stats": {
    "tokens": 100,           // 生成的总 token 数
    "drafted": 150,          // Draft 模型生成的 token 数
    "accepted": 90,          // 被接受的 draft token 数
    "accept_rate": 0.60,     // 接受率 (60%)
    "speedup": 1.85,         // 加速比 (1.85x)
    "time_ms": 542.3         // 总耗时 (ms)
  }
}
```

#### 示例

**传统推理**:
```bash
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "什么是机器学习？",
    "max_tokens": 50,
    "temperature": 0.7
  }'
```

**推测式解码**:
```bash
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "什么是机器学习？",
    "max_tokens": 50,
    "temperature": 0.7,
    "use_speculative": "true"
  }'
```

---

### 2. 加载 Draft 模型 `POST /load_draft_model`

在使用推测式解码之前，必须先加载 draft 模型。

#### 请求参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `draft_model_path` | string | ✅ | Draft 模型文件路径 |

#### 响应格式

```json
{
  "success": true,
  "message": "Draft model loaded successfully"
}
```

#### 示例

```bash
curl -X POST http://localhost:8080/load_draft_model \
  -H "Content-Type: application/json" \
  -d '{"draft_model_path": "../../models/draft/tinyllama-160m-q4.gguf"}'
```

#### 推荐的 Draft 模型

- **TinyLlama-160M-Q4** (推荐): 平衡速度和准确性
- **TinyLlama-500M-Q4**: 更高的接受率，稍慢
- **SmolLM-135M-Q4**: 最快，但接受率可能较低

详见 [Draft 模型选择指南](draft-models-guide.md)。

---

### 3. 启用/禁用推测式解码 `POST /set_speculative_mode`

全局启用或禁用推测式解码功能。

#### 请求参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `enable` | bool | ✅ | `true` 启用, `false` 禁用 |

#### 响应格式

```json
{
  "success": true,
  "message": "Speculative decoding enabled"
}
```

#### 示例

**启用**:
```bash
curl -X POST http://localhost:8080/set_speculative_mode \
  -H "Content-Type: application/json" \
  -d '{"enable": "true"}'
```

**禁用**:
```bash
curl -X POST http://localhost:8080/set_speculative_mode \
  -H "Content-Type: application/json" \
  -d '{"enable": "false"}'
```

**注意**:
- 必须先加载 draft 模型才能启用推测式解码
- 禁用后，所有 `/infer` 请求将回退到传统推理模式

---

### 4. 查询推测式解码状态 `GET /speculative_status`

获取当前推测式解码的启用状态和累积统计信息。

#### 请求参数

无

#### 响应格式

**未启用时**:
```json
{
  "enabled": false
}
```

**已启用时**:
```json
{
  "enabled": true,
  "stats": {
    "total_tokens": 1500,      // 累计生成 token 数
    "total_drafted": 2400,     // 累计 draft token 数
    "total_accepted": 1440,    // 累计接受数
    "accept_rate": 0.60,       // 平均接受率
    "speedup": 1.85            // 平均加速比
  }
}
```

#### 示例

```bash
curl -X GET http://localhost:8080/speculative_status
```

---

## 完整示例

### Python 示例

```python
import requests
import json

BASE_URL = "http://localhost:8080"

def load_draft_model(draft_model_path):
    """加载 draft 模型"""
    response = requests.post(
        f"{BASE_URL}/load_draft_model",
        json={"draft_model_path": draft_model_path}
    )
    return response.json()

def enable_speculative(enable=True):
    """启用/禁用推测式解码"""
    response = requests.post(
        f"{BASE_URL}/set_speculative_mode",
        json={"enable": str(enable).lower()}
    )
    return response.json()

def infer(message, use_speculative=False, max_tokens=100):
    """发送推理请求"""
    response = requests.post(
        f"{BASE_URL}/infer",
        json={
            "user_message": message,
            "max_tokens": max_tokens,
            "temperature": 0.7,
            "use_speculative": str(use_speculative).lower()
        }
    )
    return response.json()

def get_status():
    """获取推测式解码状态"""
    response = requests.get(f"{BASE_URL}/speculative_status")
    return response.json()

# 主流程
if __name__ == "__main__":
    # 1. 加载 draft 模型
    print("Loading draft model...")
    result = load_draft_model("../../models/draft/tinyllama-160m-q4.gguf")
    print(f"✅ {result['message']}")

    # 2. 启用推测式解码
    print("\nEnabling speculative decoding...")
    result = enable_speculative(True)
    print(f"✅ {result['message']}")

    # 3. 发送推理请求（传统方式）
    print("\n--- Normal Inference ---")
    result = infer("用Python实现快速排序算法", use_speculative=False)
    print(f"Mode: {result['mode']}")
    print(f"Answer: {result['answer'][:100]}...")

    # 4. 发送推理请求（推测式解码）
    print("\n--- Speculative Inference ---")
    result = infer("用Python实现快速排序算法", use_speculative=True)
    print(f"Mode: {result['mode']}")
    print(f"Answer: {result['answer'][:100]}...")
    if 'stats' in result:
        stats = result['stats']
        print(f"\nStatistics:")
        print(f"  Tokens: {stats['tokens']}")
        print(f"  Accept Rate: {stats['accept_rate']*100:.1f}%")
        print(f"  Speedup: {stats['speedup']:.2f}x")
        print(f"  Time: {stats['time_ms']:.1f} ms")

    # 5. 查看累积统计
    print("\n--- Global Status ---")
    status = get_status()
    print(f"Enabled: {status['enabled']}")
    if 'stats' in status:
        stats = status['stats']
        print(f"Total Tokens: {stats['total_tokens']}")
        print(f"Average Accept Rate: {stats['accept_rate']*100:.1f}%")
        print(f"Average Speedup: {stats['speedup']:.2f}x")
```

### JavaScript 示例

```javascript
const BASE_URL = "http://localhost:8080";

async function loadDraftModel(draftModelPath) {
  const response = await fetch(`${BASE_URL}/load_draft_model`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ draft_model_path: draftModelPath })
  });
  return await response.json();
}

async function enableSpeculative(enable = true) {
  const response = await fetch(`${BASE_URL}/set_speculative_mode`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ enable: enable.toString() })
  });
  return await response.json();
}

async function infer(message, useSpeculative = false, maxTokens = 100) {
  const response = await fetch(`${BASE_URL}/infer`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user_message: message,
      max_tokens: maxTokens,
      temperature: 0.7,
      use_speculative: useSpeculative.toString()
    })
  });
  return await response.json();
}

async function getStatus() {
  const response = await fetch(`${BASE_URL}/speculative_status`);
  return await response.json();
}

// 使用示例
(async () => {
  // 加载模型
  await loadDraftModel("../../models/draft/tinyllama-160m-q4.gguf");

  // 启用推测式解码
  await enableSpeculative(true);

  // 推理
  const result = await infer("用Python实现快速排序算法", true, 100);
  console.log("Answer:", result.answer);
  console.log("Stats:", result.stats);

  // 查看状态
  const status = await getStatus();
  console.log("Status:", status);
})();
```

---

## 性能对比

### 预期性能提升

| 场景 | 接受率 | 加速比 | 说明 |
|------|--------|--------|------|
| 代码生成 | 55-65% | 1.8-2.2x | 高度结构化，可预测性强 |
| 结构化输出 (JSON) | 50-60% | 1.7-2.0x | 格式固定，容易预测 |
| 对话问答 | 40-50% | 1.4-1.7x | 中等可预测性 |
| 创意写作 | 30-40% | 1.2-1.5x | 多样性高，较难预测 |

### 何时使用推测式解码？

**✅ 推荐使用**:
- 代码生成和补全
- 结构化数据生成 (JSON, XML)
- 模板化内容生成
- 技术文档写作
- 批量推理任务

**❌ 不推荐使用**:
- 创意写作（小说、诗歌）
- 高温度采样 (temperature > 1.0)
- 需要极高多样性的场景
- 单次推理延迟敏感的实时应用

### 性能调优建议

1. **调整 draft token 数量**: 在 ModelManager 中配置 `n_draft` (默认 16)
   - 增加 `n_draft` → 更高的潜在加速，但接受率可能下降
   - 减少 `n_draft` → 更稳定的接受率，加速比略低

2. **选择合适的 draft 模型**:
   - 更小的模型 → 更快的 draft 速度
   - 更大的模型 → 更高的接受率

3. **温度参数**:
   - 低温度 (0.3-0.7) → 推测式解码效果更好
   - 高温度 (> 1.0) → 推测式解码效果下降

---

## 故障排查

### 问题：推测式解码未生效

**症状**: 请求中设置 `use_speculative: true`，但响应仍显示 `"mode": "normal"`

**原因**:
1. Draft 模型未加载
2. 推测式解码未启用
3. Draft 模型加载失败

**解决方法**:
```bash
# 检查状态
curl http://localhost:8080/speculative_status

# 重新加载 draft 模型
curl -X POST http://localhost:8080/load_draft_model \
  -d '{"draft_model_path": "../../models/draft/tinyllama-160m-q4.gguf"}'

# 重新启用
curl -X POST http://localhost:8080/set_speculative_mode \
  -d '{"enable": "true"}'
```

### 问题：接受率过低

**症状**: `accept_rate < 30%`，加速比接近 1x

**可能原因**:
- Draft 模型与 target 模型词表不兼容
- 温度参数过高
- 任务本身不适合推测式解码

**解决方法**:
- 尝试不同的 draft 模型
- 降低 temperature 参数
- 考虑使用传统推理

---

## 其他资源

- [推测式解码实现文档](../research/speculative-decoding-research.md)
- [架构设计文档](../design/speculative-decoding-architecture.md)
- [Draft 模型选择指南](draft-models-guide.md)
- [性能基准测试](../scripts/run_full_benchmark.sh)

---

**最后更新**: 2025-01-17
