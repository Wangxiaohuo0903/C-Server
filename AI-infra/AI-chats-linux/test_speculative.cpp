/**
 * @file test_speculative.cpp
 * @brief 推测式解码测试程序
 *
 * 编译:
 *   cd AI-chats-linux
 *   mkdir -p build && cd build
 *   cmake .. && make -j4
 *
 * 运行:
 *   ./test_speculative \
 *       --model ../models/tinyllama-1.1b-q4.gguf \
 *       --model-draft ../models/draft/tinyllama-160m-q4.gguf \
 *       --prompt "用Python实现快速排序" \
 *       --n-predict 100
 */

#include "src/inference/SpeculativeDecoder.h"
#include "llama.h"
#include <iostream>
#include <string>
#include <cstring>

// 简单的命令行参数解析
struct Args {
    std::string model_path;
    std::string draft_model_path;
    std::string prompt = "Hello, how are you?";
    int n_predict = 100;
    int n_ctx = 2048;
    int n_threads = 4;
    int n_draft = 16;
    int n_draft_min = 5;
    float p_min = 0.9f;
    float temperature = 0.7f;
    bool verbose = false;
};

bool parse_args(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--model" && i + 1 < argc) {
            args.model_path = argv[++i];
        } else if (arg == "--model-draft" && i + 1 < argc) {
            args.draft_model_path = argv[++i];
        } else if (arg == "--prompt" && i + 1 < argc) {
            args.prompt = argv[++i];
        } else if (arg == "--n-predict" && i + 1 < argc) {
            args.n_predict = std::stoi(argv[++i]);
        } else if (arg == "--n-ctx" && i + 1 < argc) {
            args.n_ctx = std::stoi(argv[++i]);
        } else if (arg == "--n-threads" && i + 1 < argc) {
            args.n_threads = std::stoi(argv[++i]);
        } else if (arg == "--n-draft" && i + 1 < argc) {
            args.n_draft = std::stoi(argv[++i]);
        } else if (arg == "--n-draft-min" && i + 1 < argc) {
            args.n_draft_min = std::stoi(argv[++i]);
        } else if (arg == "--p-min" && i + 1 < argc) {
            args.p_min = std::stof(argv[++i]);
        } else if (arg == "--temperature" && i + 1 < argc) {
            args.temperature = std::stof(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            args.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  --model PATH             Target model path (required)\n";
            std::cout << "  --model-draft PATH       Draft model path (required)\n";
            std::cout << "  --prompt TEXT            Input prompt (default: 'Hello, how are you?')\n";
            std::cout << "  --n-predict N            Max tokens to generate (default: 100)\n";
            std::cout << "  --n-ctx N                Context size (default: 2048)\n";
            std::cout << "  --n-threads N            Thread count (default: 4)\n";
            std::cout << "  --n-draft N              Draft tokens per iteration (default: 16)\n";
            std::cout << "  --n-draft-min N          Min draft tokens (default: 5)\n";
            std::cout << "  --p-min FLOAT            Min draft confidence (default: 0.9)\n";
            std::cout << "  --temperature FLOAT      Sampling temperature (default: 0.7)\n";
            std::cout << "  --verbose, -v            Enable verbose logging\n";
            std::cout << "  --help, -h               Show this message\n";
            return false;
        }
    }

    if (args.model_path.empty()) {
        std::cerr << "ERROR: --model is required\n";
        return false;
    }

    if (args.draft_model_path.empty()) {
        std::cerr << "ERROR: --model-draft is required\n";
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    Args args;

    if (!parse_args(argc, argv, args)) {
        return 1;
    }

    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║      Speculative Decoding Test Program                   ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // 初始化 llama.cpp
    llama_backend_init();

    std::cout << "📝 Configuration:\n";
    std::cout << "  Target model:    " << args.model_path << "\n";
    std::cout << "  Draft model:     " << args.draft_model_path << "\n";
    std::cout << "  Prompt:          " << args.prompt << "\n";
    std::cout << "  Max tokens:      " << args.n_predict << "\n";
    std::cout << "  Context size:    " << args.n_ctx << "\n";
    std::cout << "  Threads:         " << args.n_threads << "\n";
    std::cout << "  Draft tokens:    " << args.n_draft << "\n";
    std::cout << "  Draft min:       " << args.n_draft_min << "\n";
    std::cout << "  Draft p_min:     " << args.p_min << "\n";
    std::cout << "  Temperature:     " << args.temperature << "\n";
    std::cout << "  Verbose:         " << (args.verbose ? "yes" : "no") << "\n";
    std::cout << "\n";

    // 1. 加载目标模型
    std::cout << "📦 Loading target model...\n";

    llama_model_params model_params = llama_model_default_params();
    llama_model* model_tgt = llama_model_load_from_file(args.model_path.c_str(), model_params);

    if (!model_tgt) {
        std::cerr << "❌ Failed to load target model: " << args.model_path << "\n";
        return 1;
    }

    std::cout << "✅ Target model loaded\n";

    // 2. 创建目标上下文
    std::cout << "🔧 Creating target context...\n";

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = args.n_ctx;
    ctx_params.n_threads = args.n_threads;
    ctx_params.n_batch = args.n_ctx;

    llama_context* ctx_tgt = llama_init_from_model(model_tgt, ctx_params);

    if (!ctx_tgt) {
        std::cerr << "❌ Failed to create target context\n";
        llama_model_free(model_tgt);
        return 1;
    }

    std::cout << "✅ Target context created\n";
    std::cout << "\n";

    // 3. 创建推测式解码器
    std::cout << "🚀 Initializing Speculative Decoder...\n";

    SpeculativeDecoder::Config config;
    config.n_draft = args.n_draft;
    config.n_draft_min = args.n_draft_min;
    config.p_min = args.p_min;
    config.n_ctx_draft = args.n_ctx;
    config.n_threads_draft = std::max(1, args.n_threads / 2);  // Draft 用一半线程
    config.temperature = args.temperature;
    config.verbose = args.verbose;

    SpeculativeDecoder decoder(model_tgt, ctx_tgt, args.draft_model_path, config);

    if (!decoder.isDraftModelLoaded()) {
        std::cerr << "❌ Failed to initialize speculative decoder\n";
        llama_free(ctx_tgt);
        llama_model_free(model_tgt);
        return 1;
    }

    std::cout << "\n";

    // 4. 推理
    std::cout << "🎯 Starting inference...\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "\n";
    std::cout << "Prompt: " << args.prompt << "\n";
    std::cout << "\n";
    std::cout << "Output:\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

    std::string result = decoder.infer(args.prompt, args.n_predict, args.temperature);

    std::cout << result << "\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "\n";

    // 5. 打印统计信息
    std::cout << "📊 Performance Statistics:\n";
    auto stats = decoder.getStats();
    std::cout << "  Generated tokens:     " << stats.n_predict << "\n";
    std::cout << "  Drafted tokens:       " << stats.n_drafted << "\n";
    std::cout << "  Accepted tokens:      " << stats.n_accepted << "\n";
    std::cout << "  Accept rate:          " << (stats.accept_rate * 100.0) << "%\n";
    std::cout << "  Speedup:              " << stats.speedup << "x\n";
    std::cout << "  Time (draft):         " << stats.time_draft_ms << " ms\n";
    std::cout << "  Time (verify):        " << stats.time_verify_ms << " ms\n";
    std::cout << "  Time (total):         " << stats.time_total_ms << " ms\n";
    std::cout << "  Tokens per second:    " << stats.tokens_per_sec() << "\n";
    std::cout << "  Ms per token:         " << stats.ms_per_token() << "\n";
    std::cout << "\n";

    // 6. 清理
    llama_free(ctx_tgt);
    llama_model_free(model_tgt);
    llama_backend_free();

    std::cout << "✅ Test complete!\n";
    std::cout << "\n";

    return 0;
}
