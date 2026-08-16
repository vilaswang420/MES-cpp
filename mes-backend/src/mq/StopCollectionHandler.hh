#pragma once

#include <nlohmann/json.hpp>

// 停采指令消费端 (计划任务 15): P1-2.2 停采二次投递。
// 消费 iot.cmd.queue 的 stop_collection 消息 -> 查工单绑定设备 (line_id 关联)
// -> 逐台写 mq_outbox (routing_key=cmd.stop.{device_id}),
// 由 OutboxDispatcher 投递到 iot.exchange, mes-iot 经 cmd.stop.#
// (iot.cmd.collector.queue) 独占消费。
namespace mes::StopCollectionHandler {

// 启动消费线程 (main.cc 调用一次, 传入 rabbitmq.json 配置);
// RabbitMQ 不可用时仅记日志不阻塞启动。
void start(const nlohmann::json& config);

// 停止消费线程
void stop();

} // namespace mes::StopCollectionHandler
