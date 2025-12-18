/*==========================================================
 * SessionManager - 会话管理类
 *
 * 功能：
 * 1. 创建和管理多个会话
 * 2. 存储每个会话的对话历史
 * 3. 提供会话ID生成
 * 4. 支持上下文拼接
 *
 * 存储方式：内存存储（重启后丢失）
 *=========================================================*/

#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>

/*==========================================================
 * Message - 单条消息结构
 *=========================================================*/
struct Message {
    std::string role;      // "user" or "assistant"
    std::string content;   // 消息内容

    Message(const std::string& r, const std::string& c)
        : role(r), content(c) {}
};

/*==========================================================
 * Session - 会话结构
 *=========================================================*/
struct Session {
    std::string session_id;
    std::vector<Message> history;  // 对话历史

    // 默认构造函数（unordered_map需要）
    Session() : session_id("") {}

    // 带参数的构造函数
    Session(const std::string& id) : session_id(id) {}

    // 添加消息到历史
    void addMessage(const std::string& role, const std::string& content) {
        history.emplace_back(role, content);
    }

    // 获取完整的上下文（拼接所有历史消息）
    std::string getFullContext() const {
        std::string context;
        for (const auto& msg : history) {
            if (msg.role == "user") {
                context += "User: " + msg.content + "\n";
            } else {
                context += "Assistant: " + msg.content + "\n";
            }
        }
        return context;
    }

    // 获取最近N轮对话的上下文
    std::string getRecentContext(int n_turns = 5) const {
        std::string context;
        int start_idx = std::max(0, (int)history.size() - n_turns * 2);

        for (size_t i = start_idx; i < history.size(); ++i) {
            if (history[i].role == "user") {
                context += "User: " + history[i].content + "\n";
            } else {
                context += "Assistant: " + history[i].content + "\n";
            }
        }
        return context;
    }
};

/*==========================================================
 * SessionManager - 会话管理器
 *=========================================================*/
class SessionManager {
public:
    SessionManager() : gen(rd()) {}

    /**
     * @brief 创建新会话
     * @return 新的session_id
     */
    std::string createSession() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string session_id = generateSessionId();
        sessions_[session_id] = Session(session_id);

        std::cout << "[SessionManager] Created session: " << session_id << "\n";
        return session_id;
    }

    /**
     * @brief 检查会话是否存在
     */
    bool hasSession(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.find(session_id) != sessions_.end();
    }

    /**
     * @brief 添加用户消息
     */
    void addUserMessage(const std::string& session_id, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            throw std::runtime_error("Session not found: " + session_id);
        }

        sessions_[session_id].addMessage("user", content);
        std::cout << "[SessionManager] Added user message to session " << session_id << "\n";
    }

    /**
     * @brief 添加助手消息
     */
    void addAssistantMessage(const std::string& session_id, const std::string& content) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            throw std::runtime_error("Session not found: " + session_id);
        }

        sessions_[session_id].addMessage("assistant", content);
        std::cout << "[SessionManager] Added assistant message to session " << session_id << "\n";
    }

    /**
     * @brief 获取完整上下文（所有历史消息）
     */
    std::string getFullContext(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            return "";
        }

        return sessions_[session_id].getFullContext();
    }

    /**
     * @brief 获取最近N轮对话的上下文
     * @param session_id 会话ID
     * @param n_turns 轮数（默认5轮，即10条消息）
     */
    std::string getRecentContext(const std::string& session_id, int n_turns = 5) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            return "";
        }

        return sessions_[session_id].getRecentContext(n_turns);
    }

    /**
     * @brief 获取会话的消息数量
     */
    int getMessageCount(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            return 0;
        }

        return sessions_[session_id].history.size();
    }

    /**
     * @brief 删除会话
     */
    void deleteSession(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        sessions_.erase(session_id);
        std::cout << "[SessionManager] Deleted session: " << session_id << "\n";
    }

    /**
     * @brief 清空所有会话
     */
    void clearAllSessions() {
        std::lock_guard<std::mutex> lock(mutex_);

        sessions_.clear();
        std::cout << "[SessionManager] Cleared all sessions\n";
    }

    /**
     * @brief 获取活跃会话数量
     */
    int getActiveSessionCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_.size();
    }

private:
    std::unordered_map<std::string, Session> sessions_;
    std::mutex mutex_;
    std::random_device rd;
    std::mt19937 gen;

    /**
     * @brief 生成唯一的session ID
     * 格式: sess_XXXXXXXX (8位十六进制随机数)
     */
    std::string generateSessionId() {
        std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        uint32_t rand_num = dis(gen);

        std::stringstream ss;
        ss << "sess_" << std::hex << std::setw(8) << std::setfill('0') << rand_num;
        return ss.str();
    }
};
