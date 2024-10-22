#include <iostream>
#include <future>
#include <thread>

// 一个模拟耗时计算的函数（例如，它可能是IO操作或CPU密集型任务）
std::string heavyComputation(int n) {
    std::cout << "Starting heavy computation on a separate thread...\n";
    std::this_thread::sleep_for(std::chrono::seconds(3)); // 模拟耗时操作
    return "The result of the computation for input: " + std::to_string(n);
}

int main() {
    // 启动异步计算，并获取一个future对象来追踪结果
    std::future<std::string> futureResult = std::async(std::launch::async, heavyComputation, 42);

    std::cout << "Continuing with other tasks in the main thread...\n";

    // 在未来某个时间点检查异步操作是否完成并获取结果
    if (futureResult.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::cout << "Future is ready. Result: " << futureResult.get() << "\n";
    } else {
        std::cout << "Future not ready yet.\n";
    }

    // 确保主线程等待所有异步任务完成后再结束
    futureResult.wait();

    std::cout << "Main thread finished.\n";
    
    return 0;
}