#pragma once

#include <nlohmann/json.hpp>

// 停采指令消费端 (计划任务 15): MVP 阶段日志占位。
// M2 起接入真实停采逻辑 (经 iot.cmd.queue 下发至 hms-iot)。
namespace hms::StopCollectionHandler {

// 启动消费线程 (main.cc 调用一次, 传入 rabbitmq.json 配置);
// RabbitMQ 不可用时仅记日志不阻塞启动。
void start(const nlohmann::json& config);

// 停止消费线程
void stop();

} // namespace hms::StopCollectionHandler
