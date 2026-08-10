#pragma once

#include <nlohmann/json.hpp>

#include <string>

// RabbitMQ 生产者封装 (SimpleAmqpClient)。
// 契约: 全项目投递唯一入口是 OutboxService (事务内写 mq_outbox),
// 本类只被 OutboxDispatcher 调用, 业务代码禁止直接使用 (grep 门禁)。
namespace hms::MqProducer {

// 从 config/rabbitmq.json 初始化 (main.cc 启动时调用一次)
void init(const nlohmann::json& config);

// 同步投递一条消息; 成功返回 true。
// 内部维护单连接 + publisher confirms; 失败由调用方决定重试策略。
bool publishSync(const std::string& exchange, const std::string& routingKey,
                 const std::string& payload);

// 关闭连接 (进程退出时调用)
void shutdown();

} // namespace hms::MqProducer
