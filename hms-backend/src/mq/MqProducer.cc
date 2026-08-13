#include "mq/MqProducer.hh"

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <drogon/drogon.h>

#include <cstdint>
#include <mutex>

namespace hms::MqProducer {

namespace {

std::mutex g_mutex;
AmqpClient::Channel::ptr_t g_channel;
std::string g_amqpUrl;

// 懒连接: 首次使用或断线后重建
bool ensureChannelLocked() {
    if (g_channel)
        return true;
    try {
        g_channel = AmqpClient::Channel::CreateFromUri(g_amqpUrl);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "amqp connect failed: " << e.what();
        g_channel.reset();
        return false;
    }
}

} // namespace

void init(const nlohmann::json& config) {
    std::lock_guard lock(g_mutex);
    // vhost "/" 必须 URL 编码为 %2F, 否则 rabbitmq-c 解析为空 vhost 导致 NOT_ALLOWED
    g_amqpUrl = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    // publisher_confirms: vcpkg 版 SimpleAmqpClient 无 confirm API, 配置保留待升级库后启用;
    // 可靠性由 mq_outbox 重投兜底 (OutboxDispatcher)
}

bool publishSync(const std::string& exchange, const std::string& routingKey,
                 const std::string& payload) {
    std::lock_guard lock(g_mutex);
    if (!ensureChannelLocked())
        return false;
    try {
        // exchange 由 deploy/mq/topology.json 预声明; 此处 passive 校验存在性
        g_channel->DeclareExchange(exchange, AmqpClient::Channel::EXCHANGE_TYPE_TOPIC,
                                   true /*passive*/);
        auto msg = AmqpClient::BasicMessage::Create(payload);
        msg->ContentType("application/json");
        msg->ContentEncoding("utf-8");
        msg->DeliveryMode(AmqpClient::BasicMessage::dm_persistent);
        g_channel->BasicPublish(exchange, routingKey, msg);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "amqp publish failed: " << e.what();
        // 连接可能已坏: 丢弃触发下次重建
        g_channel.reset();
        return false;
    }
}

// 指标采集 (任务 28): passive declare 取队列积压深度, 失败返回 -1
int64_t queueMessageCount(const std::string& queue) {
    std::lock_guard lock(g_mutex);
    if (!ensureChannelLocked())
        return -1;
    try {
        uint32_t msgs = 0;
        uint32_t consumers = 0;
        g_channel->DeclareQueueWithCounts(queue, msgs, consumers, true /*passive*/, true, false,
                                          false, AmqpClient::Table());
        return msgs;
    } catch (const std::exception& e) {
        LOG_WARN << "amqp queue depth probe failed (" << queue << "): " << e.what();
        g_channel.reset();
        return -1;
    }
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_channel.reset();
}

} // namespace hms::MqProducer
