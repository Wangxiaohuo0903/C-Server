#include <iostream>
#include <future>
#include <chrono>

// 模拟耗时操作的函数
void longRunningTask() {
    std::this_thread::sleep_for(std::chrono::seconds(3)); // 模拟耗时3秒的操作
    std::cout << "Long running task completed" << std::endl;
    return 100;
}

int main() {
    // 启动异步任务
    auto future = std::async(std::launch::async, longRunningTask);

    std::cout << "Main thread is free to do other tasks..." << std::endl;

    // 主线程继续执行其他任务，然后等待异步任务完成
    std::cout << "Main thread is now waiting for the async task to finish." << std::endl;

    // 获取异步结果（如果未就绪则阻塞直到完成）
    future.get();

    std::cout << "Both the main thread and async task have finished their execution." << std::endl;

    return 0;
}

std::future<int> async_task = std::async(std::launch::async,longRunningTask);

// ... 其他代码 ...

// 当需要获取并使用异步任务的结果时
if (async_task.valid()) {
    // 阻塞当前线程，直到异步任务完成
    int result = async_task.wait();
    // 使用result执行其他操作...
}

