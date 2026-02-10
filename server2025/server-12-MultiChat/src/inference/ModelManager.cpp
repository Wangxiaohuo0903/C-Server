/*==========================================================
 * ModelManager.cpp - 模型管理器实现
 *
 * 【初学者指南】这个文件实现了AI大模型的加载和推理逻辑
 *
 * 核心概念：
 * 1. **llama.cpp**: 一个C++实现的大语言模型推理库，支持CPU推理
 * 2. **GGUF格式**: llama.cpp使用的量化模型格式，体积小，推理快
 * 3. **单例模式**: 确保全局只有一个模型实例，节省内存
 * 4. **tokenize**: 将文本转换为模型能理解的数字序列
 * 5. **decode**: 模型处理token序列的过程
 * 6. **采样**: 从模型输出的概率分布中选择下一个token
 *
 * 推理流程：
 *   文本输入 → tokenize → decode → 采样 → detokenize → 文本输出
 *=========================================================*/

#include "ModelManager.h"
#include "llama.h"           // llama.cpp 核心库，提供模型加载和推理API
#include <iostream>
#include <cstring>
#include <algorithm>

/*==========================================================
 * 构造函数和析构函数
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

    // 步骤4: 从磁盘文件加载模型
    // llama_load_model_from_file() 是 llama.cpp 提供的API
    // 返回值：成功返回模型指针，失败返回 nullptr
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

        // 条件2: 检测到 ChatML 结束标记
        /**
         * ChatML 格式中，"<|user|>" "<|assistant|>" 等是特殊标记
         * 如果模型生成了这些，说明它认为回复已经结束
         * 需要截断到 "<|" 之前，避免输出格式标记
         */
        if (out.find("<|") != std::string::npos) {
            size_t pos = out.find("<|");
            out.resize(pos);  // 截断
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

    std::cerr << "[run] done, out.len=" << out.size() << '\n';
    return out;  // 返回生成的文本
}

/*==========================================================
 * 多轮对话推理 - 自动管理历史记录
 *=========================================================*/

/**
 * @brief 多轮对话推理 - 记住历史，像真人聊天一样
 *
 * 【初学者笔记 - 多轮对话 vs 单轮推理】
 * 这是 Server-12 的核心功能！与 Server-11 的单轮推理不同：
 *
 * Server-11 (单轮):
 *   用户: "你好"
 *   AI:   "您好！"
 *   用户: "我刚才说了什么？"
 *   AI:   "我不知道"  ← 不记得之前说过什么
 *
 * Server-12 (多轮):
 *   用户: "你好"
 *   AI:   "您好！"
 *   用户: "我刚才说了什么？"
 *   AI:   "你刚才说'你好'"  ← 记得历史对话！
 *
 * @param chat_id 会话标识符，用于隔离不同对话
 *                - 不同 chat_id 的对话互不干扰
 *                - 同一 chat_id 的对话会记住历史
 *                - 格式示例: "user123-chat456" 或任意字符串
 * @param user_msg 用户本轮输入的消息
 * @param maxTokens 最大生成token数
 * @param temperature 采样温度
 * @return AI 生成的回复
 *
 * 【工作流程】
 * ┌──────────────────────────────────────────────┐
 * │ 1. 将用户消息添加到会话历史                  │
 * │    history: ["你好", "您好！", "介绍北京"]   │
 * └──────────────────────────────────────────────┘
 *                    ↓
 * ┌──────────────────────────────────────────────┐
 * │ 2. 构造 ChatML 格式的完整 prompt            │
 * │    <|user|>                                  │
 * │    你好                                      │
 * │    <|assistant|>                             │
 * │    您好！                                    │
 * │    <|user|>                                  │
 * │    介绍一下北京                              │
 * │    <|assistant|>                             │
 * └──────────────────────────────────────────────┘
 *                    ↓
 * ┌──────────────────────────────────────────────┐
 * │ 3. 调用 raw_infer 生成回复                  │
 * │    （底层推理，不维护历史）                  │
 * └──────────────────────────────────────────────┘
 *                    ↓
 * ┌──────────────────────────────────────────────┐
 * │ 4. 清理生成的文本（去除多余换行）            │
 * └──────────────────────────────────────────────┘
 *                    ↓
 * ┌──────────────────────────────────────────────┐
 * │ 5. 将 AI 回复添加到会话历史                 │
 * │    history: [..., "北京是中国的首都..."]     │
 * └──────────────────────────────────────────────┘
 *
 * 【关键设计】
 * - 会话隔离: 使用 chat_id 作为 key 存储独立的会话
 * - 线程安全: 所有操作都加锁保护
 * - 自动管理: 用户不需要手动拼接历史，函数内部自动处理
 *
 * 【内存管理】
 * - 历史记录存储在内存中（chat_sessions_ 字典）
 * - 服务器重启后会丢失
 * - ChatSession 内部有 prune() 机制，自动限制历史长度
 */
std::string ModelManager::infer(const std::string& chat_id,
                                const std::string& user_msg,
                                int maxTokens,
                                float temperature) {

    // ========== 步骤1: 保存用户消息到会话历史 ==========
    /**
     * 【初学者笔记 - 为什么要加锁】
     * 想象多个用户同时聊天：
     * - 用户A发消息 "你好"
     * - 用户B发消息 "hi"
     *
     * 如果不加锁，可能：
     * - 用户A的消息被写到用户B的历史里
     * - 或者两个消息同时写入，导致数据损坏
     *
     * std::lock_guard 自动加锁和解锁：
     * - 进入 {} 时自动加锁
     * - 离开 {} 时自动解锁
     * - 即使发生异常也能正确解锁（RAII机制）
     */
    {
        std::lock_guard<std::mutex> g(chat_mutex_);  // 加锁

        // chat_sessions_[chat_id] 会自动创建 ChatSession（如果不存在）
        // 这是 C++ map 的特性：用 [] 访问不存在的 key 会自动创建默认值
        chat_sessions_[chat_id].add("user", user_msg);

    }  // 离开作用域，自动解锁

    // ========== 步骤2: 构造 ChatML 格式的 prompt ==========
    /**
     * 【初学者笔记 - 为什么要重新加锁】
     * 这里又加了一次锁，是为了读取会话历史
     *
     * 为什么不和步骤1合并：
     * - makePrompt() 可能耗时（拼接很长的历史）
     * - 如果和步骤1合并，锁的持有时间更长
     * - 会降低并发性能
     *
     * 分开加锁的好处：
     * - 每次只锁必要的操作
     * - 其他用户的请求可以在间隙中执行
     */
    std::string prompt;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);

        // makePrompt() 将历史记录转换为 ChatML 格式
        // 示例输出见函数头部注释
        prompt = chat_sessions_[chat_id].makePrompt();

    }  // 自动解锁

    // ========== 步骤3: 调用底层推理函数生成回复 ==========
    /**
     * 【初学者笔记 - 为什么这里不加锁】
     * raw_infer() 是纯计算过程，不访问共享数据：
     * - 只使用 model_（只读，不修改）
     * - 每次创建独立的 context
     * - 不访问 chat_sessions_
     *
     * 不加锁的好处：
     * - 多个用户可以同时推理（并行计算）
     * - 充分利用多核CPU
     */
    std::string output = raw_infer(prompt, maxTokens, temperature);

    // ========== 步骤4: 清理生成的文本 ==========
    /**
     * 【初学者笔记 - 为什么要清理】
     * AI 生成的文本末尾可能有多余的换行符：
     * - "北京是中国的首都\n\n\n"
     *
     * 清理后：
     * - "北京是中国的首都"
     *
     * 为什么会有多余换行：
     * - 训练数据中的噪声
     * - Tokenization 的特性
     * - 模型的生成习惯
     */
    while (!output.empty() &&
           (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();  // 移除最后一个字符
    }

    // ========== 步骤5: 保存 AI 回复到会话历史 ==========
    /**
     * 【初学者笔记 - 完整的对话循环】
     * 至此，一轮完整的对话完成：
     *
     * 会话历史变化：
     * 之前: ["user: 你好", "assistant: 您好！"]
     * 之后: ["user: 你好", "assistant: 您好！",
     *        "user: 介绍北京", "assistant: 北京是..."]
     *
     * 下次用户再发消息时，这些历史都会被包含在 prompt 中
     * 模型就能"记住"之前的对话内容
     */
    {
        std::lock_guard<std::mutex> g(chat_mutex_);  // 再次加锁
        chat_sessions_[chat_id].add("assistant", output);
    }  // 自动解锁

    return output;  // 返回生成的回复
}

/*==========================================================
 * 清除会话历史
 *=========================================================*/

/**
 * @brief 删除指定会话的所有历史记录
 *
 * 【初学者笔记 - 会话清理】
 * 使用场景：
 * 1. 用户点击"新建对话"按钮
 * 2. 用户想重置对话，从头开始
 * 3. 释放不再使用的会话内存
 *
 * @param chat_id 要删除的会话标识符
 *
 * 【实现细节】
 * - 从 chat_sessions_ 字典中删除对应的 key
 * - ChatSession 对象会被自动销毁（C++ 析构函数）
 * - 释放历史记录占用的内存
 *
 * 【线程安全】
 * - 加锁保护，防止并发删除导致问题
 *
 * 使用示例：
 * ```cpp
 * // 用户开始新对话
 * ModelManager::instance().dropSession("user123-chat456");
 *
 * // 之后再发消息，会创建新的空白会话
 * ModelManager::instance().infer("user123-chat456", "新的话题", 64, 0.7);
 * ```
 *
 * 【注意】
 * - 删除后无法恢复（历史记录丢失）
 * - 如果 chat_id 不存在，erase() 不会报错（安全）
 */
void ModelManager::dropSession(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(chat_mutex_);  // 加锁

    // 从字典中删除 chat_id 对应的会话
    // erase() 的行为：
    // - 如果 key 存在：删除并返回 1
    // - 如果 key 不存在：什么都不做，返回 0
    chat_sessions_.erase(chat_id);

}  // 自动解锁
