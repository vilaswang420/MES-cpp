#pragma once

#include <array>
#include <string>

// 工单 8 态状态机 (计划任务 14 / 设计文档 4.6 节):
// 表驱动建模, 禁止在业务代码中散落 if-else 判断状态流转。
// 纯函数实现, 独立单测: tests/test_work_order_state_machine.cc
namespace hms::WorkOrderStateMachine {

// 状态常量与 prod_work_orders.status 注释一致
// 0:待排产 1:已排产 2:已下达 3:进行中 4:已暂停 5:已完工 6:已关闭 7:已取消
enum Status : int {
    kPendingSchedule = 0, // 待排产
    kScheduled = 1,       // 已排产
    kReleased = 2,        // 已下达
    kInProgress = 3,      // 进行中
    kPaused = 4,          // 已暂停
    kCompleted = 5,       // 已完工
    kClosed = 6,          // 已关闭
    kCancelled = 7        // 已取消
};

// 触发事件 (与 4.6 节状态转换接口一一对应)
enum class Event {
    Schedule, // 排产 0->1
    Release,  // 下达 1->2
    Start,    // 开工 2->3 / 恢复 4->3
    Pause,    // 暂停 3->4
    Complete, // 完工 3->5 (报工满量自动完工也走此事件)
    Close,    // 关闭 5->6
    Cancel    // 取消 0/1/2->7 (开工后不可取消)
};

struct Transition {
    int from;
    Event event;
    int to;
};

// 全量转换表: 新增/调整流转只改这里
inline constexpr std::array<Transition, 10> kTable{{
    {kPendingSchedule, Event::Schedule, kScheduled},
    {kScheduled, Event::Release, kReleased},
    {kReleased, Event::Start, kInProgress},
    {kInProgress, Event::Pause, kPaused},
    {kPaused, Event::Start, kInProgress}, // 恢复即开工
    {kInProgress, Event::Complete, kCompleted},
    {kCompleted, Event::Close, kClosed},
    {kPendingSchedule, Event::Cancel, kCancelled},
    {kScheduled, Event::Cancel, kCancelled},
    // 已下达未开工亦允许取消: 单独列出保持表的可读性
    {kReleased, Event::Cancel, kCancelled},
}};

// 事件触发后的目标状态; 非法流转返回 -1 (调用方转 409)
inline int next(int from, Event event) {
    for (const auto& t : kTable) {
        if (t.from == from && t.event == event)
            return t.to;
    }
    return -1;
}

// 直接校验 from->to 是否合法 (供外部直接指定目标态的场景)
inline bool canTransition(int from, int to) {
    if (from == to)
        return true; // 幂等: 重复触发视为成功
    for (const auto& t : kTable) {
        if (t.from == from && t.to == to)
            return true;
    }
    return false;
}

// 终态: 不允许任何流转
inline bool isTerminal(int status) {
    return status == kClosed || status == kCancelled;
}

inline const char* statusName(int status) {
    switch (status) {
    case kPendingSchedule:
        return "待排产";
    case kScheduled:
        return "已排产";
    case kReleased:
        return "已下达";
    case kInProgress:
        return "进行中";
    case kPaused:
        return "已暂停";
    case kCompleted:
        return "已完工";
    case kClosed:
        return "已关闭";
    case kCancelled:
        return "已取消";
    default:
        return "未知";
    }
}

} // namespace hms::WorkOrderStateMachine
