#pragma once

// 设备心跳离线判定 (P1-2.9A):
// 定时扫描 iot_devices.last_heartbeat_at 超时(60s)且 status=1 的设备
// -> 原子置离线(status=0, WHERE status=1 防多实例重复) + 写 OFFLINE 告警
// 至 mq_outbox (routing_key=alert.{id}), 由 OutboxDispatcher 投递后
// AlertHandler 消费落库 iot_alerts 并 WS 广播 (复用既有告警链路)。
namespace mes::DeviceMonitor {

// 注册定时扫描任务 (main.cc 启动时调用一次, drogon loop runEvery)
void start();

} // namespace mes::DeviceMonitor
