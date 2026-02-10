#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <queue>
#include <future>
#include <thread>
#include <condition_variable>
#include <atomic>

/*==========================================================
 * 对话消息结构定义
 *=========================================================*/
struct InferenceMessage {
    std::string role;
    std::string content;
};

/*==========================================================
 * 会话管理类
 *=========================================================*/
class ChatSession {
public:
    bool add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        return prune();
    }

    std::string makePrompt() const {
        std::ostringstream oss;
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n" << m.content << "\n";
        }
        oss << "<|assistant|>\n";
        return oss.str();
    }

    void reset() { history.clear(); }

private:
    std::vector<InferenceMessage> history;

    bool prune() {
        const int max_round_keep = 4;
        const int max_tokens_per_msg = 200;
        bool modified = false;

        for (auto& m : history) {
            if ((int)m.content.size() > max_tokens_per_msg) {
                m.content.resize(max_tokens_per_msg);
                modified = true;
            }
        }

        while ((int)history.size() > max_round_keep * 2) {
            std::string summary = "[summary] previous conversation ...";
            history.erase(history.begin(), history.begin() + 2);
            history.insert(history.begin(), {"system", summary});
            modified = true;
        }
        return modified;
    }
};

/*==========================================================
 * 批处理请求结构 (Server-14-1-Batch 新增)
 *=========================================================*/
struct InferenceRequest {
    std::string chat_id;
    std::string prompt;
    int max_tokens;
    float temperature;
    std::promise<std::string> result_promise; // 用于返回结果
    long long timestamp; // 入队时间
};

/*==========================================================
 * 模型管理器（支持批处理）
 *=========================================================*/
class ModelManager {
public:
    static ModelManager& instance();

    // 加载模型 (新增 batch_size 参数)
    bool loadModel(const std::string& path,
                   int n_ctx = 2048,
                   int n_threads = 4,
                   int batch_size = 8); // 最大并发 batch 数

    // 提交推理请求 (异步等待结果)
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens,
                      float temperature);

    // 兼容 Router.h 的接口 (Server-12 legacy)
    std::string raw_infer(const std::string& prompt,
                          int maxTokens,
                          float temperature) const;

    void dropSession(const std::string& chat_id);

private:
    ModelManager();
    ~ModelManager();
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 内部批处理循环
    void startBatchLoop();
    void stopBatchLoop();
    void batchLoop();

    // 资源
    std::mutex mtx_;
    std::mutex chat_mutex_;
    struct llama_model* model_ = nullptr;
    struct llama_context* ctx_ = nullptr; // 全局共享 context

    int n_ctx_ = 2048;
    int n_threads_ = 4;
    int max_batch_size_ = 8; // 最大并发数

    // 会话历史 (KV Cache 由 batch loop 的 slot 管理)
    std::unordered_map<std::string, ChatSession> chat_sessions_;

    // 批处理队列
    std::queue<std::shared_ptr<InferenceRequest>> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 线程控制
    std::thread batch_thread_;
    std::atomic<bool> running_{false};
    
    // KV Cache 状态跟踪 (Slot 管理)
    struct Slot {
        int id;
        bool active = false;
        std::string chat_id;
        std::string generated_text;
        int tokens_generated = 0;
        int max_tokens = 0;
        float temperature = 0.7f;
        std::shared_ptr<InferenceRequest> current_req;
        
        // KV Cache 状态
        int n_past = 0; // 该 slot 已处理的 token 数
        int last_token = -1; // 上一轮生成的 token (用于 decode 输入)
        // 简单的缓存复用检查 (可选)
        // std::vector<int> cached_tokens; 
    };
    
    std::vector<Slot> slots_;
};
