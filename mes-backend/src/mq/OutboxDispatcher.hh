#pragma once

// Outbox 投递器 (计划任务 15 / 设计文档 7.5 节):
// 报工事务内写 mq_outbox -> COMMIT 后由本投递器扫描投递。
// 并发控制: Drogon 定时任务 + pg_try_advisory_xact_lock (MVP 不引 Redis 分布式锁),
// 多实例部署时同一时刻只有一个实例扫描, 无重复投递。
//
// 全项目 MQ 投递唯一入口约定 (grep 门禁):
// 业务事务内只允许执行 OutboxService::kEnqueueSql 写入 mq_outbox,
// 禁止直接调用 MqProducer。
namespace mes::OutboxService {

// 事务内追加 outbox 记录的唯一 SQL (调用方在自己的事务中执行)
inline constexpr const char* kEnqueueSql =
    "INSERT INTO mq_outbox (exchange, routing_key, payload) VALUES ($1, $2, $3)";

} // namespace mes::OutboxService

namespace mes::OutboxDispatcher {

// 注册定时扫描任务 (main.cc 启动时调用一次, 每 2s 一轮)
void start();

} // namespace mes::OutboxDispatcher
