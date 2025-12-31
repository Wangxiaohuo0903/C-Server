cmake_minimum_required(VERSION 3.22)
project(ai_infra_server LANGUAGES C CXX)

# ───────────────────────────────────────────────────────────
# 0.  通用编译选项：只启用“最低公共分母”指令集
#     - x86_64  : -march=x86-64 -mtune=generic
#     - ARM64   : -mcpu=cortex-a53         （极保守，任何 v8-a 都能跑）
#     - 其他架构: 不额外指定
# ───────────────────────────────────────────────────────────
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    add_compile_options(-march=x86-64 -mtune=generic)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    add_compile_options(-mcpu=cortex-a53)
endif()

# ───────────────────────────────────────────────────────────
# 1. C++ 标准及基础配置
# ───────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ───────────────────────────────────────────────────────────
# 2. 线程库（给 ggml / llama.cpp 使用）
# ───────────────────────────────────────────────────────────
find_package(Threads REQUIRED)

# ───────────────────────────────────────────────────────────
# 2.1 OpenSSL（Server-14: 用于 JWT HMAC-SHA256）
# ───────────────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")

# ───────────────────────────────────────────────────────────
# 3. llama.cpp 子模块
#    · 关闭所有 “native / 多变体” CPU 优化，避免拼出未知 -mcpu
# ───────────────────────────────────────────────────────────
set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)

# 如需 GPU，请在外部 cmake 命令行加入 -DWITH_GPU=ON
option(WITH_GPU "Enable CUDA/CUBLAS in llama.cpp" OFF)

# 计算第三方源码的绝对路径
set(LLAMA_SRC "${CMAKE_SOURCE_DIR}/third_party/llama.cpp")
# 指定编译输出放到 build/third_party/llama.cpp
set(LLAMA_BIN "${CMAKE_BINARY_DIR}/third_party/llama.cpp")

# 然后这样写：
add_subdirectory(${LLAMA_SRC} ${LLAMA_BIN})


# ───────────────────────────────────────────────────────────
# 4. 头文件搜索路径
# ───────────────────────────────────────────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include
                    ${PROJECT_SOURCE_DIR}/src)

# ───────────────────────────────────────────────────────────
# 5. 收集源文件
# ───────────────────────────────────────────────────────────
file(GLOB_RECURSE SERVER_SRCS "${PROJECT_SOURCE_DIR}/src/*.cpp")
# 移除旧的 main.cpp，使用 main_v2.cpp
list(REMOVE_ITEM SERVER_SRCS "${PROJECT_SOURCE_DIR}/src/main.cpp")

# ───────────────────────────────────────────────────────────
# 6. 生成可执行文件
# ───────────────────────────────────────────────────────────
add_executable(ai_infra_server ${SERVER_SRCS})

# ───────────────────────────────────────────────────────────
# 7. 链接依赖
# ───────────────────────────────────────────────────────────
target_link_libraries(ai_infra_server
    PRIVATE
        llama             # llama.cpp 生成的库
        Threads::Threads  # pthread
        sqlite3           # SQLite3
        OpenSSL::Crypto   # Server-14: OpenSSL for JWT
)

# ───────────────────────────────────────────────────────────
# 8. （可选）GPU
# ───────────────────────────────────────────────────────────
if (WITH_GPU)
    target_compile_definitions(ai_infra_server PRIVATE LLAMA_CUBLAS=ON)
    find_library(CUBLAS_LIBRARY cublas)
    find_library(CUDA_LIBRARY   cuda)
    if (CUBLAS_LIBRARY AND CUDA_LIBRARY)
        target_link_libraries(ai_infra_server PRIVATE ${CUBLAS_LIBRARY} ${CUDA_LIBRARY})
    else()
        message(FATAL_ERROR "WITH_GPU=ON 但未找到 CUDA/CUBLAS")
    endif()
endif()
