#include "ModelManager.h"
#include "llama.h"
#include <iostream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <random>

// Helper to add token to batch
static void batch_add(llama_batch& batch, llama_token id, llama_pos pos, const std::vector<llama_seq_id>& seq_ids, bool logits) {
    batch.token[batch.n_tokens] = id;
    batch.pos[batch.n_tokens] = pos;
    batch.n_seq_id[batch.n_tokens] = seq_ids.size();
    for (size_t i = 0; i < seq_ids.size(); ++i) {
        batch.seq_id[batch.n_tokens][i] = seq_ids[i];
    }
    batch.logits[batch.n_tokens] = logits;
    batch.n_tokens++;
}

ModelManager::ModelManager() = default;

ModelManager::~ModelManager() {
    stopBatchLoop();
    if (ctx_) llama_free(ctx_);
    if (model_) llama_free_model(model_);
}

ModelManager& ModelManager::instance() {
    static ModelManager inst;
    return inst;
}

bool ModelManager::loadModel(const std::string& path, int n_ctx, int n_threads, int batch_size) {
    std::lock_guard<std::mutex> g(mtx_);
    
    if (model_) llama_free_model(model_);
    if (ctx_) llama_free(ctx_);

    llama_model_params mp = llama_model_default_params();
    mp.use_mmap = false;
    mp.n_gpu_layers = 0; 
    
    model_ = llama_load_model_from_file(path.c_str(), mp);
    if (!model_) {
        std::cerr << "[ModelManager] Failed to load model: " << path << "\n";
        return false;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx;           // 总上下文大小 (e.g. 16384)
    cp.n_threads = n_threads;
    cp.n_batch = 2048;          // 最大批处理 token 数
    
    // 这里的 batch_size 是并发序列数 (n_seq_max)
    max_batch_size_ = batch_size;
    
    // CRITICAL FIX: Ensure context supports multiple sequences
    cp.n_seq_max = max_batch_size_; 
    
    ctx_ = llama_new_context_with_model(model_, cp);
    if (!ctx_) return false;

    slots_.resize(max_batch_size_);
    for(int i=0; i<max_batch_size_; ++i) {
        slots_[i].id = i;
        slots_[i].active = false;
        slots_[i].n_past = 0;
        slots_[i].last_token = -1;
    }

    n_ctx_ = n_ctx;
    n_threads_ = n_threads;

    std::cout << "[ModelManager] Model loaded. Batch size: " << max_batch_size_ << "\n";

    startBatchLoop();

    return true;
}

void ModelManager::startBatchLoop() {
    if (running_) return;
    running_ = true;
    batch_thread_ = std::thread(&ModelManager::batchLoop, this);
}

void ModelManager::stopBatchLoop() {
    running_ = false;
    queue_cv_.notify_all();
    if (batch_thread_.joinable()) {
        batch_thread_.join();
    }
}

std::string ModelManager::infer(const std::string& chat_id, const std::string& user_msg, int maxTokens, float temperature) {
    std::string prompt;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        if (!user_msg.empty()) {
            chat_sessions_[chat_id].add("user", user_msg);
        }
        prompt = chat_sessions_[chat_id].makePrompt();
    }

    auto req = std::make_shared<InferenceRequest>();
    req->chat_id = chat_id;
    req->prompt = prompt;
    req->max_tokens = maxTokens;
    req->temperature = temperature;
    req->timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

    auto future = req->result_promise.get_future();

    {
        std::lock_guard<std::mutex> g(queue_mutex_);
        request_queue_.push(req);
    }
    queue_cv_.notify_one();

    if (future.wait_for(std::chrono::seconds(120)) == std::future_status::timeout) {
        return "Error: Timeout";
    }
    
    std::string result = future.get();

    if (!user_msg.empty()) {
        std::lock_guard<std::mutex> g(chat_mutex_);
        chat_sessions_[chat_id].add("assistant", result);
    }

    return result;
}

std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature) const {
    ModelManager* mutable_this = const_cast<ModelManager*>(this);
    std::string temp_id = "raw_" + std::to_string(std::rand());
    
    {
        std::lock_guard<std::mutex> g(mutable_this->chat_mutex_);
        mutable_this->chat_sessions_[temp_id].reset();
        mutable_this->chat_sessions_[temp_id].add("user", prompt); 
    }
    
    std::string res = mutable_this->infer(temp_id, "", maxTokens, temperature);
    
    mutable_this->dropSession(temp_id);
    return res;
}

void ModelManager::dropSession(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(chat_mutex_);
    chat_sessions_.erase(chat_id);
}

void ModelManager::batchLoop() {
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    const int eos = llama_vocab_eos(vocab);
    const int vSize = llama_vocab_n_tokens(vocab);
    
    llama_batch batch = llama_batch_init(2048, 0, max_batch_size_);

    while (running_) {
        bool has_active_slots = false;
        for(const auto& s : slots_) if(s.active) has_active_slots = true;

        if (!has_active_slots) {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this]{ return !request_queue_.empty() || !running_; });
            if (!running_) break;
        }

        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            while (!request_queue_.empty()) {
                int best_slot = -1;
                
                for(int i=0; i<max_batch_size_; ++i) {
                    if (!slots_[i].active && slots_[i].chat_id == request_queue_.front()->chat_id) {
                        best_slot = i;
                        break;
                    }
                }
                
                if (best_slot == -1) {
                    for(int i=0; i<max_batch_size_; ++i) {
                        if (!slots_[i].active && slots_[i].chat_id.empty()) {
                            best_slot = i;
                            break;
                        }
                    }
                }
                
                if (best_slot == -1) {
                    for(int i=0; i<max_batch_size_; ++i) {
                        if (!slots_[i].active) {
                            best_slot = i;
                            llama_memory_seq_rm(llama_get_memory(ctx_), i, -1, -1); 
                            slots_[i].n_past = 0;
                            slots_[i].chat_id = "";
                            break;
                        }
                    }
                }

                if (best_slot != -1) {
                    auto req = request_queue_.front();
                    request_queue_.pop();
                    
                    Slot& s = slots_[best_slot];
                    s.active = true;
                    s.current_req = req;
                    s.chat_id = req->chat_id;
                    s.max_tokens = req->max_tokens;
                    s.temperature = req->temperature;
                    s.generated_text = "";
                    s.tokens_generated = 0;
                    
                    std::vector<llama_token> prompt_tokens(req->prompt.size() + 4);
                    int n = llama_tokenize(vocab, req->prompt.c_str(), req->prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, false);
                    prompt_tokens.resize(n);

                    llama_memory_seq_rm(llama_get_memory(ctx_), s.id, -1, -1);
                    s.n_past = 0;
                    s.last_token = -1;
                } else {
                    break;
                }
            }
        }

        batch.n_tokens = 0;
        
        for(int i=0; i<max_batch_size_; ++i) {
            Slot& s = slots_[i];
            if (!s.active) continue;

            if (s.tokens_generated == 0) {
                std::vector<llama_token> tokens(s.current_req->prompt.size() + 4);
                int n = llama_tokenize(vocab, s.current_req->prompt.c_str(), s.current_req->prompt.size(), tokens.data(), tokens.size(), true, false);
                tokens.resize(n);
                
                for(int k=0; k<n; ++k) {
                    batch_add(batch, tokens[k], s.n_past, { s.id }, k == n - 1);
                    s.n_past++;
                }
            } else {
                batch_add(batch, s.last_token, s.n_past, { s.id }, true);
                s.n_past++;
            }
        }
        
        if (batch.n_tokens == 0) continue;

        if (llama_decode(ctx_, batch) != 0) {
            std::cerr << "[ModelManager] llama_decode failed!\n";
            break;
        }

        for(int i=0; i<max_batch_size_; ++i) {
            Slot& s = slots_[i];
            if (!s.active) continue;

            int batch_idx = -1;
            for(int k=0; k<batch.n_tokens; ++k) {
                if (batch.seq_id[k][0] == s.id && batch.logits[k]) {
                    batch_idx = k;
                    break;
                }
            }
            
            if (batch_idx != -1) {
                const float* logits = llama_get_logits_ith(ctx_, batch_idx);
                
                int best = 0;
                float bestv = logits[0];
                for (int v = 1; v < vSize; ++v) {
                    if (logits[v] > bestv) {
                        bestv = logits[v];
                        best = v;
                    }
                }
                
                s.last_token = best;
                s.tokens_generated++;
                
                char piece[256] = {0};
                llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
                s.generated_text += piece;
                
                bool done = (best == eos) || (s.tokens_generated >= s.max_tokens);

                // 检测各种停止标记
                size_t stop_pos = std::string::npos;

                // 检查 <| 开头的标记 (如 <|im_end|>)
                if ((stop_pos = s.generated_text.find("<|")) != std::string::npos) {
                    s.generated_text.resize(stop_pos);
                    done = true;
                }
                // 检查 </| 开头的标记 (如 </|assistant|>)
                else if ((stop_pos = s.generated_text.find("</|")) != std::string::npos) {
                    s.generated_text.resize(stop_pos);
                    done = true;
                }
                // 检查 </think> 标记后的内容，只保留有效回复
                else if ((stop_pos = s.generated_text.find("</think>")) != std::string::npos) {
                    // 保留 </think> 之后的内容，但如果后面又出现停止标记则截断
                    std::string after_think = s.generated_text.substr(stop_pos + 8);
                    size_t next_stop = after_think.find("<|");
                    if (next_stop == std::string::npos) next_stop = after_think.find("</|");
                    if (next_stop == std::string::npos) next_stop = after_think.find("\n用户：");
                    if (next_stop != std::string::npos) {
                        s.generated_text = s.generated_text.substr(0, stop_pos + 8) + after_think.substr(0, next_stop);
                        done = true;
                    }
                }
                // 检查重复的用户输入模式
                else if ((stop_pos = s.generated_text.find("\n用户：")) != std::string::npos) {
                    s.generated_text.resize(stop_pos);
                    done = true;
                }
                else if ((stop_pos = s.generated_text.find("\nUser:")) != std::string::npos) {
                    s.generated_text.resize(stop_pos);
                    done = true;
                }
                
                if (done) {
                    // 清理输出
                    std::string clean_output = s.generated_text;

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
                    size_t start = clean_output.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos && start > 0) {
                        clean_output = clean_output.substr(start);
                    }

                    // 处理 DeepSeek-R1 思考模式：如果有 </think> 但没有 <think>，补全标签
                    size_t think_end = clean_output.find("</think>");
                    size_t think_start = clean_output.find("<think>");
                    if (think_end != std::string::npos && think_start == std::string::npos) {
                        // 有结束标签但没有开始标签，在开头添加 <think>
                        clean_output = "<think>" + clean_output;
                    }

                    // 移除结尾的空白字符
                    while (!clean_output.empty() &&
                           (clean_output.back() == ' ' || clean_output.back() == '\n' ||
                            clean_output.back() == '\r' || clean_output.back() == '\t')) {
                        clean_output.pop_back();
                    }

                    s.current_req->result_promise.set_value(clean_output);
                    s.active = false;
                }
            }
        }
    }
    
    llama_batch_free(batch);
}
