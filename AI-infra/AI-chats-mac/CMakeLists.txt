cmake_minimum_required(VERSION 3.23)
project(ai_infra_server_mac LANGUAGES C CXX)

# ───────────────────────────────────────────────────────────
# 0.  通用编译选项：针对 Mac (Apple Silicon / Intel)
# ───────────────────────────────────────────────────────────
if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Apple Silicon (M1/M2/M3)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        add_compile_options(-mcpu=apple-m1)
    # Intel Mac
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
        add_compile_options(-march=x86-64 -mtune=generic)
    endif()
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
# 3. llama.cpp 子模块
#    · 关闭所有 "native / 多变体" CPU 优化
#    · Mac 支持 Metal GPU 加速
# ───────────────────────────────────────────────────────────
set(GGML_NATIVE           OFF CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)

# Mac 特有：启用 Metal 加速（Apple GPU）
option(WITH_METAL "Enable Metal GPU acceleration on Mac" ON)
if (WITH_METAL AND APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
endif()

# 计算第三方源码的绝对路径
set(LLAMA_SRC "${CMAKE_SOURCE_DIR}/../third_party/llama.cpp")
# 指定编译输出放到 build/third_party/llama.cpp
set(LLAMA_BIN "${CMAKE_BINARY_DIR}/third_party/llama.cpp")

# 添加 llama.cpp 子目录
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

# ───────────────────────────────────────────────────────────
# 6. 生成可执行文件
# ───────────────────────────────────────────────────────────
add_executable(ai_infra_server_mac ${SERVER_SRCS})

# ───────────────────────────────────────────────────────────
# 7. 链接依赖
# ───────────────────────────────────────────────────────────
target_link_libraries(ai_infra_server_mac
    PRIVATE
        llama             # llama.cpp 生成的库
        Threads::Threads  # pthread
        sqlite3           # SQLite3
)

# ───────────────────────────────────────────────────────────
# 8. Mac 特有：Metal 框架
# ───────────────────────────────────────────────────────────
if (WITH_METAL AND APPLE)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    find_library(METALKIT_FRAMEWORK MetalKit)

    if (METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
        target_link_libraries(ai_infra_server_mac PRIVATE
            ${METAL_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
        )
        if (METALKIT_FRAMEWORK)
            target_link_libraries(ai_infra_server_mac PRIVATE ${METALKIT_FRAMEWORK})
        endif()
        message(STATUS "Metal GPU acceleration enabled")
    else()
        message(WARNING "Metal frameworks not found, falling back to CPU")
    endif()
endif()

# ───────────────────────────────────────────────────────────
# 9. 测试程序
# ───────────────────────────────────────────────────────────

# 任务感知测试程序
add_executable(test_task_aware
    test_task_aware.cpp
    src/inference/SpeculativeDecoder.cpp
    src/inference/TaskClassifier.cpp
    src/inference/ConfidenceGuide.cpp
)
target_link_libraries(test_task_aware
    PRIVATE
        llama
        Threads::Threads
)

# 自适应推测式解码测试程序
if(EXISTS "${PROJECT_SOURCE_DIR}/test_adaptive_speculative.cpp")
    add_executable(test_adaptive_speculative
        test_adaptive_speculative.cpp
        src/inference/SpeculativeDecoder.cpp
        src/inference/TaskClassifier.cpp
    )
    target_link_libraries(test_adaptive_speculative
        PRIVATE
            llama
            Threads::Threads
    )
endif()

# 置信度引导测试程序
if(EXISTS "${PROJECT_SOURCE_DIR}/test_confidence_guide.cpp")
    add_executable(test_confidence_guide
        test_confidence_guide.cpp
        src/inference/ConfidenceGuide.cpp
    )
    target_link_libraries(test_confidence_guide
        PRIVATE
            llama
            Threads::Threads
    )
endif()

# 分类器测试程序
if(EXISTS "${PROJECT_SOURCE_DIR}/test_classifier_only.cpp")
    add_executable(test_classifier_only
        test_classifier_only.cpp
        src/inference/TaskClassifier.cpp
    )
    target_link_libraries(test_classifier_only
        PRIVATE
            llama
            Threads::Threads
    )
endif()

# 推测式解码测试程序
if(EXISTS "${PROJECT_SOURCE_DIR}/test_speculative.cpp")
    # 收集测试程序需要的源文件（排除main.cpp）
    file(GLOB_RECURSE TEST_SRCS "${PROJECT_SOURCE_DIR}/src/*.cpp")
    list(FILTER TEST_SRCS EXCLUDE REGEX ".*main\\.cpp$")

    add_executable(test_speculative
        test_speculative.cpp
        ${TEST_SRCS}
    )

    target_link_libraries(test_speculative
        PRIVATE
            llama
            Threads::Threads
            sqlite3
    )

    # Mac Metal 支持
    if (WITH_METAL AND APPLE)
        if (METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
            target_link_libraries(test_speculative PRIVATE
                ${METAL_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
            )
            if (METALKIT_FRAMEWORK)
                target_link_libraries(test_speculative PRIVATE ${METALKIT_FRAMEWORK})
            endif()
        endif()
    endif()
endif()

# K值优化测试
if(EXISTS "${PROJECT_SOURCE_DIR}/test_k_optimization.cpp")
    # 收集测试程序需要的源文件（排除main.cpp）
    file(GLOB_RECURSE TEST_SRCS_K "${PROJECT_SOURCE_DIR}/src/*.cpp")
    list(FILTER TEST_SRCS_K EXCLUDE REGEX ".*main\\.cpp$")

    add_executable(test_k_optimization
        test_k_optimization.cpp
        ${TEST_SRCS_K}
    )

    target_link_libraries(test_k_optimization
        PRIVATE
            llama
            Threads::Threads
            sqlite3
    )

    # Mac Metal 支持
    if (WITH_METAL AND APPLE)
        if (METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
            target_link_libraries(test_k_optimization PRIVATE
                ${METAL_FRAMEWORK}
                ${FOUNDATION_FRAMEWORK}
            )
            if (METALKIT_FRAMEWORK)
                target_link_libraries(test_k_optimization PRIVATE ${METALKIT_FRAMEWORK})
            endif()
        endif()
    endif()
endif()

# ───────────────────────────────────────────────────────────
# 10. 安装目标（可选）
# ───────────────────────────────────────────────────────────
install(TARGETS ai_infra_server_mac DESTINATION bin)
