#include <iostream>
#include <functional>
#include <thread>

// 使用std::function<void()>的例子
void printHello() {
    std::cout << "Hello, World!" << std::endl;
}

// 定义一个接收std::function<void()>参数的函数
void executeTask(std::function<void()> task) {
    task(); // 调用传入的任务
}

int main() {
    // 创建一个std::function实例，并绑定到printHello函数
    std::function<void()> myTask = printHello;

    // 也可以直接创建一个lambda表达式作为std::function实例
    std::function<void()> anotherTask = [](){
        std::cout << "Another hello from lambda" << std::endl;
    };

    // 将任务传给executeTask函数执行
    executeTask(myTask);
    executeTask(anotherTask);

    // 在线程中执行任务
    std::thread thread([]{
        std::cout << "Executing in a separate thread..." << std::endl;
    });

    // 确保线程结束
    thread.join();

    return 0;
}