#pragma once

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

// Runs fn(y) for every row in [y0, y1] across the available cores.
template <typename F>
void parallelRows(int y0, int y1, F fn)
{
    const int n = y1 - y0 + 1;
    if (n <= 0)
        return;
    int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 32);
    if (n < 64)
        threads = 1;
    if (threads == 1) {
        for (int y = y0; y <= y1; ++y)
            fn(y);
        return;
    }
    std::vector<std::thread> pool;
    std::atomic<int> next(y0);
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                int y = next.fetch_add(1);
                if (y > y1)
                    break;
                fn(y);
            }
        });
    for (auto &th : pool)
        th.join();
}
