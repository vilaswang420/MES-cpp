#include "mq/StopCollectionHandler.hh"

#include <SimpleAmqpClient/SimpleAmqpClient.h>
#include <drogon/drogon.h>

#include <atomic>
#include <thread>

namespace hms::StopCollectionHandler {

namespace {

std::atomic<bool> g_stop{false};
std::thread g_thread;

} // namespace

void start(const nlohmann::json& config) {
    // vhost "/" 必须 URL 编码为 %2F, 否则 rabbitmq-c 解析为空 vhost 导致 NOT_ALLOWED
    auto url = config.value("amqp_url", "amqp://guest:guest@127.0.0.1:5672/%2F");
    std::vector<std::string> queues;
    if (config.contains("consume_queues"))
        for (const auto& q : config["consume_queues"])
            queues.push_back(q.get<std::string>());
    if (queues.empty())
        queues.push_back("iot.cmd.queue");

    g_stop = false;
    g_thread = std::thread([url, queues] {
        AmqpClient::Channel::ptr_t channel;
        std::string consumerTag;
        while (!g_stop) {
            try {
                if (!channel) {
                    channel = AmqpClient::Channel::CreateFromUri(url);
                    // 队列由 deploy/mq/topology.json 预声明, 此处 passive 校验
                    for (const auto& q : queues)
                        channel->DeclareQueue(q, true /*passive*/);
                    // no_ack=false: 手动 ack, 处理失败可 requeue
                    consumerTag = channel->BasicConsume(queues[0], "", true, false);
                    LOG_INFO << "stop-collection consumer connected";
                }
                // 阻塞拉取, 1s 超时轮询退出标志
                AmqpClient::Envelope::ptr_t envelope;
                if (channel->BasicConsumeMessage(consumerTag, envelope, 1000) && envelope) {
                    // MVP 日志占位: M2 接入真实停采下发 (任务 18/19)
                    LOG_INFO << "[stop-collection] routing_key=" << envelope->RoutingKey()
                             << " payload=" << envelope->Message()->Body();
                    channel->BasicAck(envelope);
                }
            } catch (const std::exception& e) {
                LOG_WARN << "stop-collection consumer unavailable: " << e.what();
                channel.reset();
                consumerTag.clear();
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
    });
    g_thread.detach();
}

void stop() {
    g_stop = true;
}

} // namespace hms::StopCollectionHandler
