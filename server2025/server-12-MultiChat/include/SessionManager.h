/*==========================================================
 * SessionManager - 会话管理类 (Server-12)
 *
 * 功能：
 * 1. 创建和管理多个AI对话会话
 * 2. 存储每个会话的对话历史
 * 3. 提供会话ID生成和管理
 * 4. 支持上下文拼接（记住历史对话）
 * 5. 支持会话列表和删除操作
 *
 * 使用场景：
 * - 多用户同时使用AI聊天
 * - 类似ChatGPT的多轮对话体验
 * - 支持"新增对话"功能
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
#include <iostream>
#include <chrono>

/*==========================================================
 * Message - 单条消息结构
 *=========================================================*/
struct Message {
    std::string role;       // "user" or "assistant"
    std::string content;    // 消息内容
    long long timestamp;    // 时间戳（毫秒）

    Message(const std::string& r, const std::string& c)
        : role(r), content(c) {
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
};

/*==========================================================
 * Session - 会话结构
 *=========================================================*/
struct Session {
    std::string session_id;
    std::vector<Message> history;      // 对话历史
    long long created_at;              // 创建时间戳
    long long last_active;             // 最后活跃时间
    std::string title;                 // 会话标题（从第一条用户消息生成）

    // 默认构造函数（unordered_map需要）
    Session() : session_id(""), created_at(0), last_active(0), title("New Chat") {}

    // 带参数的构造函数
    Session(const std::string& id) : session_id(id), title("New Chat") {
        created_at = last_active = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // 添加消息到历史
    void addMessage(const std::string& role, const std::string& content) {
        history.emplace_back(role, content);
        updateLastActive();

        // 自动设置标题（使用第一条用户消息的前30个字符）
        if (title == "New Chat" && role == "user" && !content.empty()) {
            title = content.substr(0, std::min((size_t)30, content.length()));
            if (content.length() > 30) {
                title += "...";
            }
        }
    }

    // 更新最后活跃时间
    void updateLastActive() {
        last_active = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    // 获取完整的上下文（拼接所有历史消息）
    // 格式：ChatGPT风格的对话格式
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
    // n_turns: 轮数（1轮 = 1个用户消息 + 1个助手回复）
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

    // 获取消息数量
    int getMessageCount() const {
        return history.size();
    }

    // 获取对话轮数（用户消息数量）
    int getTurnCount() const {
        int count = 0;
        for (const auto& msg : history) {
            if (msg.role == "user") count++;
        }
        return count;
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
     * @param n_turns 轮数（默认5轮，即5个用户消息+5个助手回复）
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
     * @brief 获取会话信息（用于前端显示）
     * @return JSON格式字符串：{"session_id":"xxx", "title":"xxx", "message_count":10, "last_active":123456789}
     */
    std::string getSessionInfo(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            return "{}";
        }

        const Session& sess = sessions_[session_id];
        std::ostringstream json;
        json << "{"
             << "\"session_id\":\"" << sess.session_id << "\","
             << "\"title\":\"" << escapeJson(sess.title) << "\","
             << "\"message_count\":" << sess.getMessageCount() << ","
             << "\"turn_count\":" << sess.getTurnCount() << ","
             << "\"created_at\":" << sess.created_at << ","
             << "\"last_active\":" << sess.last_active
             << "}";

        return json.str();
    }

    /**
     * @brief 获取所有会话列表（用于侧边栏显示）
     * @return JSON数组：[{session_id, title, message_count, last_active}, ...]
     */
    std::string getAllSessions() {
        std::lock_guard<std::mutex> lock(mutex_);

        // 按最后活跃时间排序
        std::vector<std::string> session_ids;
        for (const auto& pair : sessions_) {
            session_ids.push_back(pair.first);
        }

        std::sort(session_ids.begin(), session_ids.end(),
            [this](const std::string& a, const std::string& b) {
                return sessions_[a].last_active > sessions_[b].last_active;
            });

        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < session_ids.size(); ++i) {
            const Session& sess = sessions_[session_ids[i]];
            if (i > 0) json << ",";
            json << "{"
                 << "\"session_id\":\"" << sess.session_id << "\","
                 << "\"title\":\"" << escapeJson(sess.title) << "\","
                 << "\"message_count\":" << sess.getMessageCount() << ","
                 << "\"turn_count\":" << sess.getTurnCount() << ","
                 << "\"last_active\":" << sess.last_active
                 << "}";
        }
        json << "]";

        return json.str();
    }

    /**
     * @brief 获取会话历史记录（用于前端显示）
     * @return JSON数组：[{role:"user", content:"...", timestamp:123}, ...]
     */
    std::string getSessionHistory(const std::string& session_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            return "[]";
        }

        const Session& sess = sessions_[session_id];
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < sess.history.size(); ++i) {
            if (i > 0) json << ",";
            json << "{"
                 << "\"role\":\"" << sess.history[i].role << "\","
                 << "\"content\":\"" << escapeJson(sess.history[i].content) << "\","
                 << "\"timestamp\":" << sess.history[i].timestamp
                 << "}";
        }
        json << "]";

        return json.str();
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

    /**
     * @brief 更新会话标题（用户可手动修改）
     */
    void updateSessionTitle(const std::string& session_id, const std::string& new_title) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (sessions_.find(session_id) == sessions_.end()) {
            throw std::runtime_error("Session not found: " + session_id);
        }

        sessions_[session_id].title = new_title;
        sessions_[session_id].updateLastActive();
        std::cout << "[SessionManager] Updated title for session " << session_id << ": " << new_title << "\n";
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

    /**
     * @brief JSON字符串转义
     */
    std::string escapeJson(const std::string& str) const {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c;
            }
        }
        return result;
    }
};
