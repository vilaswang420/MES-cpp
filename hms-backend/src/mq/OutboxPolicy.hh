#pragma once

// Outbox 重试/死信策略 (P2-3.3, 纯逻辑 header-only 可单测):
// 投递失败后 retry_count 递增; 达到上限 kMaxRetry 后置终态死信 status=3
// (不再被 Dispatcher 选中, 保留行供人工介入/查询), 否则 status=2 等待下次重试。
// 语义对照 mq_outbox.status 注释 (010_fixes):
//   0:待投递 1:已投递 2:失败 3:死信(重试超限, 人工介入)
namespace hms::OutboxPolicy {

// 失败重试上限: 第 kMaxRetry 次失败后置死信
inline constexpr int kMaxRetry = 5;

// 投递失败后的目标状态: retryCountBefore 为当前 retry_count (未递增)。
// 例: before=4 -> 递增后 5 >= kMaxRetry -> 死信 3; before=3 -> 递增后 4 < 5 -> 失败 2。
inline int nextStatusOnFailure(int retryCountBefore) {
    return (retryCountBefore + 1 >= kMaxRetry) ? 3 : 2;
}

} // namespace hms::OutboxPolicy
