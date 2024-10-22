// 假设有一个线程池类 ThreadPool，其中包含 enqueue 函数模板

// 1. 提交一个无参数的任务（例如：打印当前时间）
void printCurrentTime() {
    std::cout << "Current time: " << std::chrono::system_clock::now() << std::endl;
}
ThreadPool pool(4); // 创建一个包含4个工作线程的线程池
pool.enqueue(printCurrentTime); // 提交任务到线程池，无需传递参数

// 2. 提交一个带有两个整数参数的任务
void sum(int a, int b) {
    std::cout << "Sum: " << a + b << std::endl;
}
pool.enqueue(sum, 5, 7); // 提交任务到线程池，并传入参数 5 和 7

// 3. 使用 Lambda 表达式提交任务
pool.enqueue([](std::string name) { 
    std::cout << "Hello, " << name << " from a thread!" << std::endl; 
}, "User"); // 提交任务并传入字符串 "User"

// 4. 对于有大量参数的任务也同样适用
struct ComplexTask {
    void operator()(int a, double b, std::string c, std::vector<int> d) {
        // 执行复杂任务...
    }
};
ComplexTask task;
std::vector<int> vec = {...}; // 假设有数据填充
pool.enqueue(task, 10, 3.14, "Some text", vec); // 提交包含多个参数的任务
