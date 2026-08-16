#pragma once

// 指标采集器 (计划任务 28): 周期性采集非请求路径指标 ——
// outbox 待投递 / WS 订阅数 / 分区剩余天数 (SQL 异步) + MQ 队列深度 (阻塞 AMQP 独立线程)。
// HTTP QPS/延迟指标由 CrossCutting advice 直接埋点, 不经此处。
namespace mes::MetricsCollector {

void start();
void stop();

} // namespace mes::MetricsCollector
