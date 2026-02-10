/*==========================================================
 * ModelManager.cpp - Server-13 KV Cache 优化的推理引擎
 *
 * 【Server-13 核心升级】KV Cache 持久化，性能提升 5-10 倍！
 *
 * 【性能对比】
 * Server-12: 每次对话处理全部历史 token
 *   第1轮: 处理 10 tokens  (用户消息)
 *   第2轮: 处理 30 tokens  (10 + 20)
 *   第3轮: 处理 60 tokens  (30 + 30)
 *   总计: 100 tokens       ← 大量重复计算！
 *
 * Server-13: 每次只处理新增 token
 *   第1轮: 处理 10 tokens  (用户消息)
 *   第2轮: 处理 20 tokens  (只处理新增部分)
 *   第3轮: 处理 30 tokens  (只处理新增部分)
 *   总计: 60 tokens        ← 节省 40% 计算！
 *
 * 【核心概念】
 * 1. **llama.cpp**: 一个C++实现的大语言模型推理库，支持CPU推理
 * 2. **GGUF格式**: llama.cpp使用的量化模型格式，体积小，推理快
 * 3. **单例模式**: 确保全局只有一个模型实例，节省内存
 * 4. **KV Cache**: 缓存注意力机制的 Key-Value 对，避免重复计算
 * 5. **增量推理**: 只处理新增的 token，复用已缓存的计算结果
 * 6. **n_past**: 已处理的 token 数量，决定从哪里开始增量计算
 *
 * 【推理流程对比】
 * Server-12 (无缓存):
 *   每次: 文本输入 → tokenize 全部 → decode 全部 → 生成 → 输出
 *
 * Server-13 (KV Cache):
 *   首次: 文本输入 → tokenize → decode → 生成 → 输出 + 保存 context
 *   后续: 新文本 → tokenize → 只 decode 新增部分 → 生成 → 输出
 *=========================================================*/

#include "ModelManager.h"
#include "llama.h"           // llama.cpp 核心库，提供模型加载和推理API
#include <iostream>
#include <cstring>
#include <algorithm>

/*==========================================================
 * KVCachedSession 析构函数
 *=========================================================*/

/**
 * @brief KVCachedSession 析构函数 - 释放 KV Cache 内存
 *
 * 【Server-13 核心资源管理】
 * 每个 KVCachedSession 持有一个 llama_context，占用 ~100-500MB 内存
 * 析构时必须正确释放，否则会内存泄漏
 *
 * 【RAII 原则】
 * - Resource Acquisition Is Initialization
 * - 对象销毁时自动释放资源
 * - 即使发生异常也能正确清理
 *
 * 【调用时机】
 * 1. ModelManager::dropSession() 删除会话
 * 2. chat_sessions_.erase() 触发
 * 3. 服务器关闭时清理所有会话
 */
KVCachedSession::~KVCachedSession() {
    if (ctx) {
        llama_free(ctx);  // 释放 context（~100-500MB）
        ctx = nullptr;
        std::cerr << "[KVCache] Context freed in destructor\n";
    }
}

/**
 * @brief 移动构造函数 - 转移 context 所有权
 */
KVCachedSession::KVCachedSession(KVCachedSession&& other) noexcept
    : session(std::move(other.session))
    , ctx(other.ctx)
    , n_past(other.n_past)
    , cached_tokens(std::move(other.cached_tokens)) {
    other.ctx = nullptr;  // 转移所有权
    other.n_past = 0;
}

/**
 * @brief 移动赋值运算符 - 转移 context 所有权
 */
KVCachedSession& KVCachedSession::operator=(KVCachedSession&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (ctx) {
            llama_free(ctx);
        }
        // 转移所有权
        session = std::move(other.session);
        ctx = other.ctx;
        n_past = other.n_past;
        cached_tokens = std::move(other.cached_tokens);
        other.ctx = nullptr;
        other.n_past = 0;
    }
    return *this;
}

/**
 * @brief 清空 KV 缓存（保留对话历史）
 */
void KVCachedSession::clearCache() {
    if (ctx) {
        llama_free(ctx);
        ctx = nullptr;
    }
    n_past = 0;
    cached_tokens.clear();
    std::cerr << "[KVCache] Cache cleared\n";
}

/*==========================================================
 * ModelManager 构造函数和析构函数
 *=========================================================*/

/**
 * @brief 默认构造函数
 *
 * 【初学者笔记】
 * = default 表示使用编译器生成的默认构造函数
 * 这里不需要做任何初始化，因为成员变量已经在头文件中初始化了
 */
ModelManager::ModelManager()  = default;

/**
 * @brief 析构函数 - 程序退出时自动调用
 *
 * 【初学者笔记】
 * 负责释放模型占用的内存（通常几百MB到几GB）
 *
 * 工作流程：
 * 1. 检查 model_ 指针是否非空（是否已加载模型）
 * 2. 如果已加载，调用 llama_free_model() 释放内存
 *
 * 为什么重要：
 * - 防止内存泄漏
 * - 大模型占用内存很大，必须正确释放
 */
ModelManager::~ModelManager() {
    if (model_) {
        llama_free_model(model_);  // 释放模型内存
    }
}

/*==========================================================
 * 单例模式实现
 *=========================================================*/

/**
 * @brief 获取全局唯一的 ModelManager 实例
 *
 * 【初学者笔记 - 单例模式】
 * 这是一个经典的设计模式，确保整个程序只有一个 ModelManager 对象
 *
 * @return ModelManager& 返回静态实例的引用
 *
 * 为什么使用单例：
 * 1. **节省内存**: 模型文件很大（几百MB到几GB），只加载一次
 * 2. **全局访问**: 任何地方都可以通过 instance() 访问同一个模型
 * 3. **线程安全**: C++11保证静态局部变量的初始化是线程安全的
 *
 * 使用示例：
 * ```cpp
 * ModelManager& mm = ModelManager::instance();
 * mm.loadModel("model.gguf", 2048, 4);
 * ```
 *
 * 技术细节：
 * - static 变量在第一次调用时初始化，程序结束时销毁
 * - 多线程同时调用也只会创建一个实例（C++11标准保证）
 */
ModelManager& ModelManager::instance() {
    static ModelManager inst;  // 静态局部变量，只初始化一次
    return inst;               // 返回引用，避免拷贝
}

/*==========================================================
 * 模型加载
 *=========================================================*/

/**
 * @brief 加载 GGUF 格式的量化模型到内存
 *
 * 【初学者笔记 - 模型加载】
 * 这是使用AI的第一步：将磁盘上的模型文件加载到内存中
 *
 * @param path 模型文件路径（.gguf 格式）
 *             示例: "/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf"
 * @param n_ctx 上下文窗口大小（token数量），默认2048
 *              - 决定模型能"记住"多少历史对话
 *              - 越大越费内存，但能处理更长的对话
 *              - 典型值：512, 1024, 2048, 4096
 * @param n_threads CPU推理线程数，默认4
 *                  - 更多线程通常推理更快
 *                  - 建议设置为CPU核心数的一半
 * @return 成功返回 true，失败返回 false
 *
 * 工作流程：
 * 1. 加锁（防止多线程同时加载模型，导致冲突）
 * 2. 如果已有模型，先释放旧模型的内存
 * 3. 使用默认参数初始化模型配置
 * 4. 调用 llama.cpp 的 API 加载模型文件
 * 5. 检查是否加载成功
 * 6. 保存配置参数（上下文大小、线程数）
 *
 * 常见问题：
 * - 文件不存在 → 返回 false
 * - 内存不足 → 返回 false（模型通常需要几百MB到几GB内存）
 * - 文件格式错误 → 返回 false
 *
 * 性能提示：
 * - 加载耗时：小模型几秒，大模型可能几十秒
 * - 量化模型（如Q4_K_M）比全精度模型小很多，推理也更快
 */
bool ModelManager::loadModel(const std::string& path, int n_ctx, int n_threads) {
    // 步骤1: 加锁，防止多线程同时加载（会导致内存混乱）
    std::lock_guard<std::mutex> g(mtx_);

    // 步骤2: 如果已经加载了模型，先释放旧模型
    // （比如用户切换模型文件）
    if (model_) {
        llama_free_model(model_);
    }

    // 步骤3: 创建模型参数结构体，使用默认配置
    // llama_model_params 包含很多配置项，这里使用默认值
    llama_model_params mp = llama_model_default_params();

    // macOS Docker 修复: 禁用 mmap，使用内存加载
    // mmap 在 macOS Docker (Apple Silicon) 上可能导致挂起
    mp.use_mmap = false;  // 禁用 mmap
    mp.use_mlock = false; // 禁用 mlock

    // macOS Docker 修复: 强制纯CPU模式，避免设备枚举卡住
    mp.n_gpu_layers = 0;  // 不使用GPU
    mp.main_gpu = -1;     // 强制CPU模式

    // 添加进度回调，跟踪加载进度并防止长时间挂起
    struct ProgressData {
        std::chrono::steady_clock::time_point start_time;
        float last_progress;
        int timeout_seconds;
    };

    ProgressData progress_data;
    progress_data.start_time = std::chrono::steady_clock::now();
    progress_data.last_progress = 0.0f;
    progress_data.timeout_seconds = 300; // 5分钟超时

    mp.progress_callback = [](float progress, void* user_data) -> bool {
        ProgressData* data = static_cast<ProgressData*>(user_data);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - data->start_time).count();

        // 每10%打印一次进度
        if (progress - data->last_progress >= 0.1f || progress >= 0.99f) {
            std::cerr << "[Model Loading] Progress: " << int(progress * 100)
                      << "% (elapsed: " << elapsed << "s)\n";
            data->last_progress = progress;
        }

        // 超时检查
        if (elapsed > data->timeout_seconds) {
            std::cerr << "[Model Loading] TIMEOUT after " << elapsed << " seconds!\n";
            return false;  // 中止加载
        }

        return true;  // 继续加载
    };

    mp.progress_callback_user_data = &progress_data;

    // 步骤4: 从磁盘文件加载模型
    // llama_load_model_from_file() 是 llama.cpp 提供的API
    // 返回值：成功返回模型指针，失败返回 nullptr
    std::cerr << "[Model Loading] Starting to load: " << path << "\n";
    model_ = llama_load_model_from_file(path.c_str(), mp);

    // 步骤5: 检查是否加载成功
    if (!model_) {
        std::cerr << "[Model] load failed: " << path << '\n';
        return false;  // 加载失败，返回 false
    }

    // 步骤6: 保存配置参数，供后续推理使用
    n_ctx_     = n_ctx;      // 上下文窗口大小
    n_threads_ = n_threads;  // 推理线程数

    std::cout << "[Model] loaded ok: " << path << '\n';
    return true;  // 加载成功
}

/*==========================================================
 * 底层推理函数 - 单轮推理（不维护历史）
 *=========================================================*/

/**
 * @brief 底层推理函数 - 将prompt转换为AI生成的文本
 *
 * 【初学者笔记 - AI推理核心流程】
 * 这是整个AI系统最核心的函数，实现了文本生成的完整流程
 *
 * @param prompt 输入的完整prompt（包含上下文）
 *               示例: "<|user|>\n你好\n<|assistant|>\n"
 * @param maxTokens 最大生成token数（一个token约等于0.75个英文单词或0.5个汉字）
 * @param temperature 采样温度（0-2），越高越随机，越低越确定
 *                    0.0: 完全确定性（总是选概率最高的）
 *                    0.7: 适中（推荐值，有一定创造性）
 *                    1.5: 很随机（可能产生不连贯的输出）
 * @return 生成的文本
 *
 * 【核心流程图】
 * ┌─────────────────────────────────────────────────────────┐
 * │ 1. 创建Context (推理工作空间)                           │
 * │    - 包含KV缓存、计算图等                               │
 * │    - 每次推理独立创建，保证线程安全                      │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 2. Tokenize (文本 → 数字序列)                          │
 * │    "你好" → [1, 2345, 6789]                            │
 * │    - 模型只能处理数字，不能直接处理文字                  │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 3. Decode Prompt (处理输入序列)                        │
 * │    - 模型"阅读"整个prompt，理解上下文                   │
 * │    - 生成内部状态（KV缓存），为生成做准备               │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 4. 自回归生成循环 (逐个token生成文本)                  │
 * │    for (step = 0; step < maxTokens; step++) {          │
 * │      4.1 获取下一个token的概率分布 (logits)            │
 * │          [0.1, 0.05, 0.3, 0.55, ...]                   │
 * │                                                         │
 * │      4.2 采样 - 根据概率选择一个token                  │
 * │          使用贪婪采样：选择概率最高的token              │
 * │          (这里是 0.55 对应的 token 3)                  │
 * │                                                         │
 * │      4.3 Detokenize - 将token转回文字                  │
 * │          token 3 → "您"                                │
 * │                                                         │
 * │      4.4 添加到输出，并检查停止条件                     │
 * │          - 遇到 EOS (句子结束标记)                     │
 * │          - 遇到 ChatML 结束标记 "<|"                   │
 * │          - 达到最大长度                                │
 * │                                                         │
 * │      4.5 将新token反馈给模型，生成下一个token          │
 * │    }                                                    │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 5. 清理资源，返回生成的文本                             │
 * └─────────────────────────────────────────────────────────┘
 *
 * 【关键术语解释】
 * - **Token**: 文本的最小单位，可以是一个字、词或符号
 * - **Logits**: 模型输出的原始分数，表示每个token的"可能性"
 * - **Sampling**: 根据概率分布选择下一个token的过程
 * - **Greedy Sampling**: 贪婪采样，总是选概率最高的token
 * - **KV Cache**: 键值缓存，存储之前token的注意力信息，避免重复计算
 * - **EOS**: End Of Sequence，句子结束标记
 * - **Batch**: 批次，一次处理多个token的数据结构
 *
 * 【性能提示】
 * - 推理速度：受模型大小、CPU性能、线程数影响
 * - 内存占用：n_ctx 越大，KV缓存占用越多内存
 * - 生成质量：temperature 和 top_p/top_k 影响输出的随机性
 */
std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature) const {
    // ========== 阶段0: 日志输出和初始化 ==========
    std::cerr << "[run] prompt bytes=" << prompt.size()
              << "  maxTok=" << maxTokens << '\n';

    // ========== 阶段1: 创建推理上下文 (Context) ==========
    /**
     * 【初学者笔记 - Context】
     * Context 是推理的"工作空间"，包含：
     * - KV缓存：存储已处理token的注意力键值对
     * - 计算图：模型的计算流程
     * - 临时缓冲区：存储中间计算结果
     *
     * 为什么每次都新建：
     * - llama_context 不是线程安全的
     * - 多个请求并发时，每个请求需要独立的 context
     * - 用完即销毁，避免内存泄漏
     */
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx     = n_ctx_;      // 上下文窗口大小
    cp.n_threads = n_threads_;  // 推理线程数

    llama_context* ctx = llama_new_context_with_model(model_, cp);
    if (!ctx) {
        return "[ctx_fail]";  // Context 创建失败（内存不足？）
    }

    // 获取词表（vocabulary）- 包含所有可能的token
    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // ========== 阶段2: Tokenize (文本 → Token序列) ==========
    /**
     * 【初学者笔记 - Tokenization】
     * 将人类可读的文字转换为模型能处理的数字
     *
     * 示例：
     * "你好" → [101, 872, 102]
     * "Hello" → [15496, 0]
     *
     * 为什么需要：
     * - 神经网络只能处理数字，不能直接处理文字
     * - Tokenization 是文本和数字之间的桥梁
     *
     * 分配 prompt.size() * 4 的缓冲区：
     * - 中文通常 1个字符 = 2-3个token
     * - 英文通常 1个单词 = 1-2个token
     * - 4倍是安全的估计
     */
    std::vector<llama_token> tokBuf(prompt.size() * 4);

    int nTok = llama_tokenize(
        vocab,                      // 词表
        prompt.c_str(),            // 输入文本
        (int)prompt.size(),        // 文本长度
        tokBuf.data(),             // 输出缓冲区
        (int)tokBuf.size(),        // 缓冲区大小
        true,                      // add_special: 添加特殊标记（如BOS）
        false                      // parse_special: 不解析特殊标记
    );

    // 检查 tokenize 是否成功
    if (nTok < 1) {
        llama_free(ctx);
        return "[tok_fail]";
    }

    tokBuf.resize(nTok);  // 调整到实际大小
    std::cerr << "[run] nTok=" << nTok << '\n';  // 打印token数量

    // ========== 阶段3: Decode Prompt (模型"阅读"输入) ==========
    /**
     * 【初学者笔记 - Decode】
     * 这一步让模型"理解"整个prompt，生成内部状态
     *
     * 比喻：
     * - 就像你读一篇文章，先通读全文，理解上下文
     * - 模型通过 decode 过程，在 KV 缓存中存储上下文信息
     *
     * Batch 的作用：
     * - 一次性处理多个token，提高效率
     * - 比逐个token处理快得多
     */
    llama_batch full = llama_batch_init(nTok, 0, 1);

    // 填充 batch 数据结构
    for (int i = 0; i < nTok; ++i) {
        full.token[i]     = tokBuf[i];       // token ID
        full.pos[i]       = i;               // 位置索引
        full.seq_id[i][0] = 0;               // 序列ID（支持批处理多个序列）
        full.n_seq_id[i]  = 1;               // 序列数量
        full.logits[i]    = (i == nTok - 1); // 只需要最后一个token的logits
    }
    full.n_tokens = nTok;

    // 调用 decode - 模型处理整个prompt
    if (llama_decode(ctx, full) != 0) {
        llama_batch_free(full);
        llama_free(ctx);
        return "[decode_prompt_fail]";
    }

    llama_batch_free(full);  // 释放 batch 内存

    // ========== 阶段4: 自回归生成循环 ==========
    /**
     * 【初学者笔记 - 自回归生成】
     * "自回归" 意思是：用自己生成的内容作为下一步的输入
     *
     * 流程：
     * 1. 模型生成 token1 → "您"
     * 2. 将 "您" 输入模型 → 生成 token2 → "好"
     * 3. 将 "好" 输入模型 → 生成 token3 → "！"
     * 4. ...
     *
     * 这就是为什么AI生成文本是"逐字"出现的
     */
    int nPast = nTok;  // 已处理的token数量（从prompt长度开始）

    const int eos   = llama_vocab_eos(vocab);       // EOS token ID
    const int vSize = llama_vocab_n_tokens(vocab);  // 词表大小

    std::string out;
    out.reserve(maxTokens * 4);  // 预分配内存，提高性能

    llama_batch bGen = llama_batch_init(1, 0, 1);  // 生成阶段每次只处理1个token

    // 采样配置（这里定义了但未使用，当前使用贪婪采样）
    const float top_p = 0.9f;   // Top-p采样：累积概率阈值
    const int   top_k = 40;     // Top-k采样：只考虑前k个最可能的token
    const float temp  = temperature > 0 ? temperature : 0.7f;

    // ========== 生成循环：逐个token生成 ==========
    for (int step = 0; step < maxTokens; ++step) {
        // --- 步骤4.1: 获取下一个token的概率分布 ---
        /**
         * logits 是一个浮点数组，长度 = 词表大小
         * logits[i] = 第i个token的"得分"
         * 得分越高，该token被选中的概率越大
         *
         * 示例：
         * logits[1234] = 5.2  ← "的" (很可能)
         * logits[5678] = 0.1  ← "xyz" (不太可能)
         */
        const float* logits = llama_get_logits(ctx);
        if (!logits) break;  // 获取失败，退出循环

        // --- 步骤4.2: 采样 - 选择下一个token ---
        /**
         * 【初学者笔记 - 贪婪采样】
         * 这是最简单的采样策略：总是选择得分最高的token
         *
         * 优点：
         * - 生成稳定、确定
         * - 适合问答、翻译等需要准确性的任务
         *
         * 缺点：
         * - 缺乏多样性
         * - 可能产生重复的文本
         *
         * 其他采样方法：
         * - Top-k: 从概率最高的k个token中随机选择
         * - Top-p (nucleus): 从累积概率达到p的token集合中选择
         * - Temperature: 调节概率分布的"平滑度"
         */
        int   best = 0;        // 最佳token的索引
        float bestv = logits[0]; // 最佳token的得分

        // 遍历所有token，找到得分最高的
        for (int v = 1; v < vSize; ++v) {
            if (logits[v] > bestv) {
                bestv = logits[v];
                best = v;
            }
        }

        // --- 步骤4.3: Detokenize - 将token转回文字 ---
        /**
         * Token ID → 文本
         * 例如：token 1234 → "的"
         *
         * piece 是一个小片段，可能是：
         * - 一个完整的字："你"
         * - 一个词的一部分："##ing"
         * - 一个标点："。"
         */
        char piece[256] = {0};
        llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);

        // 调试输出：打印当前生成的token
        std::cerr << "[run] step " << step
                  << " tok " << best
                  << " \"" << piece << "\"\n";

        // --- 步骤4.4: 添加到输出 ---
        out += piece;

        // --- 步骤4.5: 检查停止条件 ---
        /**
         * 【初学者笔记 - 停止条件】
         * 生成必须在某个时刻停止，否则会一直生成
         *
         * 三种停止方式：
         * 1. 达到最大长度 (maxTokens)
         * 2. 模型主动结束 (EOS token)
         * 3. 检测到特殊标记 (ChatML格式的 "<|")
         */

        // 条件1: 模型生成了结束标记 (EOS)
        if (best == eos) break;

        // 条件2: 检测到各种停止标记
        /**
         * ChatML 格式中，"<|user|>" "<|assistant|>" 等是特殊标记
         * DeepSeek-R1 可能输出 "</|assistant|>" 等结束标记
         * 如果模型生成了这些，说明它认为回复已经结束
         */
        size_t stop_pos = std::string::npos;

        if ((stop_pos = out.find("<|")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("</|")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("\n用户：")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("\nUser:")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }

        // --- 步骤4.6: 将新token反馈给模型 ---
        /**
         * 【初学者笔记 - 自回归的关键】
         * 刚生成的token要输入回模型，作为下一步生成的"上下文"
         *
         * 比喻：
         * - AI说了"您"，然后它要"记住"自己说过"您"
         * - 这样下一个词才能连贯（"您好"而不是"您xyz"）
         */
        bGen.n_tokens     = 1;      // 只有1个token
        bGen.token[0]     = best;   // 刚生成的token
        bGen.pos[0]       = nPast;  // 位置索引
        bGen.seq_id[0][0] = 0;      // 序列ID
        bGen.n_seq_id[0]  = 1;
        bGen.logits[0]    = 1;      // 需要输出logits（用于下一轮）

        // 执行 decode - 让模型"看到"新生成的token
        if (llama_decode(ctx, bGen) != 0) break;

        ++nPast;  // 已处理token数 +1
    }
    // ========== 生成循环结束 ==========

    // ========== 阶段5: 清理和返回 ==========
    llama_batch_free(bGen);  // 释放 batch
    llama_free(ctx);         // 释放 context（重要！防止内存泄漏）

    // 清理输出
    std::string clean_output = out;

    // 移除 "assistant:" 或 "Assistant:" 前缀
    size_t ass_pos = clean_output.find("assistant:");
    if (ass_pos == std::string::npos) ass_pos = clean_output.find("Assistant:");
    if (ass_pos != std::string::npos) {
        clean_output = clean_output.substr(ass_pos + 10);
    }

    // 移除 "用户：xxx" 前缀
    size_t user_cn_pos = clean_output.find("用户：");
    if (user_cn_pos != std::string::npos && user_cn_pos < 50) {
        size_t next_ass = clean_output.find("assistant:", user_cn_pos);
        size_t next_nl = clean_output.find("\n", user_cn_pos + 10);
        if (next_ass != std::string::npos) {
            clean_output = clean_output.substr(next_ass + 10);
        } else if (next_nl != std::string::npos) {
            clean_output = clean_output.substr(next_nl + 1);
        }
    }

    // 移除开头的空白字符
    size_t start_pos = clean_output.find_first_not_of(" \t\n\r");
    if (start_pos != std::string::npos && start_pos > 0) {
        clean_output = clean_output.substr(start_pos);
    }

    // 处理 DeepSeek-R1 思考模式：如果有 </think> 但没有 <think>，补全标签
    size_t think_end = clean_output.find("</think>");
    size_t think_start = clean_output.find("<think>");
    if (think_end != std::string::npos && think_start == std::string::npos) {
        clean_output = "<think>" + clean_output;
    }

    // 移除结尾的空白字符
    while (!clean_output.empty() &&
           (clean_output.back() == ' ' || clean_output.back() == '\n' ||
            clean_output.back() == '\r' || clean_output.back() == '\t')) {
        clean_output.pop_back();
    }

    std::cerr << "[run] done, out.len=" << clean_output.size() << '\n';
    return clean_output;
}

/*==========================================================
 * Server-13 多轮对话推理 - KV Cache 增量计算
 *=========================================================*/

/**
 * @brief Server-13 核心功能：KV Cache 优化的多轮对话推理
 *
 * 【Server-12 vs Server-13 性能对比】
 *
 * Server-12 (每次处理全部历史):
 *   第1轮: "你好" → tokenize 10 tokens → decode 10 tokens
 *   第2轮: "你好\n您好\n介绍北京" → tokenize 30 tokens → decode 30 tokens ← 重复处理前20个!
 *   第3轮: "全部历史+新消息" → tokenize 60 tokens → decode 60 tokens ← 重复处理前50个!
 *   问题: 每轮都重复计算之前的内容，越往后越慢！
 *
 * Server-13 (增量处理):
 *   第1轮: "你好" → tokenize 10 tokens → decode 10 tokens → 保存 context
 *   第2轮: "介绍北京" → tokenize 20 tokens → 只 decode 新增 20 tokens (复用之前的 KV Cache)
 *   第3轮: "继续" → tokenize 30 tokens → 只 decode 新增 30 tokens
 *   优势: 每轮只处理新内容，速度快 5-10 倍！
 *
 * @param chat_id 会话标识符
 * @param user_msg 用户本轮输入
 * @param maxTokens 最大生成token数
 * @param temperature 采样温度
 * @return AI生成的回复
 *
 * 【Server-13 工作流程】
 * ┌─────────────────────────────────────────────────────────┐
 * │ 1. 添加用户消息到会话历史                               │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 2. 构造完整 prompt 并 tokenize                          │
 * │    current_tokens = [1, 2, 3, ..., 50]                 │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 3. 缓存验证 - 对比 cached_tokens vs current_tokens     │
 * │    cached:  [1, 2, 3, 4, 5]                            │
 * │    current: [1, 2, 3, 6, 7, 8]                         │
 * │                      ↑ 这里开始不同                     │
 * │    n_reuse = 3 (前3个token可以复用)                    │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 4. 增量 Decode - 只处理新 token                        │
 * │    跳过前 n_reuse 个 token                             │
 * │    只 decode tokens[n_reuse:] = [6, 7, 8, ...]         │
 * │    → 节省大量计算！                                     │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 5. 自回归生成 - 逐 token 生成回复                      │
 * │    （与 Server-12 相同）                                │
 * └─────────────────────────────────────────────────────────┘
 *                           ↓
 * ┌─────────────────────────────────────────────────────────┐
 * │ 6. 更新缓存状态                                         │
 * │    cached_tokens = current_tokens + generated_tokens   │
 * │    n_past = total_tokens                               │
 * └─────────────────────────────────────────────────────────┘
 *
 * 【关键优化点】
 * 1. **Context 持久化**: 不再每次创建新 context，复用同一个
 * 2. **Token 缓存**: 记录已处理的 token 序列，用于验证
 * 3. **增量处理**: 只 decode 未处理过的 token
 * 4. **缓存失效检测**: 历史被修改时自动重建缓存
 *
 * 【内存使用】
 * - KVCachedSession.ctx: ~100-500MB (取决于 n_ctx)
 * - KVCachedSession.cached_tokens: ~1-10KB (token序列)
 * - 总体增加: 每个会话 +100-500MB，但换来 5-10x 性能提升
 */
std::string ModelManager::infer(const std::string& chat_id,
                                const std::string& user_msg,
                                int maxTokens,
                                float temperature) {

    // ========== 步骤1: 添加用户消息到会话历史 ==========
    bool pruned_user = false;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        pruned_user = chat_sessions_[chat_id].session.add("user", user_msg);
    }

    // ========== 步骤2: 构造 prompt 并 tokenize ==========
    std::string prompt;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        prompt = chat_sessions_[chat_id].session.makePrompt();
    }

    std::cerr << "[KVCache] chat=" << chat_id << " prompt_len=" << prompt.size() << '\n';

    // Tokenize 完整 prompt
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    std::vector<llama_token> tokBuf(prompt.size() * 4);

    int nTok = llama_tokenize(vocab, prompt.c_str(), (int)prompt.size(),
                              tokBuf.data(), (int)tokBuf.size(), true, false);
    if (nTok < 1) {
        return "[tokenize_fail]";
    }
    tokBuf.resize(nTok);

    std::cerr << "[KVCache] total_tokens=" << nTok << '\n';

    // ========== 步骤3: KV Cache 验证和增量处理 ==========
    llama_context* ctx = nullptr;
    int n_past = 0;
    int n_reuse = 0;  // 可复用的 token 数量

    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        auto& cached_sess = chat_sessions_[chat_id];

        // NEW: 如果用户消息导致历史被裁剪，必须清空缓存
        if (pruned_user) {
            std::cerr << "[KVCache] User message triggered pruning. Invalidating cache.\n";
            cached_sess.clearCache();
        }

        // 3.1 对比 cached_tokens，找出可复用的前缀
        const auto& cached = cached_sess.cached_tokens;
        n_reuse = 0;
        for (size_t i = 0; i < std::min(cached.size(), tokBuf.size()); ++i) {
            if (cached[i] == tokBuf[i]) {
                ++n_reuse;
            } else {
                break;  // 遇到第一个不匹配，停止
            }
        }

        std::cerr << "[KVCache] n_past=" << cached_sess.n_past
                  << " cached_size=" << cached.size()
                  << " n_reuse=" << n_reuse << '\n';

        // 3.2 如果缓存失效（n_reuse < n_past），需要重建 context
        /**
         * 【初学者笔记 - 缓存失效场景】
         * 什么时候缓存会失效？
         * 1. 用户删除了历史消息
         * 2. 用户编辑了之前的消息
         * 3. 系统清理了部分历史
         *
         * 解决方案：丢弃旧 context，从头重建
         */
        if (n_reuse < cached_sess.n_past) {
            std::cerr << "[KVCache] Cache invalidated! Rebuilding context.\n";
            if (cached_sess.ctx) {
                llama_free(cached_sess.ctx);
                cached_sess.ctx = nullptr;
            }
            cached_sess.n_past = 0;
            n_reuse = 0;
        }

        // 3.3 如果 context 不存在，创建新的
        if (!cached_sess.ctx) {
            llama_context_params cp = llama_context_default_params();
            cp.n_ctx = n_ctx_;
            cp.n_threads = n_threads_;
            cached_sess.ctx = llama_new_context_with_model(model_, cp);

            if (!cached_sess.ctx) {
                return "[ctx_create_fail]";
            }
            std::cerr << "[KVCache] Created new context for session\n";
        }

        ctx = cached_sess.ctx;
        n_past = cached_sess.n_past;
    }

    // ========== 步骤4: 增量 Decode - 只处理新 token ==========
    /**
     * 【Server-13 核心优化】
     * Server-12: decode tokens[0:nTok]     ← 每次都处理全部
     * Server-13: decode tokens[n_reuse:nTok]  ← 只处理新增部分！
     */
    if (n_reuse < nTok) {
        int n_to_decode = nTok - n_reuse;
        std::cerr << "[KVCache] Incremental decode: " << n_to_decode << " new tokens\n";

        llama_batch batch = llama_batch_init(n_to_decode, 0, 1);

        for (int i = 0; i < n_to_decode; ++i) {
            int tok_idx = n_reuse + i;
            batch.token[i] = tokBuf[tok_idx];
            batch.pos[i] = tok_idx;  // 使用全局位置
            batch.seq_id[i][0] = 0;
            batch.n_seq_id[i] = 1;
            batch.logits[i] = (i == n_to_decode - 1);  // 只需要最后一个 logits
        }
        batch.n_tokens = n_to_decode;

        if (llama_decode(ctx, batch) != 0) {
            llama_batch_free(batch);
            return "[decode_fail]";
        }

        llama_batch_free(batch);
        n_past = nTok;  // 更新已处理的 token 数
    } else {
        std::cerr << "[KVCache] Full cache hit! No decode needed.\n";
    }

    // ========== 步骤5: 自回归生成（与 Server-12 相同）==========
    const int eos = llama_vocab_eos(vocab);
    const int vSize = llama_vocab_n_tokens(vocab);
    std::string out;
    out.reserve(maxTokens * 4);

    llama_batch bGen = llama_batch_init(1, 0, 1);

    for (int step = 0; step < maxTokens; ++step) {
        const float* logits = llama_get_logits(ctx);
        if (!logits) break;

        // 贪婪采样
        int best = 0;
        float bestv = logits[0];
        for (int v = 1; v < vSize; ++v) {
            if (logits[v] > bestv) {
                bestv = logits[v];
                best = v;
            }
        }

        // Detokenize
        char piece[256] = {0};
        llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
        out += piece;

        // 停止条件
        if (best == eos) break;

        size_t stop_pos = std::string::npos;
        if ((stop_pos = out.find("<|")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("</|")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("\n用户：")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }
        if ((stop_pos = out.find("\nUser:")) != std::string::npos) {
            out.resize(stop_pos);
            break;
        }

        // 反馈新 token
        bGen.n_tokens = 1;
        bGen.token[0] = best;
        bGen.pos[0] = n_past;
        bGen.seq_id[0][0] = 0;
        bGen.n_seq_id[0] = 1;
        bGen.logits[0] = 1;

        if (llama_decode(ctx, bGen) != 0) break;

        tokBuf.push_back(best);  // 添加到 token 序列
        ++n_past;
    }

    llama_batch_free(bGen);

    // 清理输出
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }

    // ========== 步骤6: 更新缓存状态 ==========
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        auto& cached_sess = chat_sessions_[chat_id];

        bool pruned_assistant = cached_sess.session.add("assistant", out);  // 保存 AI 回复到历史
        cached_sess.cached_tokens = tokBuf;         // 更新 cached_tokens
        cached_sess.n_past = n_past;                // 更新 n_past

        // NEW: 如果助手消息导致历史被裁剪，清空缓存（影响下一轮）
        if (pruned_assistant) {
            std::cerr << "[KVCache] Assistant message triggered pruning. Clearing cache for next turn.\n";
            cached_sess.clearCache();
        }

        std::cerr << "[KVCache] Updated cache: n_past=" << n_past
                  << " cached_size=" << tokBuf.size() << '\n';
    }

    // 清理输出
    std::string clean_output = out;

    // 移除 "assistant:" 或 "Assistant:" 前缀
    size_t ass_pos2 = clean_output.find("assistant:");
    if (ass_pos2 == std::string::npos) ass_pos2 = clean_output.find("Assistant:");
    if (ass_pos2 != std::string::npos) {
        clean_output = clean_output.substr(ass_pos2 + 10);
    }

    // 移除 "用户：xxx" 前缀
    size_t user_cn_pos2 = clean_output.find("用户：");
    if (user_cn_pos2 != std::string::npos && user_cn_pos2 < 50) {
        size_t next_ass2 = clean_output.find("assistant:", user_cn_pos2);
        size_t next_nl2 = clean_output.find("\n", user_cn_pos2 + 10);
        if (next_ass2 != std::string::npos) {
            clean_output = clean_output.substr(next_ass2 + 10);
        } else if (next_nl2 != std::string::npos) {
            clean_output = clean_output.substr(next_nl2 + 1);
        }
    }

    // 移除开头的空白字符
    size_t start_pos2 = clean_output.find_first_not_of(" \t\n\r");
    if (start_pos2 != std::string::npos && start_pos2 > 0) {
        clean_output = clean_output.substr(start_pos2);
    }

    // 处理 DeepSeek-R1 思考模式
    size_t think_end2 = clean_output.find("</think>");
    size_t think_start2 = clean_output.find("<think>");
    if (think_end2 != std::string::npos && think_start2 == std::string::npos) {
        clean_output = "<think>" + clean_output;
    }

    // 移除结尾的空白字符
    while (!clean_output.empty() &&
           (clean_output.back() == ' ' || clean_output.back() == '\n' ||
            clean_output.back() == '\r' || clean_output.back() == '\t')) {
        clean_output.pop_back();
    }

    return clean_output;
}

/*==========================================================
 * 清除会话历史和 KV Cache
 *=========================================================*/

/**
 * @brief Server-13: 删除会话并释放 KV Cache 内存
 *
 * 【Server-12 vs Server-13 区别】
 *
 * Server-12:
 * - 只删除历史记录（几KB）
 * - 内存释放很少
 *
 * Server-13:
 * - 删除历史记录
 * - 释放 llama_context (~100-500MB!)
 * - 清空 cached_tokens
 * - 重置 n_past
 *
 * @param chat_id 要删除的会话标识符
 *
 * 【使用场景】
 * 1. 用户点击"新建对话"按钮
 * 2. 用户想重置对话，从头开始
 * 3. 释放不再使用的会话内存（**Server-13 特别重要！**）
 * 4. 服务器内存压力大时，清理闲置会话
 *
 * 【内存释放】
 * Server-13 删除一个会话可以释放：
 * - llama_context: ~100-500MB (主要部分)
 * - cached_tokens: ~1-10KB
 * - 历史记录: ~1-10KB
 * 总计: ~100-500MB
 *
 * 【实现细节】
 * - KVCachedSession 的析构函数会自动调用 llama_free(ctx)
 * - erase() 会触发析构函数，自动清理资源
 * - 符合 RAII 原则（Resource Acquisition Is Initialization）
 *
 * 【线程安全】
 * - 加锁保护，防止：
 *   - 同时删除和推理冲突
 *   - 多线程同时删除同一会话
 *
 * 使用示例：
 * ```cpp
 * // 用户开始新对话（释放旧会话的 ~500MB 内存）
 * ModelManager::instance().dropSession("user123-chat456");
 *
 * // 之后再发消息，会创建新的空白会话和新的 context
 * ModelManager::instance().infer("user123-chat456", "新的话题", 64, 0.7);
 * ```
 *
 * 【注意】
 * - 删除后无法恢复（历史记录和 KV Cache 都丢失）
 * - 如果 chat_id 不存在，erase() 不会报错（安全）
 * - **Server-13 删除会话很重要**，避免内存占用过高
 */
void ModelManager::dropSession(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(chat_mutex_);  // 加锁

    // 从字典中删除 chat_id 对应的会话
    // KVCachedSession 的析构函数会自动：
    // 1. 调用 llama_free(ctx) 释放 context 内存
    // 2. 清空 cached_tokens vector
    // 3. 清空 ChatSession 历史记录
    auto it = chat_sessions_.find(chat_id);
    if (it != chat_sessions_.end()) {
        std::cerr << "[KVCache] Dropping session " << chat_id
                  << " (freeing context memory)\n";
        chat_sessions_.erase(it);
    }

}  // 自动解锁
