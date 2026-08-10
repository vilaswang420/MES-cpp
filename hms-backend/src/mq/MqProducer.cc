#include "mq/MqProducer.hh"

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <drogon/drogon.h>

#include <mutex>

namespace hms::MqProducer {

namespace {

std::mutex g_mutex;
Amqp::Channel::ptr_t g_channel;
std::string g_amqpUrl;
bool g_confirms = true;

// 懒连接: 首次使用或断线后重建
bool ensureChannelLocked() {
    if (g_channel)
        return true;
    try {
        g_channel = Amqp::Channel::CreateFromUri(g_amqpUrl);
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
    g_amqpUrl = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/");
    g_confirms = config.value("publisher_confirms", true);
}

bool publishSync(const std::string& exchange, const std::string& routingKey,
                 const std::string& payload) {
    std::lock_guard lock(g_mutex);
    if (!ensureChannelLocked())
        return false;
    try {
        // exchange 由 deploy/mq/topology.json 预声明; 此处 passive 校验存在性
        g_channel->DeclareExchange(exchange, Amqp::Channel::EXCHANGE_TYPE_TOPIC, true /*passive*/);
        Amqp::Message msg(payload);
        msg.setContentType("application/json");
        msg.setContentEncoding("utf-8");
        msg.setDeliveryMode(Amqp::Message::dm_persistent);

        if (g_confirms) {
            // publisher confirms: 发布后等待 broker 确认
            g_channel->SelectConfirm();
            g_channel->PublishMessage(exchange, routingKey, msg);
            return g_channel->ConfirmMessage();
        }
        g_channel->PublishMessage(exchange, routingKey, msg);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "amqp publish failed: " << e.what();
        // 连接可能已坏: 丢弃触发下次重建
        g_channel.reset();
        return false;
    }
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_channel.reset();
}

} // namespace hms::MqProducer
