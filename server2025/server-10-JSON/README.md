# Server-10: JSON解析 + RESTful API

## 📋 本版本新增功能

相比**Server-9**，本版本添加了：

1. ✅ **JSON解析功能** - `parseJson()`方法
2. ✅ **RESTful风格的API** - `/api/users/*`路径
3. ✅ **查询参数支持** - `getQuery()`方法
4. ✅ **JSON格式响应** - 统一的JSON响应格式

---

## 🎯 学习目标

- 理解JSON数据格式
- 掌握RESTful API设计规范
- 学会解析URL查询参数
- 实现手写的简单JSON解析器

---

## 📁 文件清单

```
server-10-JSON/
├── HttpRequest.h       # ★ 新增parseJson()和getQuery()方法
├── Router.h            # ★ 新增setupRESTfulRoutes()方法
├── main.cpp            # ★ 调用RESTful API路由
├── HttpServer.h        # 继承自Server-9
├── HttpResponse.h      # 继承自Server-9
├── Database.h          # 继承自Server-9
├── Logger.h            # 继承自Server-9
├── ThreadPool.h        # 继承自Server-9
├── test_api.sh         # ★ 新增：API测试脚本
└── README.md           # 本文件
```

---

## 🚀 编译和运行

### 1. 编译
```bash
g++ main.cpp -o server10 -lsqlite3 -lpthread -std=c++11
```

### 2. 运行
```bash
./server10
# 或指定端口
./server10 8080
```

### 3. 测试
```bash
# 使用测试脚本
chmod +x test_api.sh
./test_api.sh
```

---

## 📡 API接口说明

### 新增：RESTful API（JSON格式）

#### 1. 用户注册
```bash
POST /api/users/register
Content-Type: application/json

{
    "username": "alice",
    "password": "password123"
}
```

**响应**:
```json
{
    "success": true,
    "message": "Registration successful"
}
```

---

#### 2. 用户登录
```bash
POST /api/users/login
Content-Type: application/json

{
    "username": "alice",
    "password": "password123"
}
```

**响应**:
```json
{
    "success": true,
    "message": "Login successful",
    "username": "alice"
}
```

---

#### 3. 获取用户信息（查询参数演示）
```bash
GET /api/users?id=123
```

**响应**:
```json
{
    "success": true,
    "userId": "123",
    "username": "user123"
}
```

---

#### 4. JSON回显测试
```bash
POST /api/echo
Content-Type: application/json

{
    "name": "Alice",
    "age": "25",
    "city": "Beijing"
}
```

**响应**:
```json
{
    "success": true,
    "received": {
        "name": "Alice",
        "age": "25",
        "city": "Beijing"
    }
}
```

---

### 兼容：传统接口（Form格式）

保留了Server-9的所有接口：
- `GET /` - 重定向到登录页
- `GET /login` - 登录页面
- `GET /register` - 注册页面
- `POST /register` - 表单注册
- `POST /login` - 表单登录

---

## 🧪 完整测试示例

### 测试1: JSON注册
```bash
curl -X POST http://localhost:8080/api/users/register \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

**预期输出**:
```json
{"success":true,"message":"Registration successful"}
```

---

### 测试2: JSON登录
```bash
curl -X POST http://localhost:8080/api/users/login \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

**预期输出**:
```json
{"success":true,"message":"Login successful","username":"alice"}
```

---

### 测试3: 查询参数
```bash
curl http://localhost:8080/api/users?id=456
```

**预期输出**:
```json
{"success":true,"userId":"456","username":"user456"}
```

---

### 测试4: JSON回显
```bash
curl -X POST http://localhost:8080/api/echo \
  -H "Content-Type: application/json" \
  -d '{"message":"Hello","from":"client"}'
```

**预期输出**:
```json
{"success":true,"received":{"message":"Hello","from":"client"}}
```

---

## 💡 核心代码解析

### 1. JSON解析实现（HttpRequest.h）

```cpp
std::unordered_map<std::string, std::string> parseJson() const {
    std::unordered_map<std::string, std::string> result;
    std::string json = body;

    // 去除首尾的大括号
    size_t start = json.find('{');
    size_t end = json.rfind('}');
    if (start == std::string::npos || end == std::string::npos) {
        return result;
    }
    json = json.substr(start + 1, end - start - 1);

    // 解析 "key":"value" 键值对
    size_t pos = 0;
    while (pos < json.length()) {
        // 查找键
        if (json[pos] != '\"') { pos++; continue; }
        size_t key_start = pos + 1;
        size_t key_end = json.find('\"', key_start);
        std::string key = json.substr(key_start, key_end - key_start);

        // 查找值
        pos = json.find(':', key_end) + 1;
        while (pos < json.length() && json[pos] == ' ') pos++;
        if (json[pos] != '\"') { pos++; continue; }
        size_t val_start = pos + 1;
        size_t val_end = json.find('\"', val_start);
        std::string value = json.substr(val_start, val_end - val_start);

        result[key] = value;
        pos = val_end + 1;
    }

    return result;
}
```

**支持的JSON格式**:
```json
{"key1":"value1", "key2":"value2", "key3":"value3"}
```

**限制**:
- 只支持字符串值（不支持数字、布尔值、嵌套对象）
- 不支持数组
- 简单实现，适合学习和小型项目

---

### 2. 查询参数解析（HttpRequest.h）

```cpp
std::unordered_map<std::string, std::string> getQuery() const {
    std::unordered_map<std::string, std::string> params;
    size_t pos = path.find('?');
    if (pos == std::string::npos) return params;

    std::string query = path.substr(pos + 1);
    std::istringstream stream(query);
    std::string pair;

    while (std::getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos == std::string::npos) continue;
        std::string key = pair.substr(0, eq_pos);
        std::string value = pair.substr(eq_pos + 1);
        params[key] = value;
    }

    return params;
}
```

**示例解析**:
```
/api/users?id=123&name=alice&age=25
↓
{
    "id": "123",
    "name": "alice",
    "age": "25"
}
```

---

### 3. RESTful API路由（Router.h）

```cpp
void setupRESTfulRoutes(Database& db) {
    // POST /api/users/register
    addRoute("POST", "/api/users/register", [&db](const HttpRequest& req) {
        auto params = req.parseJson();
        std::string username = params["username"];
        std::string password = params["password"];

        if (username.empty() || password.empty()) {
            HttpResponse resp;
            resp.setStatusCode(400);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":false,\"message\":\"Username and password required\"}");
            return resp;
        }

        bool success = db.registerUser(username, password);
        HttpResponse resp;
        resp.setHeader("Content-Type", "application/json");
        if (success) {
            resp.setStatusCode(200);
            resp.setBody("{\"success\":true,\"message\":\"Registration successful\"}");
        } else {
            resp.setStatusCode(400);
            resp.setBody("{\"success\":false,\"message\":\"User already exists\"}");
        }
        return resp;
    });

    // 其他API...
}
```

---

## 🔍 RESTful设计规范

### 资源命名
- ✅ 使用名词复数: `/api/users` 而不是 `/api/user`
- ✅ 层级结构: `/api/users/123/posts`
- ✅ 小写字母，用连字符分隔: `/api/user-profiles`

### HTTP方法语义
- `GET` - 获取资源
- `POST` - 创建资源
- `PUT` - 更新整个资源
- `PATCH` - 部分更新资源
- `DELETE` - 删除资源

### 状态码使用
- `200 OK` - 成功
- `201 Created` - 创建成功
- `400 Bad Request` - 客户端错误
- `401 Unauthorized` - 未授权
- `404 Not Found` - 资源不存在
- `500 Internal Server Error` - 服务器错误

---

## 📚 与Server-9的对比

| 特性 | Server-9 | Server-10 |
|------|----------|-----------|
| 请求格式 | Form-data | JSON + Form-data |
| 响应格式 | HTML | JSON + HTML |
| API风格 | 传统 | RESTful |
| 查询参数 | ❌ | ✅ |
| JSON解析 | ❌ | ✅ |
| API路径 | `/register` | `/api/users/register` |

---

## 🎓 学习检查清单

完成以下任务后，你已经掌握了本节内容：

- [ ] 理解JSON格式的基本结构
- [ ] 能够手写简单的JSON解析器
- [ ] 理解RESTful API的设计原则
- [ ] 掌握HTTP状态码的使用
- [ ] 能够解析URL查询参数
- [ ] 成功测试所有新增API接口
- [ ] 理解JSON与Form-data的区别

---

## 🐛 常见问题

### Q1: 为什么JSON解析器这么简单？
A: 这是教学版本，目的是理解JSON解析的基本原理。生产环境建议使用成熟的库如`nlohmann/json`或`rapidjson`。

### Q2: 支持嵌套JSON吗？
A: 当前版本不支持。只支持简单的`{"key":"value"}`格式。

### Q3: 如何测试POST请求？
A: 使用`curl`命令或Postman工具。推荐使用提供的`test_api.sh`脚本。

### Q4: RESTful API的优势是什么？
A:
- 统一的接口风格
- 更好的可维护性
- 易于理解和使用
- 前后端分离友好

---

## 🚀 下一步

学完Server-10后，继续学习：

**Server-11**: llama.cpp集成 + 单轮推理
- 引入AI模型
- 实现文本生成
- 学习llama.cpp库的基本使用

---

**编写日期**: 2025-12-10
**作者**: Claude Code
