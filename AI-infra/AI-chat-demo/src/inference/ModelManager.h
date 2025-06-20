// ModelManager.h
#pragma once

#include <string>
#include <mutex>

// 单例化管理所有 LLaMA 模型上下文与推理操作
class ModelManager {
public:
    // 获取单例实例
    // 原理：利用函数局部 static 保证线程安全初始化，且只会构造一次
    static ModelManager& instance();

    // 加载模型文件
    // 参数 path：模型文件路径（GGUF 或 GGML 格式）
    //       n_ctx：上下文窗口大小（最多可同时处理多少 tokens）
    //       n_threads：推理时并行线程数
    // 返回值：加载是否成功
    bool loadModel(const std::string& path, int n_ctx = 2048, int n_threads = 4);

    // 执行一次推理（贪心解码）
    // 参数 prompt：输入的文本提示
    //       maxTokens：最多生成多少个 token
    //       temperature：目前未使用（可扩展为 Top-k/Top-p 采样）
    // 返回值：生成的字符串结果
    std::string infer(const std::string& prompt, int maxTokens, float temperature);

private:
    // 构造函数私有：禁止外部直接 new
    ModelManager();
    // 析构函数释放 llama 上下文与模型
    ~ModelManager();

    // 禁止拷贝与赋值
    ModelManager(const ModelManager&)            = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    std::mutex mtx_;                // 保护 ctx_ 与 model_ 线程安全
    struct llama_context* ctx_   = nullptr;  // llama.cpp 上下文句柄
    struct llama_model*   model_ = nullptr;  // llama 模型句柄
};
