#pragma once  // 避免头文件重复包含

#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>

/*==========================================================
 * Minimal HTTP request parser (only what our server needs)
 * 这是一个极简的 HTTP 请求解析器，只实现了服务器需要的功能
 *=========================================================*/
class HttpRequest {
public:
    // 支持的 HTTP 方法枚举
    enum Method { GET, POST, HEAD, PUT, DELETE_, TRACE, OPTIONS,
                  CONNECT, PATCH, UNKNOWN };

    // HTTP 报文的解析状态机
    enum ParseState { REQUEST_LINE, HEADERS, BODY, FINISH };

    HttpRequest() : method(UNKNOWN),
                    state(REQUEST_LINE),
                    contentLength(0) {}

    /* -----------------------------------------------------
     *  parse(rawHttp) — 解析整个 HTTP 报文
     * ----------------------------------------------------*/
    bool parse(const std::string& raw);

    /* ---------- helpers ---------- */

    // 解析 application/x-www-form-urlencoded 表单
    std::unordered_map<std::string,std::string> parseFormBody() const;

    // 从 body 中提取指定 key 的 JSON 字符串字段
    std::string parseJsonField(const std::string& key) const;

    // 简单 JSON 解析器，解析为 key-value 字典（无嵌套支持）
    std::unordered_map<std::string,std::string> parseJson() const {
        std::unordered_map<std::string,std::string> mp;
        size_t i = body.find('{');  // 找到 JSON 起始位置
        if (i == std::string::npos) return mp;
        ++i;
        while (i < body.size()) {
            i = body.find('"', i);                 if (i == std::string::npos) break;
            size_t k2 = body.find('"', i+1);       if (k2==std::string::npos)  break;
            std::string key = body.substr(i+1, k2-i-1);

            i = body.find(':', k2);                if (i == std::string::npos) break;
            ++i;
            while(i<body.size() && isspace(body[i])) ++i;

            std::string val;
            if (body[i] == '"') {  // 处理字符串值
                size_t v2 = body.find('"', i+1); if (v2==std::string::npos) break;
                val = body.substr(i+1, v2-i-1);
                i = v2+1;
            } else {  // 处理数字、布尔、null
                size_t v2 = i;
                while(v2<body.size() && body[v2]!=',' && body[v2]!='}') ++v2;
                val = body.substr(i, v2-i);
                val.erase(remove_if(val.begin(),val.end(),::isspace),val.end());
                i = v2;
            }
            mp[key]=val;

            i = body.find(',', i);
            if (i == std::string::npos) break;
            ++i;
        }
        return mp;
    }

    // 方法、路径、版本号与正文的访问器
    std::string        getMethodString() const;
    const std::string& getPath()   const { return path; }
    const std::string& getVersion()const { return version; }
    const std::string& getBody()   const { return body; }

    // 查询参数（例如 GET /url?a=1&b=2）
    const std::unordered_map<std::string,std::string>& getQuery() const {
        return queryMap;
    }

    // 处理形如 /chat/123 的路径，提取类似 ID 的字段
    std::string getPathParam(const std::string& key) const;

private:
    // 解析请求行和头部
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);

    // 状态字段
    Method      method;
    ParseState  state;
    size_t      contentLength;

    // 请求行解析结果
    std::string path;
    std::string version;
    std::unordered_map<std::string,std::string> headers;
    std::string body;

    // 查询字符串
    std::string queryRaw;
    std::unordered_map<std::string,std::string> queryMap;
};

/*==================== 实现部分 ====================*/

inline bool HttpRequest::parse(const std::string& raw) {
    std::istringstream iss(raw);
    std::string line;

    // 解析请求行和头部
    while (state == REQUEST_LINE || state == HEADERS) {
        if (!std::getline(iss, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // 去掉 '\r'

        if (state == REQUEST_LINE) {
            if (!parseRequestLine(line)) return false;
            state = HEADERS;
        } else {
            if (line.empty()) {  // 遇到空行，头部结束
                state = (method == POST && contentLength) ? BODY : FINISH;
                break;
            }
            if (!parseHeader(line)) return false;
        }
    }

    // 若有 body，截取它
    if (state == BODY) {
        size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return false;
        size_t bodyStart = headerEnd + 4;
        if (raw.size() < bodyStart + contentLength) return false;
        body = raw.substr(bodyStart, contentLength);
        state = FINISH;
    }
    return state == FINISH;
}

/* ---------- 表单解析 ---------- */
inline std::unordered_map<std::string,std::string>
HttpRequest::parseFormBody() const {
    std::unordered_map<std::string,std::string> mp;
    if (method != POST || body.empty()) return mp;
    std::istringstream ss(body);
    std::string kv;
    while (std::getline(ss, kv, '&')) {
        auto p = kv.find('=');
        if (p == std::string::npos) continue;
        mp[kv.substr(0,p)] = kv.substr(p+1);
    }
    return mp;
}

/* ---------- 解析 JSON 中字段 ---------- */
inline std::string HttpRequest::parseJsonField(const std::string& key) const {
    auto pos = body.find("\""+key+"\"");
    if (pos == std::string::npos) return "";
    pos = body.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = body.find('"', pos);
    if (pos == std::string::npos) return "";
    auto end = body.find('"', pos+1);
    if (end == std::string::npos) return "";
    return body.substr(pos+1, end-pos-1);
}

/* ---------- 处理路径参数 ---------- */
inline std::string HttpRequest::getPathParam(const std::string& key) const {
    if (key != "id") return "";
    std::string clean = path.substr(0, path.find('?'));
    size_t start = 0;
    while (start < clean.size()) {
        size_t slash = clean.find('/', start);
        if (slash == std::string::npos) slash = clean.size();
        std::string seg = clean.substr(start, slash-start);
        if (!seg.empty() && std::all_of(seg.begin(), seg.end(), ::isdigit))
            return seg;
        start = slash+1;
    }
    return "";
}

/* ---------- 请求行解析 ---------- */
inline bool HttpRequest::parseRequestLine(const std::string& l){
    std::istringstream iss(l);
    std::string m, rawPath;
    if (!(iss >> m >> rawPath >> version)) return false;

    // 方法转换
    if      (m=="GET")  method=GET;
    else if (m=="POST") method=POST;
    else if (m=="HEAD") method=HEAD;
    else if (m=="PUT")  method=PUT;
    else if (m=="DELETE") method=DELETE_;
    else if (m=="TRACE")  method=TRACE;
    else if (m=="OPTIONS")method=OPTIONS;
    else if (m=="CONNECT")method=CONNECT;
    else if (m=="PATCH")  method=PATCH;
    else method=UNKNOWN;

    // 分离路径与查询字符串
    size_t q = rawPath.find('?');
    if (q==std::string::npos) { path=rawPath; return true; }

    path     = rawPath.substr(0,q);
    queryRaw = rawPath.substr(q+1);
    std::istringstream qs(queryRaw);
    std::string kv;
    while (std::getline(qs, kv, '&')) {
        auto p = kv.find('=');
        if (p==std::string::npos) continue;
        queryMap[kv.substr(0,p)] = kv.substr(p+1);
    }
    return true;
}

/* ---------- 头部解析 ---------- */
inline bool HttpRequest::parseHeader(const std::string& l){
    auto p = l.find(':');
    if (p == std::string::npos) return false;
    std::string k = l.substr(0, p);
    std::string v = l.substr(p+1);
    if (!v.empty() && v.front() == ' ') v.erase(0, 1);
    headers[k] = v;

    // 若是 Content-Length，记录其值
    if (k == "Content-Length") {
        try { contentLength = std::stoul(v); } catch (...) { contentLength = 0; }
    }
    return true;
}

/* ---------- 方法转字符串 ---------- */
inline std::string HttpRequest::getMethodString() const {
    static const char* names[] = {
        "GET","POST","HEAD","PUT","DELETE",
        "TRACE","OPTIONS","CONNECT","PATCH","UNKNOWN"};
    return names[method];
}
