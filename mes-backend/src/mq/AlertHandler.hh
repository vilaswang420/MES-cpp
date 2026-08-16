#pragma once

#include <nlohmann/json.hpp>

// 告警消费者 (计划任务 19 / 设计文档 3.4):
// 独立消费 iot.alert.queue -> 写 iot_alerts -> Redis PUBLISH ws:broadcast:alert
// (M2 WS 广播链路订阅该频道推送大屏弹窗)。
namespace mes::AlertHandler {

void start(const nlohmann::json& config);
void stop();

} // namespace mes::AlertHandler
