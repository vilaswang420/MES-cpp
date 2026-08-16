#include <gtest/gtest.h>

#include "models/WorkOrderStateMachine.hh"

// 工单 8 态状态机单测 (计划: 测试计划-单测)。
// 覆盖: 正向主链路 / 暂停恢复 / 取消窗口 / 终态不可流转 / 幂等。
using namespace mes::WorkOrderStateMachine;

TEST(WorkOrderStateMachine, HappyPath) {
    // 0 待排产 -> 1 已排产 -> 2 已下达 -> 3 进行中 -> 5 已完工 -> 6 已关闭
    int s = kPendingSchedule;
    s = next(s, Event::Schedule);
    ASSERT_EQ(s, kScheduled);
    s = next(s, Event::Release);
    ASSERT_EQ(s, kReleased);
    s = next(s, Event::Start);
    ASSERT_EQ(s, kInProgress);
    s = next(s, Event::Complete);
    ASSERT_EQ(s, kCompleted);
    s = next(s, Event::Close);
    ASSERT_EQ(s, kClosed);
}

TEST(WorkOrderStateMachine, PauseAndResume) {
    int s = next(kInProgress, Event::Pause);
    ASSERT_EQ(s, kPaused);
    s = next(s, Event::Start); // 恢复即开工
    ASSERT_EQ(s, kInProgress);
}

TEST(WorkOrderStateMachine, CancelOnlyBeforeStart) {
    EXPECT_EQ(next(kPendingSchedule, Event::Cancel), kCancelled);
    EXPECT_EQ(next(kScheduled, Event::Cancel), kCancelled);
    EXPECT_EQ(next(kReleased, Event::Cancel), kCancelled);
    // 开工后不允许取消
    EXPECT_EQ(next(kInProgress, Event::Cancel), -1);
    EXPECT_EQ(next(kPaused, Event::Cancel), -1);
    EXPECT_EQ(next(kCompleted, Event::Cancel), -1);
}

TEST(WorkOrderStateMachine, IllegalTransitionsRejected) {
    EXPECT_EQ(next(kPendingSchedule, Event::Start), -1);   // 未下达不能开工
    EXPECT_EQ(next(kPendingSchedule, Event::Release), -1); // 未排产不能下达
    EXPECT_EQ(next(kReleased, Event::Complete), -1);       // 未开工不能完工
    EXPECT_EQ(next(kPaused, Event::Complete), -1);         // 暂停中不能完工
    EXPECT_EQ(next(kCompleted, Event::Start), -1);         // 完工不能回退
}

TEST(WorkOrderStateMachine, TerminalStates) {
    EXPECT_TRUE(isTerminal(kClosed));
    EXPECT_TRUE(isTerminal(kCancelled));
    for (Event e : {Event::Schedule, Event::Release, Event::Start, Event::Pause, Event::Complete,
                    Event::Close, Event::Cancel}) {
        EXPECT_EQ(next(kClosed, e), -1);
        EXPECT_EQ(next(kCancelled, e), -1);
    }
}

TEST(WorkOrderStateMachine, CanTransitionIdempotent) {
    // 重复触发同状态视为合法 (幂等语义)
    EXPECT_TRUE(canTransition(kInProgress, kInProgress));
    EXPECT_TRUE(canTransition(kReleased, kInProgress));
    EXPECT_FALSE(canTransition(kReleased, kCompleted));
    EXPECT_FALSE(canTransition(kClosed, kInProgress));
}
