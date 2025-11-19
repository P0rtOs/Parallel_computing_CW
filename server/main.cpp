#include <iostream>
#include <thread>
#include <vector>
#include "concurrent_queue.h"

int main() {
    ConcurrentQueue<int> q;

    std::thread producer1([&]() {
        for (int i = 0; i < 50; ++i) {
            q.push(i);
        }
    });

    std::thread producer2([&]() {
        for (int i = 100; i < 150; ++i) {
            q.push(i);
        }
    });

    std::thread consumer([&]() {
        int value;
        for (;;) {
            if (q.try_pop(value)) {
                std::cout << "Got: " << value << "\n";
            } else {
                std::this_thread::yield();
            }

            if (q.empty()) {
            }
        }
    });

    producer1.join();
    producer2.join();

    consumer.detach();

    return 0;
}
