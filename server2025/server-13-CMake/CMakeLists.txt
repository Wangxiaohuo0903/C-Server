cmake_minimum_required(VERSION 3.15)
project(Server13 VERSION 1.0.0 LANGUAGES CXX)

# ============================================
# 编译选项配置
# ============================================
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译警告
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================
# 包含目录
# ============================================
include_directories(${PROJECT_SOURCE_DIR}/include)

# ============================================
# 源文件列表
# ============================================
set(HTTP_SOURCES
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
)

set(SOURCES
    src/main.cpp
    ${HTTP_SOURCES}
)

# ============================================
# 可执行文件
# ============================================
add_executable(server13 ${SOURCES})

# ============================================
# 链接库
# ============================================
# SQLite3
find_package(SQLite3)
if(SQLite3_FOUND)
    target_link_libraries(server13 SQLite::SQLite3)
else()
    # 如果find_package失败，尝试直接链接
    target_link_libraries(server13 sqlite3)
endif()

# 线程库
find_package(Threads REQUIRED)
target_link_libraries(server13 Threads::Threads)

# ============================================
# llama.cpp集成（暂时使用mock）
# ============================================
# 创建mock llama.cpp头文件目录
set(LLAMA_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/third_party/llama.cpp)
file(MAKE_DIRECTORY ${LLAMA_INCLUDE_DIR})

# 在configure阶段创建mock头文件
file(WRITE ${LLAMA_INCLUDE_DIR}/llama.h
"#pragma once
#include <string>
#include <vector>
#include <cstdint>

typedef int32_t llama_token;

struct llama_model {};
struct llama_context {};
struct llama_vocab {};
struct llama_batch {
    llama_token* token;
    int* pos;
    int** seq_id;
    int* n_seq_id;
    int* logits;
    int n_tokens;
};

struct llama_model_params {};
struct llama_context_params {
    int n_ctx;
    int n_threads;
    int n_batch;
};

inline llama_model_params llama_model_default_params() { return llama_model_params(); }
inline llama_context_params llama_context_default_params() { return llama_context_params(); }
inline llama_model* llama_load_model_from_file(const char*, llama_model_params) { return nullptr; }
inline void llama_free_model(llama_model*) {}
inline llama_context* llama_new_context_with_model(llama_model*, llama_context_params) { return nullptr; }
inline void llama_free(llama_context*) {}
inline const llama_vocab* llama_model_get_vocab(const llama_model*) { return nullptr; }
inline int llama_tokenize(const llama_vocab*, const char*, int, llama_token*, int, bool, bool) { return 0; }
inline llama_batch llama_batch_init(int, int, int) { return llama_batch(); }
inline void llama_batch_free(llama_batch) {}
inline int llama_decode(llama_context*, llama_batch) { return 0; }
inline const float* llama_get_logits(llama_context*) { return nullptr; }
inline int llama_get_kv_cache_used_cells(llama_context*) { return 0; }
inline llama_token llama_vocab_eos(const llama_vocab*) { return 0; }
inline int llama_vocab_n_tokens(const llama_vocab*) { return 32000; }
inline int llama_token_to_piece(const llama_vocab*, llama_token, char*, int, int, bool) { return 0; }
")

include_directories(${LLAMA_INCLUDE_DIR})

# ============================================
# 安装规则
# ============================================
install(TARGETS server13 DESTINATION bin)

# ============================================
# 打印配置信息
# ============================================
message(STATUS "========================================")
message(STATUS "Server-13 CMake Configuration")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
