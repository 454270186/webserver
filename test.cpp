#include <iostream>
#include <chrono>
#include "threadpool.h"

void print(int i) {
    printf("task %d started\n", i);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    printf("task %d finished\n", i);
}

int main() {
    Threadpool threadpool(4); // 创建4个线程的线程池

    // 向线程池提交10个任务
    for (int i = 0; i < 10; i++) {
        threadpool.append([i](){ print(i); });
    }

    std::this_thread::sleep_for(std::chrono::seconds(6));
    std::cout << "All tasks finished." << std::endl;

    return 0;
}