#pragma once

#include <drogon/drogon.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// CPU 密集型工作 (bcrypt 哈希/校验等) 卸载池:
// HTTP IO 线程若同步执行 cost=10 的 bcrypt (~50-100ms) 会阻塞事件循环,
// 高并发下拖垮全部请求 P95 (k6 基线要求 P95<300ms)。
// 计算提交到常驻工作线程, 完成后回到发起请求的事件循环执行回调。
namespace mes {

namespace detail {

inline void submitCpuJob(std::function<void()> job) {
    static struct Pool {
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> q;
        std::mutex m;
        std::condition_variable cv;
        Pool() {
            auto n = (std::max)(2u, std::thread::hardware_concurrency());
            for (unsigned i = 0; i < n; ++i) {
                workers.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock lk(m);
                            cv.wait(lk, [this] { return !q.empty(); });
                            job = std::move(q.front());
                            q.pop();
                        }
                        job();
                    }
                });
                workers.back().detach(); // 进程级生命周期, 退出时自然终止
            }
        }
    } pool;
    {
        std::lock_guard lk(pool.m);
        pool.q.push(std::move(job));
    }
    pool.cv.notify_one();
}

} // namespace detail

// fn() 在工作线程执行, cb(result) 回到当前事件循环执行
template <typename Fn, typename Cb> void offloadCpu(Fn fn, Cb cb) {
    auto loop = drogon::app().getLoop();
    auto sharedFn = std::make_shared<Fn>(std::move(fn));
    detail::submitCpuJob([sharedFn, cb = std::move(cb), loop]() mutable {
        auto result = (*sharedFn)();
        loop->queueInLoop(
            [cb = std::move(cb), result = std::move(result)]() mutable { cb(std::move(result)); });
    });
}

} // namespace mes
