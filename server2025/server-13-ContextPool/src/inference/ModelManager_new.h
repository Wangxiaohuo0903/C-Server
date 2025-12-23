#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sstream>n#include "ContextPool.h"

/*==========================================================
 * 对话消息结构定义
 *
 * 【新增功能】相比server-9，这是全新增加的推理模块基础结构
 *
 * 用于存储多轮对话中的单条消息，遵循ChatML格式规范
 *=========================================================*/

/**
 * @struct InferenceMessage
 * @brief 单条对话消息（推理内部使用）
 *
 * 存储聊天对话中的单条消息，包括消息角色和内容
 * 遵循ChatML协议格式，用于构建符合大模型要求的prompt
 */
struct InferenceMessage {
    std::string role;     // 消息角色�?user"（用户）| "assistant"（助手）| "system"（系统）
    std::string content;  // 消息文本内容
};

/*==========================================================
 * 会话管理�?
 *
 * 【新增功能】相比server-9，ChatSession是全新增加的核心组件
 *
 * 功能�?
 * 1. 维护多轮对话的上下文历史
 * 2. 自动进行历史裁剪，控制prompt长度
 * 3. 按照ChatML格式构造模型输入prompt
 *
 * 设计理念�?
 * - 每个用户的每个会话对应一个ChatSession实例
 * - 自动管理上下文窗口，避免超出模型最大长度限�?
 * - 支持DeepSeek/ChatML等主流对话格�?
 *=========================================================*/
class ChatSession {
public:
    /**
     * @brief 向会话追加一条消�?
     *
     * 【核心功能】追加新消息并自动裁剪历史记�?
     *
     * @param role 消息角色（user/assistant/system�?
     * @param content 消息内容
     *
     * 流程�?
     * 1. 将新消息添加到历史队列末�?
     * 2. 调用prune()自动裁剪过长的历�?
     */
    void add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        prune();   // 限制上下文长度，避免 prompt 过长
    }

    /**
     * @brief 将当前历史序列化为ChatML格式的Prompt
     *
     * 【核心功能】构造符合大模型要求的输入格�?
     *
     * @return 完整的ChatML格式prompt字符�?
     *
     * ChatML格式示例�?
     * ```
     * <|user|>
     * 你好
     * <|assistant|>
     * 您好！有什么可以帮您的�?
     * <|user|>
     * 介绍一下北�?
     * <|assistant|>
     * ```
     *
     * 最后的<|assistant|>标记用于提示模型开始生成回�?
     */
    std::string makePrompt() const {
        std::ostringstream oss;
        // 对每条消息前后加上标记，ChatML 协议格式
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n"
                << m.content << "\n";
        }
        // 最后留给模型接着�?assistant 部分
        oss << "<|assistant|>\n";
        return oss.str();
    }

    /**
     * @brief 完全清空会话历史
     *
     * 用于重置会话或开始新的对话主�?
     */
    void reset() {
        history.clear();
    }

private:
    // 会话历史按时间先后顺序存�?
    std::vector<InferenceMessage> history;

    /**
     * @brief 裁剪历史记录，防止上下文过长
     *
     * 【核心算法】智能历史管理策�?
     *
     * 目的�?
     * - 控制prompt长度不超过模型上下文窗口
     * - 保留最近的对话内容，保证上下文连贯�?
     * - 通过摘要机制保留早期对话的概要信�?
     *
     * 策略�?
     * 1. 单条消息截断：每条消息最多保�?00字符
     * 2. 历史轮数限制：最多保�?轮完整对话（8条消息）
     * 3. 摘要机制：超出部分用system角色的摘要替�?
     *
     * 实现细节�?
     * - 每轮对话包含2条消息（1条user + 1条assistant�?
     * - 删除最早的2条消息时，会插入摘要提示
     * - 这确保模型始终知道有之前的对话历�?
     */
    void prune() {
        const int max_round_keep       = 4;   // 最多保留完整的 4 轮对�?
        const int max_tokens_per_msg   = 200; // 单条消息内容最大字符数（粗略估计）

        // 步骤1：逐条截断超长文本，避免单条消息撑�?prompt
        for (auto& m : history) {
            if ((int)m.content.size() > max_tokens_per_msg) {
                m.content.resize(max_tokens_per_msg);
            }
        }

        // 步骤2：超过总条数时，进�?摘要+删除最早两�?的简化处�?
        //       保证上下文窗口大小可控，同时保留一定的历史概览
        while ((int)history.size() > max_round_keep * 2) {
            // 简易实现：直接丢弃最早的一轮（两条消息user+assistant�?
            // 并在开头插入一�?system 角色的摘要提�?
            std::string summary = "[summary] previous conversation ...";
            history.erase(history.begin(), history.begin() + 2);
            history.insert(history.begin(), {"system", summary});
        }
    }
};

/*==========================================================
 * 模型管理器（单例模式�?
 *
 * 【核心新增模块】相比server-9，这是完全新增的AI推理引擎
 *
 * 职责�?
 * 1. 管理llama.cpp模型的加载和卸载
 * 2. 维护所有用户的多轮对话会话
 * 3. 提供线程安全的推理接�?
 * 4. 处理模型生成和停止条�?
 *
 * 设计模式�?
 * - 单例模式：全局共享一个模型实例，节省内存
 * - 会话隔离：每个chat_id对应独立的ChatSession
 * - 线程安全：使用互斥锁保护共享资源
 *
 * 技术栈�?
 * - llama.cpp：C++实现的LLM推理�?
 * - GGUF格式：支持量化模型，降低内存占用
 * - 多线程：支持并发推理请求
 *=========================================================*/
class ModelManager {
public:
    /**
     * @brief 获取ModelManager单例实例
     *
     * 【单例模式】确保全局只有一个模型实�?
     *
     * @return ModelManager引用
     *
     * 优点�?
     * - 避免重复加载模型，节省内存（模型通常几百MB到几GB�?
     * - 全局统一管理所有会�?
     * - 简化资源管�?
     */
    static ModelManager& instance();

    /**
     * @brief 加载GGUF格式的量化模�?
     *
     * 【核心功能】初始化LLM模型，加载到内存
     *
     * @param path 模型文件路径�?gguf格式�?
     * @param n_ctx 最大上下文长度（token数），默�?048
     * @param n_threads CPU推理线程数，默认4
     * @return 成功返回true，失败返回false
     *
     * 注意事项�?
     * - 加载过程耗时较长（几秒到几十秒）
     * - 需要足够内存（量化模型�?00MB-2GB�?
     * - 线程安全：内部使用互斥锁保护
     * - 支持环境变量MODEL_PATH配置路径
     */
    bool loadModel(const std::string& path,
                   int n_ctx = 2048,
                   int n_threads = 4);

    /**
     * @brief 多轮对话推理接口
     *
     * 【核心API】处理用户输入，生成AI回复
     *
     * @param chat_id 会话标识符，用于隔离不同用户/会话的上下文
     * @param user_msg 用户本轮输入的消�?
     * @param maxTokens 最大生成token数，默认64
     * @param temperature 采样温度�?-2），越高越随机，默认0.7
     * @return AI生成的回复文�?
     *
     * 工作流程�?
     * 1. 将user_msg添加到对应chat_id的会话历�?
     * 2. 构造ChatML格式的完整prompt
     * 3. 调用底层raw_infer进行推理
     * 4. 将AI回复添加到会话历�?
     * 5. 返回生成的文�?
     *
     * 会话隔离�?
     * - 不同chat_id的对话互不干�?
     * - 同一chat_id的对话会自动记住上下�?
     * - 支持字符串chat_id（如"user123-chat456"�?
     */
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens,
                      float temperature);

    /**
     * @brief 清除指定会话的历史记�?
     *
     * @param chat_id 要清除的会话标识�?
     *
     * 用途：
     * - 用户主动重置对话
     * - 释放不再使用的会话内�?
     * - 开始新的对话主�?
     */
    void dropSession(const std::string& chat_id);

    /**
     * @brief 底层单轮推理函数（Server-12新增：改为public�?
     *
     * 【公开API】供SessionManager使用，不维护内部历史
     *
     * @param prompt 完整的prompt（包含上下文�?
     * @param maxTokens 最大生成token�?
     * @param temperature 采样温度
     * @return 生成的文�?
     *
     * 实现细节�?
     * 1. 创建临时llama_context（每次推理独立）
     * 2. Tokenize输入prompt
     * 3. 逐token生成，使用贪婪采�?
     * 4. 遇到EOS或ChatML标记时停�?
     * 5. 释放临时context
     */
    std::string raw_infer(const std::string& prompt,
                          int maxTokens,
                          float temperature) const;

private:
    // 私有构�?析构函数：实现单例模�?
    ModelManager();
    ~ModelManager();
    // 禁用拷贝构造和赋值：防止创建多个实例
    ModelManager(const ModelManager&)            = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // ========== 线程同步 ==========
    std::mutex mtx_;         // 保护模型加载操作的互斥锁
    std::mutex chat_mutex_;  // 保护会话字典的互斥锁

    // ========== llama.cpp 核心资源 ==========
    /**
     * 模型句柄：指向加载的GGUF模型
     * - 由llama_load_model_from_file()创建
     * - 包含模型权重、词表等静态资�?
     * - 可被多个context共享
     */
    struct llama_model* model_   = nullptr;

    /**
     * 上下文句柄：推理时的工作空间（已废弃，改用每次创建临时context�?
     * - 包含KV缓存、计算图等动态资�?
     * - 单个context不支持并发，因此raw_infer中每次新�?
     */
    struct llama_context* ctx_   = nullptr;

    int n_ctx_     = 2048;   // 最大上下文长度（token数）
    int n_threads_ = 4;      // CPU推理线程�?

    // ========== 会话管理 ==========
    /**
     * 会话字典：存储所有活跃的聊天会话
     * - key: chat_id（字符串，如"user123-chat456"�?
     * - value: ChatSession对象（包含该会话的完整历史）
     * - 线程安全：由chat_mutex_保护
     *
     * 生命周期�?
     * - 首次访问某chat_id时自动创建ChatSession
     * - 调用dropSession()时删�?
     * - 服务器重启时所有会话丢失（未持久化�?
     */
    std::unordered_map<std::string, ChatSession> chat_sessions_;
};
