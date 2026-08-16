#pragma once

#include <chrono>
#include <mutex>

// ERP/WMS 外呼熔断器 (设计文档 7.6 节)。
// 语义: 连续 N 次失败 -> OPEN; OPEN 满 openSeconds 后下一请求转 HALF_OPEN 放行探测;
// 半开失败立即重新 OPEN, 半开成功回 CLOSED。任何状态下的成功都重置失败计数
// ("连续 N 次失败熔断"语义, 否则历史偶发失败被永久累计导致误熔断)。
namespace hms {

class CircuitBreaker {
  public:
    enum class State { CLOSED, OPEN, HALF_OPEN };

    explicit CircuitBreaker(int failThreshold = 5, int openSeconds = 30)
        : failThreshold_(failThreshold), openWindow_(std::chrono::seconds(openSeconds)) {}

    // 允许请求放行? OPEN 且未到冷却期返回 false (调用方应快速失败)
    bool allowRequest() {
        std::lock_guard<std::mutex> lk(mu_);
        switch (state_) {
        case State::CLOSED:
            return true;
        case State::OPEN:
            if (std::chrono::steady_clock::now() - openedAt_ >= openWindow_) {
                state_ = State::HALF_OPEN;
                return true;
            }
            return false;
        case State::HALF_OPEN:
            return true;
        }
        return false;
    }

    void recordSuccess() {
        std::lock_guard<std::mutex> lk(mu_);
        failureCount_ = 0;
        if (state_ == State::HALF_OPEN)
            state_ = State::CLOSED;
    }

    void recordFailure() {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ == State::HALF_OPEN) {
            state_ = State::OPEN;
            openedAt_ = std::chrono::steady_clock::now();
        } else if (++failureCount_ >= failThreshold_) {
            state_ = State::OPEN;
            openedAt_ = std::chrono::steady_clock::now();
        }
    }

    State state() {
        std::lock_guard<std::mutex> lk(mu_);
        return state_;
    }

    const char* stateName() {
        switch (state()) {
        case State::CLOSED:
            return "CLOSED";
        case State::OPEN:
            return "OPEN";
        default:
            return "HALF_OPEN";
        }
    }

    int failureCount() {
        std::lock_guard<std::mutex> lk(mu_);
        return failureCount_;
    }

  private:
    State state_ = State::CLOSED;
    int failureCount_ = 0;
    int failThreshold_;
    std::chrono::steady_clock::duration openWindow_;
    std::chrono::steady_clock::time_point openedAt_;
    std::mutex mu_;
};

} // namespace hms
