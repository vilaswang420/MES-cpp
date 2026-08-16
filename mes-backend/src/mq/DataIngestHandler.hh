#pragma once

#include <nlohmann/json.hpp>

// IoT 数据入库消费者 (计划任务 19 / 设计文档 7.3):
// 消费 iot.data.queue -> schema 校验 -> 多行 VALUES 批量写 iot_raw_data
// (500 条或 100ms 窗口) -> Redis device:latest 更新;
// 校验失败按消息头 x-retry-count 有界重试 (3 次后经 DLX 进 iot.dlq)。
// 数值越限时发布 alert.{device_id} 消息, 由 AlertHandler 落库并推送。
namespace mes::DataIngestHandler {

void start(const nlohmann::json& config);
void stop();

} // namespace mes::DataIngestHandler
