#include <gtest/gtest.h>

#include "utils/CircuitBreaker.hh"

// 熔断器计数语义单测 (计划测试计划: "熔断器计数语义")。
// 对端不可用场景由 E2E 冒烟覆盖 (tests/e2e/m2_integ_smoke.ps1)。
using mes::CircuitBreaker;

TEST(CircuitBreaker, ClosedAllowsAll) {
    CircuitBreaker cb(3, 30);
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(cb.allowRequest());
    EXPECT_EQ(cb.state(), CircuitBreaker::State::CLOSED);
}

TEST(CircuitBreaker, OpensAfterConsecutiveFailures) {
    CircuitBreaker cb(3, 30);
    cb.recordFailure();
    cb.recordFailure();
    EXPECT_TRUE(cb.allowRequest()); // 未达阈值仍放行
    cb.recordFailure();
    EXPECT_EQ(cb.state(), CircuitBreaker::State::OPEN);
    EXPECT_FALSE(cb.allowRequest()); // 熔断后快速失败
}

TEST(CircuitBreaker, SuccessResetsFailureCount) {
    // 核心语义: 成功重置计数, 历史偶发失败不得永久累计 (防误熔断)
    CircuitBreaker cb(3, 30);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordSuccess();
    EXPECT_EQ(cb.failureCount(), 0);
    cb.recordFailure();
    cb.recordFailure();
    EXPECT_EQ(cb.state(), CircuitBreaker::State::CLOSED); // 重新计数, 未达 3
}

TEST(CircuitBreaker, HalfOpenAfterCooldownAndProbeOutcome) {
    CircuitBreaker cb(1, 0); // 阈值 1, 冷却 0s -> 下一请求即半开
    cb.recordFailure();
    EXPECT_EQ(cb.state(), CircuitBreaker::State::OPEN);
    EXPECT_TRUE(cb.allowRequest()); // 冷却已过 -> 转 HALF_OPEN 放行探测
    EXPECT_EQ(cb.state(), CircuitBreaker::State::HALF_OPEN);

    cb.recordFailure(); // 半开探测失败 -> 重新 OPEN
    EXPECT_EQ(cb.state(), CircuitBreaker::State::OPEN);

    EXPECT_TRUE(cb.allowRequest()); // 再次半开
    cb.recordSuccess();             // 半开探测成功 -> CLOSED
    EXPECT_EQ(cb.state(), CircuitBreaker::State::CLOSED);
    EXPECT_EQ(cb.failureCount(), 0);
}
