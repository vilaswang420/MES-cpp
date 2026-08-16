#include <gtest/gtest.h>

#include "mq/OutboxPolicy.hh"

// P2-3.3 Outbox 重试/死信策略单测:
// 失败递增后 retry_count 达到上限 kMaxRetry 置死信 status=3, 否则 status=2。
using hms::OutboxPolicy::kMaxRetry;
using hms::OutboxPolicy::nextStatusOnFailure;

TEST(OutboxPolicy, EarlyFailuresStayRetryable) {
    // 前 4 次失败 (递增后 1..4 < 5): 保持 status=2 可重试
    for (int before = 0; before < kMaxRetry - 1; ++before)
        EXPECT_EQ(nextStatusOnFailure(before), 2) << "retry_count=" << before;
}

TEST(OutboxPolicy, LimitReachedBecomesDeadLetter) {
    // 第 5 次失败 (before=4, 递增后 5 >= 5): 置死信 status=3
    EXPECT_EQ(nextStatusOnFailure(kMaxRetry - 1), 3);
    // 防御: 理论上查询不会选中 before >= kMaxRetry 的行, 但语义应保持死信
    EXPECT_EQ(nextStatusOnFailure(kMaxRetry), 3);
    EXPECT_EQ(nextStatusOnFailure(99), 3);
}

TEST(OutboxPolicy, BoundaryAtMaxRetry) {
    ASSERT_EQ(kMaxRetry, 5); // 与 010_fixes mq_outbox.status 注释的死信语义一致
}
