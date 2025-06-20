#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sstream>

// ------------------ 对话消息结构 ------------------
// 每条消息包含角色（user/assistant/system）和内容
struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;  // 消息文本
};

// ------------------ 会话管理 ------------------
class ChatSession {
public:
    // 向会话追加一条消息，并在必要时自动裁剪历史
    void add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        prune();   // 限制上下文长度，避免 prompt 过长
    }

    // 将当前历史序列化为 DeepSeek/ChatML 格式 Prompt
    std::string makePrompt() const {
        std::ostringstream oss;
        // 对每条消息前后加上标记，ChatML 协议格式
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n"
                << m.content << "\n";
        }
        // 最后留给模型接着写 assistant 部分
        oss << "<|assistant|>\n";
        return oss.str();
    }

    // 完全清空历史（例如 reset 会话时调用）
    void reset() {
        history.clear();
    }

private:
    // 会话历史按时间先后顺序存储
    std::vector<Message> history;

    // ============ 内部：裁剪历史 ============
    // 当历史过长时，保留最后几轮；最旧的合并成“system”摘要
    void prune() {
        const int max_round_keep       = 4;   // 最多保留完整的 4 轮
        const int max_tokens_per_msg   = 200; // 单条消息内容最大字符数

        // 1) 逐条截断超长文本，避免单条消息撑爆 prompt
        for (auto& m : history) {
            if ((int)m.content.size() > max_tokens_per_msg) {
                m.content.resize(max_tokens_per_msg);
            }
        }

        // 2) 超过总条数时，进行“摘要+删除最早两条”的简化处理
        //    保证上下文窗口大小可控，同时保留一定的历史概览
        while ((int)history.size() > max_round_keep * 2) {
            // 这里简易实现：直接丢弃最早的一轮（两条消息），
            // 并在开头插入一条 system 角色的摘要提示
            std::string summary = "[summary] previous conversation ...";
            history.erase(history.begin(), history.begin() + 2);
            history.insert(history.begin(), {"system", summary});
        }
    }
};

class ModelManager {
public:
    // 单例接口：全局共享一个 ModelManager 实例
    static ModelManager& instance();

    // 加载或重载模型（线程安全）
    // path: 模型文件路径
    // n_ctx: 最大上下文长度；n_threads: 并行线程数
    bool loadModel(const std::string& path,
                   int n_ctx = 2048,
                   int n_threads = 4);

    // 多轮对话接口
    // chat_id: 会话标识，用于索引 ChatSession
    // user_msg: 本轮用户输入
    // maxTokens, temperature: 解码参数
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens,
                      float temperature);

    // 清理指定会话历史
    void dropSession(const std::string& chat_id);

private:
    ModelManager();
    ~ModelManager();
    ModelManager(const ModelManager&)            = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 保护 loadModel / infer 接口的互斥锁
    std::mutex mtx_;

    // 底层 llama.cpp 模型句柄
    struct llama_model* model_   = nullptr;
    struct llama_context* ctx_   = nullptr;
    int n_ctx_     = 2048;
    int n_threads_ = 4;

    // 保存所有活跃的 ChatSession，key 为 chat_id
    std::unordered_map<std::string, ChatSession> chat_sessions_;
    std::mutex chat_mutex_;

    // **内部**单轮推理：不使用历史，只按给定 prompt 一次性生成
    std::string raw_infer(const std::string& prompt,
                          int maxTokens,
                          float temperature) const;
};
