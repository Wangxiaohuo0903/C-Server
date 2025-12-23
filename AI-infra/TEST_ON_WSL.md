# 在 Windows WSL2 中测试 AI-chats-linux

## 🚀 快速开始

### 1. 安装WSL2 (PowerShell管理员)

```powershell
# 启用WSL
wsl --install

# 安装Ubuntu 22.04
wsl --install -d Ubuntu-22.04

# 检查WSL版本
wsl --list --verbose
```

### 2. 进入WSL Ubuntu

```powershell
wsl
```

### 3. 安装依赖

```bash
# 更新包管理器
sudo apt update

# 安装编译工具
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libsqlite3-dev \
    pkg-config

# 验证安装
gcc --version    # 应该显示 11.x 或更高
cmake --version  # 应该显示 3.22 或更高
```

### 4. 访问Windows文件

```bash
# Windows的C盘在WSL中映射为 /mnt/c/
cd /mnt/c/Users/实习生/Documents/Code/server/C-Server/AI-infra

# 或者将项目复制到WSL的home目录（推荐，更快）
cp -r /mnt/c/Users/实习生/Documents/Code/server/C-Server/AI-infra ~/
cd ~/AI-infra
```

### 5. 编译 AI-chats-linux

```bash
cd AI-chats-linux

# 创建build目录
mkdir -p build && cd build

# 配置CMake
cmake ..

# 编译（使用所有CPU核心）
make -j$(nproc)

# 查看编译结果
ls -lh
```

**预期输出**:
```
ai_infra_server_mac          # 主程序
test_task_aware              # 测试程序
test_adaptive_speculative
test_confidence_guide
test_classifier_only
test_speculative
```

### 6. 下载模型

```bash
# 返回项目根目录
cd ~/AI-infra

# 创建模型目录
mkdir -p models
cd models

# 使用代理下载模型（使用你的Windows代理）
export HTTP_PROXY="http://127.0.0.1:7890"
export HTTPS_PROXY="http://127.0.0.1:7890"

# 下载TinyLlama模型（约600MB）
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf

# 或者使用curl
curl -L -o tinyllama-q4.gguf \
  https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf
```

### 7. 运行测试

```bash
cd ~/AI-infra/AI-chats-linux/build

# 测试1: 任务分类器
./test_classifier_only
# 输出: 识别30个prompts的任务类型

# 测试2: 置信度引导
./test_confidence_guide
# 输出: 置信度计算 + 相关性分析

# 测试3: 推测式解码
./test_speculative
# 输出: 接受率 + 加速比

# 测试4: 任务感知
./test_task_aware
# 输出: 不同任务的性能对比
```

### 8. 启动服务器

```bash
# 运行服务器
./ai_infra_server_mac

# 在另一个终端测试API（新开一个PowerShell）
wsl
curl http://localhost:8080
# 输出: Hello, World!

# 测试推理
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "Write a Python function to calculate fibonacci",
    "chat_id": "test_001",
    "max_tokens": 100
  }'
```

---

## 🔧 常见问题

### Q1: WSL访问Windows代理失败

**问题**: WSL无法访问Windows的127.0.0.1:7890代理

**解决**:
```bash
# 获取Windows主机IP
cat /etc/resolv.conf | grep nameserver | awk '{print $2}'
# 假设输出: 172.20.144.1

# 使用Windows主机IP设置代理
export HTTP_PROXY="http://172.20.144.1:7890"
export HTTPS_PROXY="http://172.20.144.1:7890"

# 测试代理
curl -I https://www.google.com
```

### Q2: 编译速度慢

**原因**: 直接在 /mnt/c/ 下编译会很慢（跨文件系统）

**解决**: 将项目复制到WSL的home目录
```bash
cp -r /mnt/c/Users/实习生/Documents/Code/server/C-Server/AI-infra ~/
cd ~/AI-infra
```

### Q3: 模型文件太大下载失败

**方案1**: 使用Windows下载，然后访问
```powershell
# 在Windows PowerShell中下载
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\models

# 使用代理下载
$env:HTTP_PROXY="http://127.0.0.1:7890"
Invoke-WebRequest -Uri "https://huggingface.co/..." -OutFile "tinyllama.gguf"
```

**方案2**: 使用断点续传
```bash
# 在WSL中使用wget的断点续传
wget -c https://huggingface.co/.../tinyllama.gguf
```

### Q4: 端口被占用

**检查**:
```bash
# 查看8080端口占用
sudo lsof -i :8080

# 杀死占用进程
sudo kill -9 <PID>
```

---

## 📊 性能对比: WSL vs 原生Linux

| 指标 | WSL2 | 原生Linux | 差异 |
|------|------|----------|------|
| **编译速度** | ~40秒 | ~30秒 | +33% |
| **推理速度** | ~95% | 100% | -5% |
| **I/O性能** | ~80% | 100% | -20% |
| **网络性能** | ~98% | 100% | -2% |

**结论**: WSL2性能已经非常接近原生Linux，完全可以用于开发和测试

---

## 🎯 优化建议

### 1. 使用WSL2（不是WSL1）

```powershell
# 检查版本
wsl --list --verbose

# 如果是WSL1，升级到WSL2
wsl --set-version Ubuntu-22.04 2
```

### 2. 启用systemd（可选）

编辑 `/etc/wsl.conf`:
```ini
[boot]
systemd=true
```

### 3. 配置WSL内存限制

创建 `C:\Users\实习生\.wslconfig`:
```ini
[wsl2]
memory=8GB      # 限制WSL使用8GB内存
processors=4    # 使用4个CPU核心
```

### 4. 配置Git换行符

```bash
# 在WSL中
git config --global core.autocrlf input
```

---

## 🚀 进阶: 使用VSCode Remote WSL

### 安装VSCode扩展

1. 安装 "Remote - WSL" 扩展
2. 点击左下角绿色图标
3. 选择 "Connect to WSL"

### 优势

- ✅ 直接在WSL中编辑代码
- ✅ 集成终端自动在WSL运行
- ✅ 智能感知、调试都在WSL环境
- ✅ 无需手动同步文件

---

## 📝 完整测试脚本

```bash
#!/bin/bash
# test_all.sh - 一键测试所有功能

set -e  # 遇到错误立即退出

echo "=== 1. 测试任务分类器 ==="
./build/test_classifier_only

echo ""
echo "=== 2. 测试置信度引导 ==="
./build/test_confidence_guide

echo ""
echo "=== 3. 测试推测式解码 ==="
./build/test_speculative

echo ""
echo "=== 4. 测试任务感知 ==="
./build/test_task_aware

echo ""
echo "=== 5. 测试自适应推测式解码 ==="
./build/test_adaptive_speculative

echo ""
echo "✅ 所有测试完成！"
```

使用方法:
```bash
chmod +x test_all.sh
./test_all.sh
```

---

**测试环境**: WSL2 + Ubuntu 22.04
**推荐配置**: 8GB+ RAM, 4+ CPU cores
**预计用时**: 首次编译约5分钟，后续测试约2分钟
