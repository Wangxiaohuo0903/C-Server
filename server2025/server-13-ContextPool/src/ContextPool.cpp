#include "ContextPool.h"
#include <iostream>
#include <stdexcept>

ContextPool::ContextPool(llama_model* model, size_t pool_size, uint32_t n_ctx, int n_threads)
    : model_(model), n_ctx_(n_ctx), n_threads_(n_threads), pool_size_(pool_size) {

    if (!model_) {
        throw std::invalid_argument("ContextPool: model cannot be null");
    }

    if (pool_size == 0) {
        throw std::invalid_argument("ContextPool: pool_size must be > 0");
    }

    // 预创建 pool_size 个 context
    std::cout << "[ContextPool] Initializing pool with " << pool_size
              << " contexts (n_ctx=" << n_ctx
              << ", n_threads=" << n_threads << ")..." << std::endl;

    for (size_t i = 0; i < pool_size; ++i) {
        llama_context* ctx = createContext();
        if (!ctx) {
            // 创建失败，清理已创建的 context
            for (auto* c : all_contexts_) {
                llama_free(c);
            }
            throw std::runtime_error("ContextPool: failed to create context " + std::to_string(i));
        }
        available_contexts_.push(ctx);
        all_contexts_.push_back(ctx);
    }

    std::cout << "[ContextPool] Pool initialized successfully" << std::endl;
}

ContextPool::~ContextPool() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "[ContextPool] Destroying pool, freeing "
              << all_contexts_.size() << " contexts..." << std::endl;

    // 释放所有 context
    for (auto* ctx : all_contexts_) {
        llama_free(ctx);
    }

    // 清空队列和集合
    while (!available_contexts_.empty()) {
        available_contexts_.pop();
    }
    all_contexts_.clear();

    std::cout << "[ContextPool] Pool destroyed" << std::endl;
}

llama_context* ContextPool::createContext() {
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;
    cp.n_threads = n_threads_;
    cp.n_batch = 512;  // 批处理大小

    llama_context* ctx = llama_new_context_with_model(model_, cp);
    return ctx;
}

llama_context* ContextPool::acquire(uint32_t timeout_ms) {
    auto start_time = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(mutex_);

    // 无限等待模式
    if (timeout_ms == 0) {
        cv_.wait(lock, [this] { return !available_contexts_.empty(); });

        llama_context* ctx = available_contexts_.front();
        available_contexts_.pop();

        total_acquires_.fetch_add(1, std::memory_order_relaxed);
        return ctx;
    }

    // 超时等待模式
    auto timeout = std::chrono::milliseconds(timeout_ms);
    bool acquired = cv_.wait_for(lock, timeout, [this] {
        return !available_contexts_.empty();
    });

    if (!acquired || available_contexts_.empty()) {
        // 等待超时
        total_waits_.fetch_add(1, std::memory_order_relaxed);

        auto end_time = std::chrono::steady_clock::now();
        auto wait_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        total_wait_time_us_.fetch_add(wait_time_us, std::memory_order_relaxed);

        return nullptr;
    }

    // 成功获取
    llama_context* ctx = available_contexts_.front();
    available_contexts_.pop();

    total_acquires_.fetch_add(1, std::memory_order_relaxed);

    // 记录等待时间
    auto end_time = std::chrono::steady_clock::now();
    auto wait_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    if (wait_time_us > 1000) {  // 超过 1ms 才记录
        total_waits_.fetch_add(1, std::memory_order_relaxed);
        total_wait_time_us_.fetch_add(wait_time_us, std::memory_order_relaxed);
    }

    return ctx;
}

void ContextPool::release(llama_context* ctx) {
    if (!ctx) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        available_contexts_.push(ctx);
        total_releases_.fetch_add(1, std::memory_order_relaxed);
    }

    // 通知等待的线程
    cv_.notify_one();
}

ContextPool::PoolStats ContextPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    PoolStats stats;
    stats.total_contexts = all_contexts_.size();
    stats.available_contexts = available_contexts_.size();
    stats.in_use_contexts = stats.total_contexts - stats.available_contexts;
    stats.total_acquires = total_acquires_.load(std::memory_order_relaxed);
    stats.total_releases = total_releases_.load(std::memory_order_relaxed);
    stats.total_waits = total_waits_.load(std::memory_order_relaxed);

    // 计算平均等待时间
    uint64_t total_wait_us = total_wait_time_us_.load(std::memory_order_relaxed);
    if (stats.total_waits > 0) {
        stats.avg_wait_time_ms = static_cast<double>(total_wait_us) / stats.total_waits / 1000.0;
    } else {
        stats.avg_wait_time_ms = 0.0;
    }

    return stats;
}
