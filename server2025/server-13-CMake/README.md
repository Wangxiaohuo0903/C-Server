# Server-13: CMake构建 + 模块化重构

## 🎯 核心改进

### 1. CMake构建系统
- ✅ 自动化编译管理
- ✅ 跨平台支持（Linux/macOS/Windows）
- ✅ 依赖库自动查找
- ✅ 增量编译优化

### 2. 模块化目录结构
```
server-13-CMake/
├── CMakeLists.txt          # CMake配置文件
├── include/                # 头文件目录
│   ├── http/              # HTTP模块
│   │   ├── HttpServer.h
│   │   ├── HttpRequest.h
│   │   ├── HttpResponse.h
│   │   └── Router.h
│   ├── inference/         # AI推理模块
│   │   ├── SessionManager.h
│   │   └── SimpleInference.h
│   ├── database/          # 数据库模块
│   │   └── Database.h
│   └── utils/             # 工具模块
│       ├── ThreadPool.h
│       └── Logger.h
├── src/                    # 源文件目录
│   ├── main.cpp
│   └── http/
│       ├── HttpRequest.cpp
│       └── HttpResponse.cpp
├── third_party/            # 第三方库
│   └── llama.cpp/         # (当前使用mock)
└── build/                  # 构建输出（不提交）
```

### 3. 模块化include路径
```cpp
// Server-12 (旧)
#include "HttpServer.h"
#include "SessionManager.h"

// Server-13 (新)
#include "http/HttpServer.h"
#include "inference/SessionManager.h"
```

---

## 🚀 快速开始

### 方法1: Linux/WSL环境

```bash
# 1. 进入项目目录
cd server-13-CMake

# 2. 创建build目录
mkdir build && cd build

# 3. CMake配置
cmake ..

# 4. 编译
make

# 5. 运行
./server13
```

### 方法2: Docker环境（推荐）

```bash
# 构建Docker镜像
docker build -t server13-cmake .

# 运行容器
docker run -p 8080:8080 server13-cmake
```

---

## 📋 CMake配置说明

### 编译选项
- **C++标准**: C++11
- **编译警告**: 启用（-Wall -Wextra）
- **优化级别**: 可通过CMAKE_BUILD_TYPE控制

### 依赖库
| 库名称 | 用途 | 查找方式 |
|--------|------|----------|
| **SQLite3** | 数据库 | find_package() |
| **pthread** | 多线程 | find_package(Threads) |
| **llama.cpp** | AI推理 | 第三方库（当前mock） |

### 自定义配置

```bash
# Debug模式
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release模式
cmake -DCMAKE_BUILD_TYPE=Release ..

# 指定安装路径
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..

# 指定编译器
cmake -DCMAKE_CXX_COMPILER=g++-11 ..
```

---

## 🔧 开发指南

### 添加新源文件

**方法1: 修改CMakeLists.txt**
```cmake
set(HTTP_SOURCES
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/YourNewFile.cpp  # 添加这里
)
```

**方法2: 使用GLOB（不推荐生产环境）**
```cmake
file(GLOB_RECURSE SOURCES "src/*.cpp")
```

### 添加新模块

1. 创建include和src子目录
2. 添加头文件到`include/your_module/`
3. 添加源文件到`src/your_module/`
4. 更新CMakeLists.txt的SOURCES列表

### IDE支持

**CLion / VSCode (CMake Tools)**
- 自动识别CMakeLists.txt
- 支持代码补全和跳转
- 集成调试功能

**生成compile_commands.json**
```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
```

---

## 🆚 与Server-12对比

| 维度 | Server-12 | Server-13 |
|------|-----------|-----------|
| **编译方式** | `g++ main.cpp -I... -l...` | `cmake .. && make` |
| **目录结构** | 单层平铺 | 分层模块化 |
| **include路径** | 相对路径 | 模块化路径 |
| **依赖管理** | 手动指定 | CMake自动查找 |
| **增量编译** | ❌ | ✅ |
| **跨平台** | Linux only | ✅ 全平台 |
| **IDE支持** | ⚠️ 需手动配置 | ✅ 自动识别 |

---

## 🐛 故障排查

### 问题1: sqlite3找不到
```bash
# Ubuntu/Debian
sudo apt-get install libsqlite3-dev

# macOS
brew install sqlite3

# 或手动指定路径
cmake -DSQLite3_INCLUDE_DIR=/path/to/include ..
```

### 问题2: 编译警告
```bash
# 临时禁用警告
make VERBOSE=1 2>&1 | grep -v "warning"

# 或修改CMakeLists.txt移除警告选项
```

### 问题3: llama.cpp mock不工作
```bash
# 检查mock头文件是否生成
ls third_party/llama.cpp/llama.h

# 重新配置CMake
rm -rf build/* && cd build && cmake ..
```

---

## 📊 构建统计

| 指标 | 数值 |
|------|------|
| 源文件数 | 3个cpp |
| 头文件数 | 10+个h |
| 编译时间 | ~5秒 |
| 二进制大小 | ~500KB |
| 外部依赖 | sqlite3, pthread |

---

## 🎯 下一步计划

### Server-14: Docker基础部署
- 完整Dockerfile（真实llama.cpp）
- 模型文件管理
- 生产环境配置

### Server-15: Docker优化
- 多阶段构建
- 镜像体积优化
- 健康检查和日志

---

## 📚 相关文档

- [CMake官方文档](https://cmake.org/documentation/)
- [CMake教程](https://cmake.org/cmake/help/latest/guide/tutorial/index.html)
- [llama.cpp GitHub](https://github.com/ggerganov/llama.cpp)
