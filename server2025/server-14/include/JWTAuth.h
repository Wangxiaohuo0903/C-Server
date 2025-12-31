#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <openssl/hmac.h>
#include <openssl/evp.h>

/**
 * @brief JWT 认证类 - 使用 HS256 算法（HMAC-SHA256）
 *
 * JWT 格式: header.payload.signature
 * - Header: {"alg":"HS256","typ":"JWT"}
 * - Payload: {"username":"alice","exp":1234567890}
 * - Signature: HMAC-SHA256(base64(header) + "." + base64(payload), secret_key)
 */
class JWTAuth {
public:
    /**
     * @brief 生成 JWT token（有效期 24 小时）
     * @param username 用户名
     * @return JWT token 字符串
     */
    static std::string generateToken(const std::string& username) {
        // 1. 创建 header (固定)
        std::string header = R"({"alg":"HS256","typ":"JWT"})";

        // 2. 创建 payload (包含用户名和过期时间)
        long long exp = getCurrentTimestamp() + 86400; // 24小时后过期
        std::ostringstream payload_stream;
        payload_stream << "{\"username\":\"" << username << "\",\"exp\":" << exp << "}";
        std::string payload = payload_stream.str();

        // 3. Base64 编码 header 和 payload
        std::string encoded_header = base64UrlEncode(header);
        std::string encoded_payload = base64UrlEncode(payload);

        // 4. 创建待签名数据
        std::string data = encoded_header + "." + encoded_payload;

        // 5. 使用 HMAC-SHA256 生成签名
        std::string signature = hmacSHA256(data, secret_key);
        std::string encoded_signature = base64UrlEncode(signature);

        // 6. 组合成完整的 JWT
        return data + "." + encoded_signature;
    }

    /**
     * @brief 验证 JWT token 并提取用户名
     * @param token JWT token 字符串
     * @return 用户名（验证失败或过期返回空字符串）
     */
    static std::string validateToken(const std::string& token) {
        // 1. 分割 token 为 header.payload.signature
        size_t first_dot = token.find('.');
        size_t second_dot = token.rfind('.');

        if (first_dot == std::string::npos || second_dot == std::string::npos || first_dot == second_dot) {
            return ""; // 格式错误
        }

        std::string encoded_header = token.substr(0, first_dot);
        std::string encoded_payload = token.substr(first_dot + 1, second_dot - first_dot - 1);
        std::string encoded_signature = token.substr(second_dot + 1);

        // 2. 验证签名
        std::string data = encoded_header + "." + encoded_payload;
        std::string expected_signature = hmacSHA256(data, secret_key);
        std::string expected_encoded_sig = base64UrlEncode(expected_signature);

        if (encoded_signature != expected_encoded_sig) {
            return ""; // 签名验证失败
        }

        // 3. 解码 payload 并提取用户名和过期时间
        std::string payload = base64UrlDecode(encoded_payload);

        // 简单的 JSON 解析（提取 username 和 exp）
        std::string username = extractJsonValue(payload, "username");
        std::string exp_str = extractJsonValue(payload, "exp");

        if (username.empty() || exp_str.empty()) {
            return ""; // 格式错误
        }

        // 4. 检查是否过期
        long long exp = std::stoll(exp_str);
        long long now = getCurrentTimestamp();

        if (now > exp) {
            return ""; // Token 已过期
        }

        return username;
    }

    /**
     * @brief 设置密钥（服务器启动时调用）
     * @param key 密钥字符串
     */
    static void setSecretKey(const std::string& key) {
        secret_key = key;
    }

private:
    static std::string secret_key;

    /**
     * @brief 获取当前时间戳（秒）
     */
    static long long getCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    /**
     * @brief Base64 URL 安全编码（JWT 标准）
     * 将 + 替换为 -, / 替换为 _, 移除尾部的 =
     */
    static std::string base64UrlEncode(const std::string& input) {
        std::string b64 = base64Encode(input);

        // 替换字符以符合 Base64 URL 安全标准
        for (char& c : b64) {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }

        // 移除尾部的 =
        size_t pos = b64.find('=');
        if (pos != std::string::npos) {
            b64 = b64.substr(0, pos);
        }

        return b64;
    }

    /**
     * @brief Base64 URL 安全解码
     */
    static std::string base64UrlDecode(const std::string& input) {
        std::string b64 = input;

        // 还原标准 Base64 字符
        for (char& c : b64) {
            if (c == '-') c = '+';
            else if (c == '_') c = '/';
        }

        // 补全 padding
        while (b64.length() % 4 != 0) {
            b64 += '=';
        }

        return base64Decode(b64);
    }

    /**
     * @brief 标准 Base64 编码
     */
    static std::string base64Encode(const std::string& input) {
        static const char* base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        int val = 0;
        int valb = -6;

        for (unsigned char c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6) {
            encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        }

        while (encoded.size() % 4) {
            encoded.push_back('=');
        }

        return encoded;
    }

    /**
     * @brief 标准 Base64 解码
     */
    static std::string base64Decode(const std::string& input) {
        static const unsigned char base64_table[256] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
        };

        std::string decoded;
        int val = 0;
        int valb = -8;

        for (unsigned char c : input) {
            if (base64_table[c] == 64) break;
            val = (val << 6) + base64_table[c];
            valb += 6;
            if (valb >= 0) {
                decoded.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }

        return decoded;
    }

    /**
     * @brief 使用 OpenSSL 的 HMAC-SHA256 生成签名
     */
    static std::string hmacSHA256(const std::string& data, const std::string& key) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;

        HMAC(EVP_sha256(),
             key.c_str(), key.length(),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
             hash, &hash_len);

        return std::string(reinterpret_cast<char*>(hash), hash_len);
    }

    /**
     * @brief 从简单的 JSON 字符串中提取值
     * 例如: {"username":"alice","exp":123} -> extractJsonValue(..., "username") -> "alice"
     */
    static std::string extractJsonValue(const std::string& json, const std::string& key) {
        std::string search_key = "\"" + key + "\":";
        size_t pos = json.find(search_key);

        if (pos == std::string::npos) {
            return "";
        }

        pos += search_key.length();

        // 跳过空格
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
            pos++;
        }

        if (pos >= json.length()) {
            return "";
        }

        // 检查是字符串还是数字
        if (json[pos] == '"') {
            // 字符串值
            pos++; // 跳过开始的引号
            size_t end_pos = json.find('"', pos);
            if (end_pos == std::string::npos) {
                return "";
            }
            return json.substr(pos, end_pos - pos);
        } else {
            // 数字值
            size_t end_pos = pos;
            while (end_pos < json.length() &&
                   (std::isdigit(json[end_pos]) || json[end_pos] == '.' || json[end_pos] == '-')) {
                end_pos++;
            }
            return json.substr(pos, end_pos - pos);
        }
    }
};

// 静态成员初始化
std::string JWTAuth::secret_key = "";
