# 🎯 推测式解码优化 - 下一步行动计划

## 当前状态总结

### ✅ 已完成
- [x] 发现并修复verification off-by-one错误
- [x] JSON生成任务达到82%接受率
- [x] 数学推理任务达到64%接受率
- [x] 任务分类器100%准确率
- [x] Greedy采样配置验证

### ⚠️ 待解决问题
- [ ] 代码生成和问答任务提前停止（1-7 tokens）
- [ ] 翻译任务接受率偏低（13.6%）
- [ ] 创意写作任务接受率低（19.4%，但符合预期）

---

## 📋 Phase 1: 紧急问题修复 (1-2天)

### 🔴 Priority 1: 调查提前停止问题

**问题**: 代码生成(7 tokens)和问答(1 token)任务异常提前结束

**可能原因**:
1. Prompt格式导致模型立即输出EOS
2. 中英文混合prompt处理问题
3. max_tokens或停止条件bug
4. 模型对特定prompt的响应异常

**行动步骤**:

#### Step 1.1: 检查生成的实际内容
```bash
# 查看test log中这两个任务的实际输出文本
grep -A20 "code_generation_python (Full Inference)" CRITICAL_FIX_test_results.log
grep -A20 "qa_general (Full Inference)" CRITICAL_FIX_test_results.log
```

**目标**: 确认是否遇到EOS，还是其他停止条件

#### Step 1.2: 测试简化prompt
```cpp
// 修改test_task_aware.cpp，使用简单prompt测试
{
    "code_generation_simple",
    "Write a Python function",  // 纯英文，简化
    TaskType::CODE_GENERATION,
    24, 32,
    0.0f,
    100  // 增加max_tokens
}
```

**目标**: 排除prompt复杂度问题

#### Step 1.3: 添加调试日志
```cpp
// SpeculativeDecoder.cpp 的 inferTokens() 函数中
if (generated.size() >= max_tokens) {
    std::cerr << "[Debug] Stopped: reached max_tokens=" << max_tokens << "\n";
}
if (next_token == eos_token) {
    std::cerr << "[Debug] Stopped: encountered EOS token\n";
}
```

**目标**: 精确定位停止原因

**预期结果**: 找到并修复提前停止的根本原因

**时间**: 0.5天

---

### 🟡 Priority 2: 优化翻译任务接受率

**问题**: 翻译任务只有13.6%接受率

**可能原因**:
1. 中英文混合导致draft和target分布不一致
2. n_draft参数不适合翻译任务
3. Prompt格式需要优化

**行动步骤**:

#### Step 2.1: 测试不同语言组合
```cpp
// 添加测试用例
{
    "translation_en_only",
    "Translate to Chinese: Hello",  // 纯英文prompt
    TaskType::TRANSLATION,
    20, 28,
    0.0f,
    40
},
{
    "translation_zh_only",
    "翻译成英语：你好",  // 纯中文prompt
    TaskType::TRANSLATION,
    20, 28,
    0.0f,
    40
}
```

**目标**: 确定是否是语言混合导致的问题

#### Step 2.2: 调整n_draft参数
```cpp
// 尝试不同的n_draft值
n_draft_candidates = {12, 16, 20, 24, 28};
// 运行实验找到最佳值
```

**目标**: 找到翻译任务的最佳n_draft配置

#### Step 2.3: 添加verbose日志分析
```cpp
// 打印draft和target的token差异
if (config_.verbose) {
    std::cerr << "[Verify] Draft token=" << draft_token
              << " (" << detokenize(draft_token) << ")"
              << " prob=" << draft_prob << "\n";
}
```

**目标**: 分析为什么draft token不被接受

**预期结果**: 将翻译任务接受率提升到40%+

**时间**: 0.5天

---

## 📊 Phase 2: 性能基准测试和优化 (2-3天)

### 目标: 建立完整的性能基准和最佳配置

#### Task 2.1: 创建性能基准测试套件

**文件**: `benchmark/speculative_benchmark.cpp`

```cpp
// 测试不同配置的性能
struct BenchmarkConfig {
    std::string task_type;
    int n_draft;
    float temperature;
    bool enable_adaptive;
    bool enable_task_aware;
};

// 运行100次测试，取平均值
void runBenchmark(BenchmarkConfig config) {
    // 1. 测试accept rate
    // 2. 测试speedup
    // 3. 测试tokens/sec
    // 4. 输出统计报告
}
```

**测试矩阵**:
| Task Type | n_draft | Temperature | Expected Accept Rate |
|-----------|---------|-------------|---------------------|
| JSON | 20, 24, 28, 32 | 0.0 | 75-85% |
| Math | 16, 20, 22, 24 | 0.0 | 60-70% |
| Code | 20, 24, 28, 32 | 0.0 | TBD |
| Translation | 12, 16, 20, 24 | 0.0 | TBD |
| QA | 12, 16, 20 | 0.0 | TBD |

**输出**: `BENCHMARK_RESULTS.md` 包含所有任务的最佳配置

**时间**: 1天

---

#### Task 2.2: 实现真实Speedup测量

**问题**: 当前speedup计算不准确（显示0.1x）

**修复**:
```cpp
// SpeculativeDecoder.cpp
double calculateSpeedup() {
    // Speedup = (draft阶段并行处理的token数) / (总时间/单token平均时间)
    double time_per_token_normal = time_verify_ms / stats_.n_predict;
    double time_saved = stats_.n_accepted * time_per_token_normal;
    double speedup = (time_total_ms + time_saved) / time_total_ms;
    return speedup;
}
```

**或者更简单**:
```cpp
// Speedup = 生成的token数 / (等效自回归所需的decode次数)
// 等效decode次数 = n_predict - n_accepted + 1
double speedup = (double)stats_.n_predict / (stats_.n_predict - stats_.n_accepted + 1);
```

**验证**:
- JSON任务(82% accept rate) → 应该得到 ~1.8x speedup
- Math任务(64% accept rate) → 应该得到 ~1.5x speedup

**时间**: 0.5天

---

#### Task 2.3: 对比不同模型配置

**实验配置**:
1. **相同模型**: Q4 + Q4 (当前)
2. **不同大小**: Q4 (target) + Q2 (draft)
3. **不同量化**: Q4 (target) + FP16 (draft)

**测试脚本**:
```bash
# test_model_combinations.sh
for draft_model in q2 q4 fp16; do
    for target_model in q4 q8; do
        ./test_task_aware \
            --model $target_model \
            --model-draft $draft_model \
            > results_${target}_${draft}.log
    done
done
```

**分析维度**:
- Accept rate
- Speedup
- Memory usage
- Latency

**时间**: 1天

---

## 🚀 Phase 3: 生产部署准备 (2-3天)

### Task 3.1: 创建易用的API接口

**文件**: `include/SpeculativeInference.h`

```cpp
class SpeculativeInference {
public:
    // 简化的用户接口
    static std::unique_ptr<SpeculativeInference> create(
        const std::string& model_path,
        const std::string& draft_model_path = ""  // 可选，默认使用相同模型
    );

    // 智能推理：自动检测任务类型并优化
    std::string infer(
        const std::string& prompt,
        int max_tokens = 100,
        float temperature = 0.0f
    );

    // 高级接口：手动指定任务类型
    std::string inferWithTaskType(
        const std::string& prompt,
        TaskType task_type,
        int max_tokens = 100,
        float temperature = 0.0f
    );

    // 获取性能统计
    PerformanceStats getStats() const;
};
```

**使用示例**:
```cpp
auto inference = SpeculativeInference::create("model.gguf");

// 自动检测任务类型并优化
std::string json = inference->infer(
    "Generate a JSON user profile",
    100,
    0.0f  // greedy for edge computing
);

auto stats = inference->getStats();
std::cout << "Accept rate: " << stats.accept_rate << "%\n";
std::cout << "Speedup: " << stats.speedup << "x\n";
```

**时间**: 1天

---

### Task 3.2: 编写部署文档

**文件**: `docs/DEPLOYMENT_GUIDE.md`

**内容大纲**:
1. **快速开始**
   - 依赖安装
   - 模型下载
   - 编译指令
   - Hello World示例

2. **任务类型配置**
   - JSON生成 (82% accept rate)
   - 数学推理 (64% accept rate)
   - 其他任务

3. **性能优化指南**
   - 选择合适的n_draft
   - 何时使用greedy vs temperature
   - 内存优化
   - CPU线程配置

4. **边缘设备部署**
   - 树莓派配置
   - 资源受限设备优化
   - 批处理策略

5. **故障排查**
   - 低accept rate问题
   - 内存溢出
   - 性能瓶颈诊断

**时间**: 1天

---

### Task 3.3: 创建Docker部署镜像

**文件**: `docker/Dockerfile.production`

```dockerfile
FROM ubuntu:22.04

# 安装依赖
RUN apt-get update && apt-get install -y \
    cmake build-essential libgomp1

# 复制编译好的二进制
COPY build/speculative_inference /usr/local/bin/
COPY models/ /models/

# 运行示例
ENTRYPOINT ["speculative_inference"]
CMD ["--model", "/models/tinyllama-q4.gguf", \
     "--draft-model", "/models/tinyllama-q4.gguf"]
```

**使用方式**:
```bash
docker build -t speculative-inference .
docker run speculative-inference \
    --task json \
    --prompt "Generate user profile JSON"
```

**时间**: 0.5天

---

## 📝 Phase 4: 论文和文档撰写 (3-5天)

### Task 4.1: 撰写技术论文

**标题**: "Task-Aware Speculative Decoding for Edge Computing: Achieving 82% Token Acceptance Rate"

**大纲**:

#### 1. Abstract
- 推测式解码在边缘计算的挑战
- 任务感知优化方法
- 关键成果：JSON 82%, Math 64%

#### 2. Introduction
- 边缘计算的推理需求
- 推测式解码原理
- 现有方法的局限性

#### 3. Background
- Speculative Decoding算法
- Draft-Verify-Accept流程
- 温度采样 vs Greedy采样

#### 4. Methodology
- 任务分类器设计
- 任务感知配置策略
- Verification算法实现

#### 5. Key Technical Contribution
- **Verification Off-by-One Bug发现**
- 正确的batch-draft对齐方法
- 对accept rate的巨大影响

#### 6. Experiments
- 测试设置 (TinyLlama Q4)
- 6类任务测试结果
- Accept rate分析
- Speedup测量

#### 7. Results
- JSON: 82% accept rate
- Math: 64% accept rate
- 任务类型对比
- 与baseline对比

#### 8. Discussion
- 为什么greedy最适合推测式解码
- Temperature采样的问题
- 边缘计算的实际意义

#### 9. Limitations
- 部分任务仍需优化
- 模型大小限制
- 语言混合问题

#### 10. Conclusion
- 推测式解码在边缘计算已可用
- 任务感知优化的重要性
- 未来工作方向

**时间**: 3天

---

### Task 4.2: 开源准备

**GitHub Repository结构**:
```
speculative-decoding-edge/
├── README.md                    # 项目介绍
├── docs/
│   ├── DEPLOYMENT_GUIDE.md      # 部署指南
│   ├── API_REFERENCE.md         # API文档
│   ├── BENCHMARK_RESULTS.md     # 性能基准
│   └── TROUBLESHOOTING.md       # 故障排查
├── src/
│   ├── inference/               # 核心代码
│   ├── classifier/              # 任务分类
│   └── utils/                   # 工具函数
├── tests/
│   ├── test_task_aware.cpp      # 测试程序
│   └── benchmark/               # 性能测试
├── examples/
│   ├── json_generation.cpp      # JSON生成示例
│   ├── math_reasoning.cpp       # 数学推理示例
│   └── ...
├── docker/
│   └── Dockerfile.production    # 部署镜像
└── CMakeLists.txt
```

**README.md重点**:
- 82% JSON accept rate的醒目展示
- 快速开始示例
- 性能对比图表
- 引用论文

**时间**: 1天

---

## 🎓 Phase 5: 毕业论文撰写 (并行进行)

### 大纲建议

**中文题目**: 面向边缘计算的任务感知推测式解码优化研究

**英文题目**: Task-Aware Speculative Decoding Optimization for Edge Computing

#### 第一章 绪论
1.1 研究背景
- 大语言模型的边缘部署需求
- 推理速度瓶颈
- 边缘设备资源限制

1.2 研究意义
- 边缘AI的实用价值
- 推测式解码的加速潜力
- 任务感知优化的必要性

1.3 研究内容
- 任务分类器设计
- 推测式解码算法优化
- 性能评估与部署

1.4 论文组织结构

#### 第二章 相关工作
2.1 大语言模型推理优化
- KV Cache
- 量化技术
- 模型剪枝

2.2 推测式解码
- 原始算法 (Leviathan et al. 2022)
- 变种方法
- 现有问题

2.3 边缘计算与AI
- 边缘设备特点
- 资源约束
- 应用场景

#### 第三章 任务感知推测式解码设计
3.1 系统架构
- 整体设计
- 模块划分
- 数据流

3.2 任务分类器
- 关键词匹配
- 8类任务定义
- 置信度计算

3.3 任务感知配置策略
- n_draft参数选择
- accept_rate目标
- 自适应调整

3.4 推测式解码实现
- Draft生成
- 批量验证
- Token接受

#### 第四章 关键技术问题与解决
4.1 Verification Off-by-One Bug
- 问题发现过程
- 根本原因分析
- 修复方案
- 影响评估 (10倍性能提升!)

4.2 采样策略优化
- Greedy vs Temperature
- 理论分析
- 实验验证

4.3 性能优化
- KV Cache复用
- 批处理优化
- 内存管理

#### 第五章 实验与评估
5.1 实验设置
- 硬件环境
- 模型选择 (TinyLlama Q4)
- 测试数据集

5.2 任务分类器评估
- 准确率: 100%
- 置信度分布
- 误分类分析

5.3 推测式解码性能
- 6类任务测试结果
- Accept rate分析
  - JSON: 82%
  - Math: 64%
  - 其他: 13-19%
- Speedup测量
- 延迟分析

5.4 对比实验
- 与标准自回归对比
- 不同配置对比
- 消融研究

5.5 边缘设备部署
- 树莓派测试
- 资源消耗
- 实际应用案例

#### 第六章 系统实现与部署
6.1 系统实现
- 开发环境
- 技术栈
- 代码组织

6.2 API设计
- 用户接口
- 使用示例
- 性能监控

6.3 部署方案
- Docker镜像
- 配置指南
- 最佳实践

#### 第七章 总结与展望
7.1 研究总结
- 主要贡献
- 关键成果
- 技术创新

7.2 局限性
- 部分任务优化不足
- 模型大小限制
- 通用性问题

7.3 未来工作
- 更多任务类型支持
- 更大模型适配
- 生产环境优化

---

## 📅 时间线总结

| 阶段 | 任务 | 时间 | 优先级 |
|------|------|------|--------|
| **Phase 1** | 紧急问题修复 | 1-2天 | 🔴 高 |
| - Task 1.1 | 调查提前停止问题 | 0.5天 | 🔴 |
| - Task 1.2 | 优化翻译任务 | 0.5天 | 🟡 |
| **Phase 2** | 性能基准测试 | 2-3天 | 🟡 中 |
| - Task 2.1 | 基准测试套件 | 1天 | 🟡 |
| - Task 2.2 | Speedup测量修复 | 0.5天 | 🟡 |
| - Task 2.3 | 模型配置对比 | 1天 | 🟢 |
| **Phase 3** | 生产部署准备 | 2-3天 | 🟡 中 |
| - Task 3.1 | API接口设计 | 1天 | 🟡 |
| - Task 3.2 | 部署文档 | 1天 | 🟡 |
| - Task 3.3 | Docker镜像 | 0.5天 | 🟢 |
| **Phase 4** | 论文与开源 | 3-5天 | 🟢 低 |
| - Task 4.1 | 技术论文撰写 | 3天 | 🟢 |
| - Task 4.2 | 开源准备 | 1天 | 🟢 |
| **Phase 5** | 毕业论文 | 并行 | 🟡 中 |

**总时间**: 约2-3周 (不含毕业论文全文)

---

## 💡 立即可执行的任务

### 今天 (Day 1):
1. ✅ 查看test log确认提前停止原因
2. ✅ 添加调试日志
3. ✅ 测试简化prompt
4. ✅ 找到并修复提前停止bug

### 明天 (Day 2):
1. 优化翻译任务配置
2. 修复speedup计算
3. 运行完整基准测试

### 本周 (Week 1):
1. 完成Phase 1和Phase 2
2. 建立完整的性能基准
3. 确定所有任务的最佳配置

### 下周 (Week 2):
1. 完成Phase 3
2. 准备生产部署
3. 开始论文撰写

---

## 🎯 成功标准

### Phase 1成功标准:
- [ ] 代码生成任务能正常生成50+ tokens
- [ ] 问答任务能正常生成50+ tokens
- [ ] 翻译任务accept rate > 40%

### Phase 2成功标准:
- [ ] 完整的性能基准报告
- [ ] 准确的speedup测量（JSON > 1.5x, Math > 1.3x）
- [ ] 所有任务的最佳配置文档

### Phase 3成功标准:
- [ ] 易用的API接口
- [ ] 完整的部署文档
- [ ] 可运行的Docker镜像

### Phase 4成功标准:
- [ ] 技术论文初稿完成
- [ ] GitHub仓库准备就绪
- [ ] 开源许可确定

---

## 📞 需要决策的问题

1. **论文投稿目标**:
   - 会议 (ICML, NeurIPS, ACL?)
   - 期刊 (JMLR, TACL?)
   - 还是只作为毕业论文?

2. **开源时间**:
   - 立即开源
   - 论文发表后开源
   - 毕业后开源

3. **模型选择**:
   - 继续使用TinyLlama
   - 测试更大模型 (Llama-7B, Mistral?)
   - 测试其他小模型

4. **部署重点**:
   - 专注边缘设备 (树莓派, Jetson)
   - 服务器端优化
   - 两者兼顾

---

**建议**: 先完成Phase 1紧急问题修复，确保基础功能完整，再进行后续优化和部署准备。

需要我先开始哪个任务？
