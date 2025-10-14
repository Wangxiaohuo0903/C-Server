# 模型文件说明

本项目需要下载 LLM 模型文件才能运行。由于模型文件体积较大（约 600MB），未包含在 git 仓库中。

## 📥 下载模型

### 选项 1：TinyLlama 1.1B Q4 量化版本（推荐）

这是一个轻量级的 LLM 模型，适合本地运行：

```bash
# 下载模型文件
cd models/
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf

# 重命名为项目使用的文件名
mv tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf tinyllama-q4.gguf
```

**或者使用 curl：**

```bash
cd models/
curl -L -o tinyllama-q4.gguf https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf
```

### 选项 2：使用浏览器下载

1. 访问 [TinyLlama HuggingFace 页面](https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF)
2. 下载 `tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf` 文件
3. 将文件重命名为 `tinyllama-q4.gguf`
4. 放置在 `models/` 目录下

## 📊 模型信息

- **名称**: TinyLlama 1.1B Chat v1.0
- **量化**: Q4_K_M（4-bit 量化）
- **大小**: 约 607 MB
- **参数量**: 1.1B
- **上下文长度**: 2048 tokens
- **训练数据**: 3 trillion tokens
- **许可**: Apache 2.0

## 🔍 验证安装

模型下载完成后，验证文件是否正确：

```bash
# 检查文件是否存在
ls -lh models/tinyllama-q4.gguf

# 文件大小应该约为 607MB
```

输出应该类似于：
```
-rw-r--r--  1 user  staff   607M  date  models/tinyllama-q4.gguf
```

## 🚀 其他可选模型

如果你想尝试其他模型，可以从以下来源下载：

### 更小的模型（更快，质量稍低）
- **TinyLlama Q2**: 约 400MB
- **Phi-2 Q4**: 约 1.6GB

### 更大的模型（更慢，质量更高）
- **Llama 2 7B Q4**: 约 3.8GB
- **Mistral 7B Q4**: 约 4.1GB

**注意**: 更换模型后需要修改代码中的模型路径。

## 📝 模型存储位置

```
AI-infra/
├── models/
│   ├── README.md          # 本文件
│   └── tinyllama-q4.gguf  # 下载的模型文件
```

## ⚠️ 注意事项

1. **不要提交模型文件到 git**
   - 模型文件已在 `.gitignore` 中被忽略
   - 请勿使用 `git add -f` 强制添加

2. **硬盘空间**
   - 确保有至少 1GB 的可用空间
   - 下载完成后可以删除临时文件

3. **网络问题**
   - 如果下载失败，可以尝试使用 VPN
   - HuggingFace 镜像站点：https://hf-mirror.com/

## 🆘 常见问题

### Q: 下载速度太慢怎么办？
A: 可以使用迅雷、IDM 等下载工具，或者使用 HuggingFace 镜像站点。

### Q: 如何使用其他模型？
A:
1. 下载模型到 `models/` 目录
2. 修改 `AI-chats-mac/src/main.cpp` 中的模型路径
3. 重新编译项目

### Q: 模型文件损坏怎么办？
A: 删除文件后重新下载，或使用 `md5sum` 验证文件完整性。

---

**如有问题，请参考项目主 README 或提交 Issue。**
