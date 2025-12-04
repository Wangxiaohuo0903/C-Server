# ✅ Phase 1.1 代码修复完成 - 准备测试

## 📊 当前状态

### 已完成的工作 ✅

1. **诊断早停问题** ✅
   - 创建了完整的诊断报告
   - 确认是TinyLlama模型特性，非代码bug

2. **Prompt格式优化** ✅
   - `test_task_aware.cpp:43-48` - code_generation改为引导式prompt
   - `test_task_aware.cpp:59-64` - qa_general改为引导式prompt
   - 增加max_tokens到100

3. **Batch Size修复** ✅
   - `test_task_aware.cpp:297` - Target context: `n_batch = 512`
   - `SpeculativeDecoder.cpp:58` - Draft context: `n_batch = 512`

### 预期效果

修复后应该实现：
- ✅ Part 1: 13/13 (100%) 任务分类准确率
- ✅ Part 2: 所有6个推理测试完成
- ✅ code_generation: 50-80 tokens (不是7)
- ✅ qa_general: 50-80 tokens (不是1)
- ✅ 没有GGML_ASSERT错误
- ✅ JSON和Math保持高accept rate

---

## 🚀 如何运行测试

### 方法 1: Windows一键运行 (推荐)

双击运行：
```
scripts/test_batch_fix.bat
```

这会自动:
1. 在WSL环境中清理旧编译文件
2. 重新编译（包含所有修复）
3. 运行完整测试
4. 验证修复效果

### 方法 2: WSL/Linux手动运行

```bash
cd AI-chats-linux/build

# 清理旧文件
rm -f test_task_aware
rm -f CMakeFiles/test_task_aware.dir/test_task_aware.cpp.o
find CMakeFiles/llama.dir/src/inference/ -name "*.o" -delete

# 重新编译
make test_task_aware -j$(nproc)

# 运行测试
./test_task_aware \
  --model ../models/tinyllama-q4.gguf \
  --model-draft ../models/tinyllama-q4.gguf \
2>&1 | tee ../BATCH_FIX_VERIFIED.log
```

### 方法 3: 使用自动化脚本

```bash
cd scripts
chmod +x test_batch_fix.sh
./test_batch_fix.sh
```

---

## 📋 测试验证标准

### 成功标准

- [ ] 编译成功，无错误
- [ ] Part 1: 13/13 分类正确
- [ ] Part 2: 所有6个测试运行完成
- [ ] **没有GGML_ASSERT错误** ← 关键！
- [ ] code_generation: ≥ 40 tokens
- [ ] qa_general: ≥ 40 tokens
- [ ] JSON任务: ~82% accept rate
- [ ] Math任务: ~64% accept rate

### 如果测试通过

✅ **Phase 1.1 完成！**

可以进入 **Phase 1.2**: 优化翻译任务
- 当前翻译accept rate: 13.6%
- 目标: 40%+

### 如果测试失败

检查日志中的错误：

1. **如果仍有GGML_ASSERT错误**:
   - 说明batch size修复未生效
   - 需要完全清理build目录: `rm -rf build && mkdir build && cd build && cmake ..`

2. **如果code/qa仍然早停**:
   - 检查prompt是否正确修改
   - 查看生成的token内容

3. **如果其他错误**:
   - 查看完整日志文件
   - 检查模型文件路径是否正确

---

## 📁 相关文件

### 代码修改
- `AI-chats-linux/test_task_aware.cpp` - Prompt优化 + batch size修复
- `AI-chats-linux/src/inference/SpeculativeDecoder.cpp` - Draft context batch size修复

### 文档
- `NEXT_STEPS_PLAN.md` - 完整5阶段开发计划
- `EOS_EARLY_STOP_DIAGNOSIS.md` - 早停问题诊断
- `PROMPT_FIX_PROGRESS.md` - Prompt优化详情
- `BATCH_SIZE_FIX_FINAL.md` - Batch size修复说明
- `BATCH_SIZE_FIX_STATUS.md` - 当前状态

### 测试脚本
- `scripts/test_batch_fix.bat` - Windows启动器
- `scripts/test_batch_fix.sh` - Linux自动化测试脚本

---

## 🔍 测试后检查项

运行测试后，查看日志文件，确认：

```bash
# 查看分类准确率
grep "Classification Accuracy:" BATCH_FIX_VERIFIED*.log

# 查看code_generation结果
grep -A 5 "Test: code_generation_python" BATCH_FIX_VERIFIED*.log | grep "Tokens generated:"

# 查看qa_general结果
grep -A 5 "Test: qa_general" BATCH_FIX_VERIFIED*.log | grep "Tokens generated:"

# 检查是否有GGML_ASSERT错误
grep "GGML_ASSERT" BATCH_FIX_VERIFIED*.log
```

应该看到：
- ✅ `Classification Accuracy: 13/13 (100%)`
- ✅ `Tokens generated: 50+` (code_generation)
- ✅ `Tokens generated: 50+` (qa_general)
- ✅ 没有GGML_ASSERT输出

---

## ⏭️ 下一步 (测试通过后)

1. **记录基准数据**:
   - 保存所有任务的accept rate
   - 记录token生成数量
   - 准备论文数据表格

2. **进入Phase 1.2**:
   - 优化翻译任务（当前13.6% → 目标40%+）
   - 分析翻译任务为何accept rate低
   - 测试不同prompt格式

3. **或者进入Phase 2**:
   - 性能基准测试
   - 对比标准推测式解码
   - 测量加速比和效率

---

## 💡 提示

1. **第一次运行可能较慢**（3-4分钟）
   - 包含模型加载时间
   - 13个分类测试
   - 6个完整推理测试

2. **日志文件很有价值**
   - 保存所有日志文件
   - 可用于论文数据分析
   - 便于后续对比

3. **如遇到问题**
   - 检查模型文件是否存在
   - 确保WSL/Linux环境可用
   - 查看完整错误信息

---

## 📞 需要帮助？

如果测试过程中遇到问题，请提供：
1. 完整的错误信息
2. 日志文件内容
3. 使用的测试方法（方法1/2/3）

---

**创建时间**: 2025-11-26
**当前阶段**: Phase 1.1 代码完成，等待测试验证
**下一步**: 运行测试并验证修复效果
