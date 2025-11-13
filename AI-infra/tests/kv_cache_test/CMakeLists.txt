cmake_minimum_required(VERSION 3.22)
project(kv_cache_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# llama.cpp 路径
set(LLAMA_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/llama.cpp)

# 添加 llama.cpp
add_subdirectory(${LLAMA_CPP_DIR} ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp)

# 测试可执行文件
add_executable(test_state_serialization test_state_serialization.cpp)

target_include_directories(test_state_serialization PRIVATE
    ${LLAMA_CPP_DIR}/include
    ${LLAMA_CPP_DIR}/src
    ${LLAMA_CPP_DIR}/common
)

target_link_libraries(test_state_serialization PRIVATE
    llama
    common
)

# 编译选项
if(MSVC)
    target_compile_options(test_state_serialization PRIVATE /W3)
else()
    target_compile_options(test_state_serialization PRIVATE -Wall -Wextra)
endif()

message(STATUS "KV-Cache 测试程序配置完成")
message(STATUS "  - llama.cpp: ${LLAMA_CPP_DIR}")
message(STATUS "  - 可执行文件: test_state_serialization")
